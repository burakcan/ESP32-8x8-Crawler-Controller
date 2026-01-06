/**
 * @file metrics.h
 * @brief Performance metrics collection for optimization analysis
 */

#ifndef METRICS_H
#define METRICS_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// Metric snapshot structure
typedef struct {
    // Heap metrics
    uint32_t heap_free;
    uint32_t heap_min_free;
    uint32_t heap_largest_block;

    // Timing metrics (microseconds)
    uint32_t loop_time_avg;
    uint32_t loop_time_max;
    uint32_t loop_jitter;

    // Audio metrics
    uint32_t audio_underruns;

    // WebSocket metrics
    uint32_t ws_frame_latency_avg;
    uint32_t ws_frame_latency_max;

    // Stack high water marks (bytes remaining)
    uint32_t stack_main;
    uint32_t stack_audio;
    uint32_t stack_websocket;

    // Mutex wait times (microseconds)
    uint32_t mutex_wait_max;

    // Timestamp
    int64_t timestamp_ms;
} metrics_snapshot_t;

/**
 * @brief Initialize metrics collection system
 */
esp_err_t metrics_init(void);

/**
 * @brief Record main loop iteration time
 * @param elapsed_us Elapsed time in microseconds
 */
void metrics_record_loop_time(uint32_t elapsed_us);

/**
 * @brief Record audio buffer underrun
 */
void metrics_record_audio_underrun(void);

/**
 * @brief Record WebSocket frame latency
 * @param latency_us Latency in microseconds
 */
void metrics_record_ws_latency(uint32_t latency_us);

/**
 * @brief Record mutex wait time
 * @param wait_us Wait time in microseconds
 */
void metrics_record_mutex_wait(uint32_t wait_us);

/**
 * @brief Get current metrics snapshot
 * @param snapshot Output buffer for snapshot
 */
void metrics_get_snapshot(metrics_snapshot_t *snapshot);

/**
 * @brief Get metrics as JSON string
 * @param buf Output buffer
 * @param buf_len Buffer length
 * @return Bytes written
 */
int metrics_to_json(char *buf, size_t buf_len);

/**
 * @brief Log metrics summary via UDP
 */
void metrics_log_summary(void);

#endif // METRICS_H
