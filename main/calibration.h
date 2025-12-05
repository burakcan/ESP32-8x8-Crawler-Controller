/**
 * @file calibration.h
 * @brief RC calibration system interface
 * 
 * Provides calibration mode for learning transmitter ranges and storing to NVS.
 */

#ifndef CALIBRATION_H
#define CALIBRATION_H

#include "config.h"
#include "esp_err.h"

/**
 * @brief Calibration state machine states
 */
typedef enum {
    CAL_STATE_IDLE = 0,         // Not calibrating
    CAL_STATE_WAIT_CENTER,      // Waiting for sticks at center
    CAL_STATE_RECORD_CENTER,    // Recording center values
    CAL_STATE_WAIT_ENDPOINTS,   // Waiting for endpoint movement
    CAL_STATE_RECORD_ENDPOINTS, // Recording min/max values
    CAL_STATE_COMPLETE,         // Calibration complete
    CAL_STATE_FAILED,           // Calibration failed
} calibration_state_t;

/**
 * @brief Calibration status/result
 */
typedef struct {
    calibration_state_t state;
    uint8_t progress_percent;   // 0-100 progress indicator
    const char *status_message; // Human-readable status
    uint32_t time_remaining_ms; // Time remaining in current state (if applicable)
} calibration_status_t;

/**
 * @brief Initialize calibration system
 * @param data Pointer to calibration data structure (will be loaded from NVS or defaults)
 * @return ESP_OK on success
 */
esp_err_t calibration_init(calibration_data_t *data);

/**
 * @brief Start calibration mode
 * @return ESP_OK on success
 */
esp_err_t calibration_start(void);

/**
 * @brief Cancel ongoing calibration
 * @return ESP_OK on success
 */
esp_err_t calibration_cancel(void);

/**
 * @brief Update calibration state machine (call from main loop)
 * Should be called regularly during calibration mode.
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
 * @brief Clear calibration and reset to defaults
 * @return ESP_OK on success
 */
esp_err_t calibration_clear(void);

/**
 * @brief Check if calibration mode should be entered
 * This checks for a specific trigger condition (e.g., sticks in corners at boot)
 * @return true if calibration should be started
 */
bool calibration_check_trigger(void);

#endif // CALIBRATION_H
