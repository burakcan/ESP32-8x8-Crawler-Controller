/**
 * @file perf_metrics.c
 * @brief Performance metrics instrumentation implementation
 */

#include "perf_metrics.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

#define TAG "perf_metrics"

// Ring buffer for loop timing samples
#define LOOP_SAMPLE_COUNT 64

static struct {
    // Loop timing samples
    uint32_t loop_samples[LOOP_SAMPLE_COUNT];
    uint8_t loop_idx;
    uint32_t loop_max;
    uint32_t loop_min;
    uint32_t loop_sum;
    uint32_t loop_count;

    // Audio underruns
    uint32_t audio_underruns;

    // Task handles for stack monitoring
    TaskHandle_t main_task;
    TaskHandle_t audio_task;
    TaskHandle_t web_task;

    bool initialized;
} s_metrics;

esp_err_t perf_metrics_init(void)
{
    memset(&s_metrics, 0, sizeof(s_metrics));
    s_metrics.loop_min = UINT32_MAX;
    s_metrics.initialized = true;

    // Store main task handle (current task during init)
    s_metrics.main_task = xTaskGetCurrentTaskHandle();

    return ESP_OK;
}

void perf_metrics_record_loop(int64_t start_us)
{
    if (!s_metrics.initialized) return;

    int64_t now = esp_timer_get_time();
    uint32_t elapsed = (uint32_t)(now - start_us);

    // Update ring buffer
    s_metrics.loop_samples[s_metrics.loop_idx] = elapsed;
    s_metrics.loop_idx = (s_metrics.loop_idx + 1) % LOOP_SAMPLE_COUNT;

    // Update statistics
    s_metrics.loop_sum += elapsed;
    s_metrics.loop_count++;

    if (elapsed > s_metrics.loop_max) {
        s_metrics.loop_max = elapsed;
    }
    if (elapsed < s_metrics.loop_min) {
        s_metrics.loop_min = elapsed;
    }
}

void perf_metrics_record_underrun(void)
{
    if (!s_metrics.initialized) return;
    s_metrics.audio_underruns++;
}

void perf_metrics_get(perf_metrics_t *metrics)
{
    if (!metrics) return;
    memset(metrics, 0, sizeof(perf_metrics_t));

    if (!s_metrics.initialized) return;

    // Heap metrics
    metrics->heap_free = esp_get_free_heap_size();
    metrics->heap_min = esp_get_minimum_free_heap_size();
    metrics->heap_largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

    // Loop timing
    if (s_metrics.loop_count > 0) {
        metrics->loop_avg_us = s_metrics.loop_sum / s_metrics.loop_count;
        metrics->loop_max_us = s_metrics.loop_max;
        metrics->loop_jitter_us = s_metrics.loop_max - s_metrics.loop_min;
    }

    // Audio
    metrics->audio_underruns = s_metrics.audio_underruns;

    // Stack high water marks
    if (s_metrics.main_task) {
        metrics->stack_main = uxTaskGetStackHighWaterMark(s_metrics.main_task) * sizeof(StackType_t);
    }
    if (s_metrics.audio_task) {
        metrics->stack_audio = uxTaskGetStackHighWaterMark(s_metrics.audio_task) * sizeof(StackType_t);
    }
    if (s_metrics.web_task) {
        metrics->stack_web = uxTaskGetStackHighWaterMark(s_metrics.web_task) * sizeof(StackType_t);
    }

    metrics->samples = s_metrics.loop_count;
}

void perf_metrics_reset(void)
{
    if (!s_metrics.initialized) return;

    s_metrics.loop_idx = 0;
    s_metrics.loop_max = 0;
    s_metrics.loop_min = UINT32_MAX;
    s_metrics.loop_sum = 0;
    s_metrics.loop_count = 0;
    s_metrics.audio_underruns = 0;
    memset(s_metrics.loop_samples, 0, sizeof(s_metrics.loop_samples));
}

int perf_metrics_to_json(char *buf, size_t buf_size)
{
    perf_metrics_t m;
    perf_metrics_get(&m);

    return snprintf(buf, buf_size,
        "{"
        "\"heap\":{\"free\":%lu,\"min\":%lu,\"largest_block\":%lu},"
        "\"loop\":{\"avg_us\":%lu,\"max_us\":%lu,\"jitter_us\":%lu},"
        "\"audio\":{\"underruns\":%lu},"
        "\"stack\":{\"main\":%lu,\"audio\":%lu,\"web\":%lu},"
        "\"samples\":%lu"
        "}",
        (unsigned long)m.heap_free,
        (unsigned long)m.heap_min,
        (unsigned long)m.heap_largest_block,
        (unsigned long)m.loop_avg_us,
        (unsigned long)m.loop_max_us,
        (unsigned long)m.loop_jitter_us,
        (unsigned long)m.audio_underruns,
        (unsigned long)m.stack_main,
        (unsigned long)m.stack_audio,
        (unsigned long)m.stack_web,
        (unsigned long)m.samples
    );
}
