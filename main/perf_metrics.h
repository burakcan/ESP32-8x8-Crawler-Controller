/**
 * @file perf_metrics.h
 * @brief Performance metrics instrumentation for optimization tracking
 *
 * Captures heap, timing, audio, and stack metrics for before/after comparison.
 */

#ifndef PERF_METRICS_H
#define PERF_METRICS_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Performance metrics snapshot
 */
typedef struct {
    // Heap metrics
    uint32_t heap_free;           // Current free heap
    uint32_t heap_min;            // Minimum free heap since boot
    uint32_t heap_largest_block;  // Largest free block (fragmentation indicator)

    // Main loop timing (microseconds)
    uint32_t loop_avg_us;         // Average loop time
    uint32_t loop_max_us;         // Maximum loop time
    uint32_t loop_jitter_us;      // Max - Min (jitter)

    // Audio metrics
    uint32_t audio_underruns;     // Buffer underrun count

    // Stack high water marks (bytes remaining)
    uint32_t stack_main;          // Main task
    uint32_t stack_audio;         // Audio task
    uint32_t stack_web;           // Web server task

    // Sample count
    uint32_t samples;             // Number of samples collected
} perf_metrics_t;

/**
 * @brief Initialize performance metrics collection
 * @return ESP_OK on success
 */
esp_err_t perf_metrics_init(void);

/**
 * @brief Record main loop timing sample
 * Call at start and end of main loop iteration
 * @param start_us Timestamp from esp_timer_get_time() at loop start
 */
void perf_metrics_record_loop(int64_t start_us);

/**
 * @brief Record audio buffer underrun
 */
void perf_metrics_record_underrun(void);

/**
 * @brief Get current metrics snapshot
 * @param metrics Pointer to metrics structure to fill
 */
void perf_metrics_get(perf_metrics_t *metrics);

/**
 * @brief Reset all metrics (for new measurement period)
 */
void perf_metrics_reset(void);

/**
 * @brief Format metrics as JSON string
 * @param buf Output buffer
 * @param buf_size Buffer size
 * @return Number of bytes written (excluding null terminator)
 */
int perf_metrics_to_json(char *buf, size_t buf_size);

#endif // PERF_METRICS_H
