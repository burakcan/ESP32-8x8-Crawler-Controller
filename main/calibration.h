/**
 * @file calibration.h
 * @brief RC calibration system interface
 *
 * Provides manual per-channel calibration with step-by-step user control.
 */

#ifndef CALIBRATION_H
#define CALIBRATION_H

#include "config.h"
#include "esp_err.h"

/**
 * @brief Calibration step states (manual per-channel)
 */
typedef enum {
    CAL_STEP_IDLE = 0,      // Not calibrating
    CAL_STEP_CENTER,        // Waiting for user to center stick and confirm
    CAL_STEP_MIN,           // Waiting for user to move to min and confirm
    CAL_STEP_MAX,           // Waiting for user to move to max and confirm
    CAL_STEP_COMPLETE,      // Channel calibration complete
} calibration_step_t;

/**
 * @brief Calibration status
 */
typedef struct {
    calibration_step_t step;    // Current step
    int8_t channel;             // Channel being calibrated (-1 if none)
    uint16_t current_pulse;     // Current raw pulse reading for display
    uint16_t recorded_center;   // Recorded center value (if past that step)
    uint16_t recorded_min;      // Recorded min value (if past that step)
    uint16_t recorded_max;      // Recorded max value (if past that step)
    const char *message;        // Human-readable status message
} calibration_status_t;

/**
 * @brief Initialize calibration system
 * @param data Pointer to calibration data structure (will be loaded from NVS or defaults)
 * @return ESP_OK on success
 */
esp_err_t calibration_init(calibration_data_t *data);

/**
 * @brief Start calibrating a specific channel
 * @param channel Channel index (0-5)
 * @return ESP_OK on success
 */
esp_err_t calibration_start_channel(rc_channel_t channel);

/**
 * @brief Confirm current step and move to next
 * Records the current value and advances to next step.
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not calibrating
 */
esp_err_t calibration_confirm_step(void);

/**
 * @brief Cancel ongoing calibration
 * @return ESP_OK on success
 */
esp_err_t calibration_cancel(void);

/**
 * @brief Update calibration (call from main loop to update current_pulse)
 * @return ESP_OK on success
 */
esp_err_t calibration_update(void);

/**
 * @brief Get current calibration status
 * @param status Pointer to status structure to fill
 * @return ESP_OK on success
 */
esp_err_t calibration_get_status(calibration_status_t *status);

/**
 * @brief Check if calibration is currently in progress
 * @return true if calibrating
 */
bool calibration_in_progress(void);

/**
 * @brief Check if system has valid calibration
 * @return true if valid calibration exists
 */
bool calibration_is_valid(void);

/**
 * @brief Get current calibration data
 * @return Pointer to calibration data (read-only)
 */
const calibration_data_t* calibration_get_data(void);

/**
 * @brief Set reversed flag for a channel
 * @param channel Channel index
 * @param reversed true to reverse
 * @return ESP_OK on success
 */
esp_err_t calibration_set_reversed(rc_channel_t channel, bool reversed);

/**
 * @brief Clear calibration for a channel and reset to defaults
 * @param channel Channel index
 * @return ESP_OK on success
 */
esp_err_t calibration_clear_channel(rc_channel_t channel);

/**
 * @brief Clear all calibration and reset to defaults
 * @return ESP_OK on success
 */
esp_err_t calibration_clear(void);

/**
 * @brief Check if calibration mode should be entered (stick trigger at boot)
 * @return true if calibration should be started
 */
bool calibration_check_trigger(void);

#endif // CALIBRATION_H
