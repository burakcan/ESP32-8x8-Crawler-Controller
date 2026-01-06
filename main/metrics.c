/**
 * @file metrics.c
 * @brief Performance metrics collection implementation
 */

#include "metrics.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "METRICS";

// Ring buffer for loop timing samples
#define LOOP_SAMPLE_COUNT 64
static uint32_t loop_samples[LOOP_SAMPLE_COUNT];
static uint8_t loop_sample_idx = 0;
static uint32_t loop_sample_count = 0;
static uint32_t loop_time_max = 0;

// Audio underrun counter
static uint32_t audio_underrun_count = 0;

// WebSocket latency samples
#define WS_SAMPLE_COUNT 32
static uint32_t ws_samples[WS_SAMPLE_COUNT];
static uint8_t ws_sample_idx = 0;
static uint32_t ws_sample_count = 0;
static uint32_t ws_latency_max = 0;

// Mutex wait tracking
static uint32_t mutex_wait_max = 0;

// External task handles for stack checking
extern TaskHandle_t engine_task_handle;

esp_err_t metrics_init(void) {
    memset(loop_samples, 0, sizeof(loop_samples));
    memset(ws_samples, 0, sizeof(ws_samples));
    ESP_LOGI(TAG, "Metrics system initialized");
    return ESP_OK;
}

void metrics_record_loop_time(uint32_t elapsed_us) {
    loop_samples[loop_sample_idx] = elapsed_us;
    loop_sample_idx = (loop_sample_idx + 1) % LOOP_SAMPLE_COUNT;
    if (loop_sample_count < LOOP_SAMPLE_COUNT) {
        loop_sample_count++;
    }
    if (elapsed_us > loop_time_max) {
        loop_time_max = elapsed_us;
    }
}

void metrics_record_audio_underrun(void) {
    audio_underrun_count++;
}

void metrics_record_ws_latency(uint32_t latency_us) {
    ws_samples[ws_sample_idx] = latency_us;
    ws_sample_idx = (ws_sample_idx + 1) % WS_SAMPLE_COUNT;
    if (ws_sample_count < WS_SAMPLE_COUNT) {
        ws_sample_count++;
    }
    if (latency_us > ws_latency_max) {
        ws_latency_max = latency_us;
    }
}

void metrics_record_mutex_wait(uint32_t wait_us) {
    if (wait_us > mutex_wait_max) {
        mutex_wait_max = wait_us;
    }
}

static uint32_t calc_average(uint32_t *samples, uint32_t count) {
    if (count == 0) return 0;
    uint64_t sum = 0;
    for (uint32_t i = 0; i < count; i++) {
        sum += samples[i];
    }
    return (uint32_t)(sum / count);
}

static uint32_t calc_jitter(uint32_t *samples, uint32_t count, uint32_t avg) {
    if (count < 2) return 0;
    uint64_t sum_sq_diff = 0;
    for (uint32_t i = 0; i < count; i++) {
        int32_t diff = (int32_t)samples[i] - (int32_t)avg;
        sum_sq_diff += (uint64_t)(diff * diff);
    }
    // Return standard deviation approximation
    uint32_t variance = (uint32_t)(sum_sq_diff / count);
    // Integer square root approximation
    uint32_t x = variance;
    uint32_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + variance / x) / 2;
    }
    return x;
}

void metrics_get_snapshot(metrics_snapshot_t *snapshot) {
    memset(snapshot, 0, sizeof(metrics_snapshot_t));

    // Heap metrics
    snapshot->heap_free = esp_get_free_heap_size();
    snapshot->heap_min_free = esp_get_minimum_free_heap_size();
    snapshot->heap_largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);

    // Loop timing
    snapshot->loop_time_avg = calc_average(loop_samples, loop_sample_count);
    snapshot->loop_time_max = loop_time_max;
    snapshot->loop_jitter = calc_jitter(loop_samples, loop_sample_count, snapshot->loop_time_avg);

    // Audio
    snapshot->audio_underruns = audio_underrun_count;

    // WebSocket
    snapshot->ws_frame_latency_avg = calc_average(ws_samples, ws_sample_count);
    snapshot->ws_frame_latency_max = ws_latency_max;

    // Stack high water marks
    TaskHandle_t main_task = xTaskGetCurrentTaskHandle();
    if (main_task) {
        snapshot->stack_main = uxTaskGetStackHighWaterMark(main_task) * sizeof(StackType_t);
    }
    if (engine_task_handle) {
        snapshot->stack_audio = uxTaskGetStackHighWaterMark(engine_task_handle) * sizeof(StackType_t);
    }

    // Mutex wait
    snapshot->mutex_wait_max = mutex_wait_max;

    // Timestamp
    snapshot->timestamp_ms = esp_timer_get_time() / 1000;
}

int metrics_to_json(char *buf, size_t buf_len) {
    metrics_snapshot_t snap;
    metrics_get_snapshot(&snap);

    return snprintf(buf, buf_len,
        "{"
        "\"heap\":{\"free\":%lu,\"min\":%lu,\"largest\":%lu},"
        "\"loop\":{\"avg_us\":%lu,\"max_us\":%lu,\"jitter_us\":%lu},"
        "\"audio\":{\"underruns\":%lu},"
        "\"ws\":{\"latency_avg_us\":%lu,\"latency_max_us\":%lu},"
        "\"stack\":{\"main\":%lu,\"audio\":%lu},"
        "\"mutex\":{\"wait_max_us\":%lu},"
        "\"ts\":%lld"
        "}",
        (unsigned long)snap.heap_free, (unsigned long)snap.heap_min_free,
        (unsigned long)snap.heap_largest_block,
        (unsigned long)snap.loop_time_avg, (unsigned long)snap.loop_time_max,
        (unsigned long)snap.loop_jitter,
        (unsigned long)snap.audio_underruns,
        (unsigned long)snap.ws_frame_latency_avg, (unsigned long)snap.ws_frame_latency_max,
        (unsigned long)snap.stack_main, (unsigned long)snap.stack_audio,
        (unsigned long)snap.mutex_wait_max,
        (long long)snap.timestamp_ms);
}

void metrics_log_summary(void) {
    metrics_snapshot_t snap;
    metrics_get_snapshot(&snap);

    ESP_LOGI(TAG, "PERF: heap=%lu/%lu loop=%luus(max=%lu) underruns=%lu ws=%luus mutex=%luus",
        (unsigned long)snap.heap_free, (unsigned long)snap.heap_min_free,
        (unsigned long)snap.loop_time_avg, (unsigned long)snap.loop_time_max,
        (unsigned long)snap.audio_underruns,
        (unsigned long)snap.ws_frame_latency_avg,
        (unsigned long)snap.mutex_wait_max);
}
