/**
 * @file tuning.h
 * @brief Tuning configuration management for 8x8 crawler
 *
 * Handles servo endpoints, trim/subtrim, steering geometry,
 * and ESC settings with NVS persistence.
 */

#ifndef TUNING_H
#define TUNING_H

#include "config.h"
#include "esp_err.h"

/**
 * @brief Initialize tuning system
 * Loads configuration from NVS or sets defaults
 * @param config Pointer to store loaded config (can be NULL)
 * @return ESP_OK on success
 */
esp_err_t tuning_init(tuning_config_t *config);

/**
 * @brief Get current tuning configuration (read-only)
 * @return Pointer to current tuning config
 */
const tuning_config_t* tuning_get_config(void);

/**
 * @brief Update tuning configuration
 * @param config New configuration to apply
 * @return ESP_OK on success
 */
esp_err_t tuning_set_config(const tuning_config_t *config);

/**
 * @brief Save current tuning to NVS
 * @return ESP_OK on success
 */
esp_err_t tuning_save(void);

/**
 * @brief Reset tuning to factory defaults
 * @param save_to_nvs If true, also saves defaults to NVS
 * @return ESP_OK on success
 */
esp_err_t tuning_reset_defaults(bool save_to_nvs);

/**
 * @brief Set default tuning values
 * @param config Config struct to fill with defaults
 */
void tuning_get_defaults(tuning_config_t *config);

// ============================================================================
// Servo Output Calculation (applies tuning to raw values)
// ============================================================================

/**
 * @brief Calculate servo pulse with tuning applied
 * Applies endpoints, subtrim, trim, and reverse
 * @param servo_idx Servo index (0-3)
 * @param position Normalized position (-1000 to +1000)
 * @return Pulse width in microseconds
 */
uint16_t tuning_calc_servo_pulse(uint8_t servo_idx, int16_t position);

/**
 * @brief Calculate ESC pulse with tuning applied
 * Applies limits, subtrim, deadzone, and reverse
 * @param throttle Normalized throttle (-1000 to +1000)
 * @return Pulse width in microseconds
 */
uint16_t tuning_calc_esc_pulse(int16_t throttle);

/**
 * @brief Apply realistic throttle behavior (coasting, drag brake)
 * Called internally by tuning_calc_esc_pulse when realistic mode enabled
 * @param throttle_input Raw throttle input (-1000 to +1000)
 * @return Modified throttle value with realistic physics
 */
int16_t tuning_apply_realistic_throttle(int16_t throttle_input);

/**
 * @brief Reset realistic throttle state (e.g., on signal loss)
 */
void tuning_reset_realistic_throttle(void);

/**
 * @brief Get current simulated velocity
 * @return Current velocity (-1000 to +1000)
 */
int16_t tuning_get_simulated_velocity(void);

/**
 * @brief Apply steering expo curve
 * @param input Input value (-1000 to +1000)
 * @return Output value with expo applied
 */
int16_t tuning_apply_expo(int16_t input);

/**
 * @brief Get axle steering ratio for current mode
 * @param axle_idx Axle index (0-3)
 * @param mode Current steering mode
 * @return Ratio to apply (0-100)
 */
uint8_t tuning_get_axle_ratio(uint8_t axle_idx, steering_mode_t mode);

/**
 * @brief Apply speed-dependent steering reduction
 * Reduces steering at higher speeds for stability
 * Uses simulated velocity (current output) not throttle input
 * @param steering Input steering value (-1000 to +1000)
 * @return Reduced steering value
 */
int16_t tuning_apply_speed_steering(int16_t steering);

/**
 * @brief Throttle mode selection (3-position switch)
 */
typedef enum {
    THROTTLE_MODE_DIRECT = 0,   // Direct pass-through (low position)
    THROTTLE_MODE_NEUTRAL,      // Rev engine but no ESC output (center position)
    THROTTLE_MODE_REALISTIC     // Realistic throttle physics (high position)
} throttle_mode_t;

/**
 * @brief Set throttle mode from AUX switch
 * @param mode Throttle mode selection
 */
void tuning_set_throttle_mode(throttle_mode_t mode);

/**
 * @brief Check if in neutral mode (engine rev only, no ESC output)
 * @return true if neutral mode active
 */
bool tuning_is_neutral_mode(void);

/**
 * @brief Check if currently braking (throttle opposing movement direction)
 * @return true if braking
 */
bool tuning_is_braking(void);

/**
 * @brief Get the last movement direction
 * @return 1 for forward, -1 for reverse, 0 for stopped
 */
int8_t tuning_get_last_direction(void);

/**
 * @brief Check if motor is effectively stopped (below ESC cutoff threshold)
 * The ESC has a deadband where it stops outputting power even though
 * the simulated velocity hasn't reached zero yet.
 * @return true if simulated velocity is below motor cutoff threshold
 */
bool tuning_is_motor_stopped(void);

/**
 * @brief Get motor cutoff threshold
 * @return Motor cutoff value (0-1000)
 */
int16_t tuning_get_motor_cutoff(void);

#endif // TUNING_H

