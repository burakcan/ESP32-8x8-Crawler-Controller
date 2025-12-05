/**
 * @file rc_input.h
 * @brief RC receiver input capture interface
 * 
 * Captures PWM signals from RC receiver channels using MCPWM capture.
 */

#ifndef RC_INPUT_H
#define RC_INPUT_H

#include "config.h"
#include "esp_err.h"

/**
 * @brief Raw RC channel data (before calibration applied)
 */
typedef struct {
    uint16_t pulse_us;      // Raw pulse width in microseconds
    bool valid;             // Whether signal is valid (within bounds)
    uint32_t last_update;   // Timestamp of last valid reading (ms)
} rc_channel_raw_t;

/**
 * @brief Processed RC channel data (after calibration applied)
 */
typedef struct {
    int16_t value;          // Normalized value: -1000 to +1000 (0 = center)
    uint16_t pulse_us;      // Raw pulse width for reference
    bool valid;             // Whether signal is valid
    bool signal_lost;       // Whether signal has been lost (timeout)
} rc_channel_data_t;

/**
 * @brief Initialize RC input capture
 * @return ESP_OK on success
 */
esp_err_t rc_input_init(void);

/**
 * @brief Get raw pulse width for a channel (before calibration)
 * @param channel Channel index (0-3)
 * @param raw Pointer to raw data structure to fill
 * @return ESP_OK on success
 */
esp_err_t rc_input_get_raw(rc_channel_t channel, rc_channel_raw_t *raw);

/**
 * @brief Get all raw channel values
 * @param raw Array of RC_CHANNEL_COUNT raw data structures
 * @return ESP_OK on success
 */
esp_err_t rc_input_get_all_raw(rc_channel_raw_t raw[RC_CHANNEL_COUNT]);

/**
 * @brief Get processed/calibrated value for a channel
 * @param channel Channel index
 * @param calibration Calibration data to apply
 * @param data Pointer to processed data structure to fill
 * @return ESP_OK on success
 */
esp_err_t rc_input_get_calibrated(rc_channel_t channel, 
                                   const channel_calibration_t *calibration,
                                   rc_channel_data_t *data);

/**
 * @brief Check if any RC signal is being received
 * @return true if at least one channel has valid signal
 */
bool rc_input_has_signal(void);

/**
 * @brief Check if a specific channel has valid signal
 * @param channel Channel index
 * @return true if channel has valid signal
 */
bool rc_input_channel_valid(rc_channel_t channel);

/**
 * @brief Get time since last valid signal (any channel)
 * @return Time in milliseconds since last valid signal
 */
uint32_t rc_input_signal_age_ms(void);

#endif // RC_INPUT_H
