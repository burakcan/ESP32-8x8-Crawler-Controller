/**
 * @file pwm_output.h
 * @brief PWM output interface for ESC and servos
 */

#ifndef PWM_OUTPUT_H
#define PWM_OUTPUT_H

#include "config.h"
#include "esp_err.h"

// Servo indices (one servo per axle)
typedef enum {
    SERVO_AXLE_1 = 0,   // Front axle
    SERVO_AXLE_2,       // Second axle
    SERVO_AXLE_3,       // Third axle
    SERVO_AXLE_4,       // Rear axle
    SERVO_COUNT
} servo_id_t;

/**
 * @brief Initialize all PWM outputs (ESC and servos)
 * @return ESP_OK on success
 */
esp_err_t pwm_output_init(void);

// ============================================================================
// ESC Control
// ============================================================================

/**
 * @brief Set ESC pulse width directly
 * @param pulse_us Pulse width in microseconds (1000-2000)
 * @return ESP_OK on success
 */
esp_err_t esc_set_pulse(uint16_t pulse_us);

/**
 * @brief Set ESC throttle from normalized value
 * @param throttle Throttle value: -1000 to +1000 (0 = neutral)
 * @return ESP_OK on success
 */
esp_err_t esc_set_throttle(int16_t throttle);

/**
 * @brief Set ESC to neutral/failsafe position
 * @return ESP_OK on success
 */
esp_err_t esc_set_neutral(void);

/**
 * @brief Get current ESC pulse width
 * @return Current pulse width in microseconds
 */
uint16_t esc_get_pulse(void);

// ============================================================================
// Servo Control
// ============================================================================

/**
 * @brief Set servo pulse width directly
 * @param servo Servo ID
 * @param pulse_us Pulse width in microseconds (500-2500 typical)
 * @return ESP_OK on success
 */
esp_err_t servo_set_pulse(servo_id_t servo, uint16_t pulse_us);

/**
 * @brief Set servo position from normalized value
 * @param servo Servo ID
 * @param position Position: -1000 to +1000 (0 = center)
 * @return ESP_OK on success
 */
esp_err_t servo_set_position(servo_id_t servo, int16_t position);

/**
 * @brief Set all servos to center position
 * @return ESP_OK on success
 */
esp_err_t servo_center_all(void);

/**
 * @brief Set all servos at once (for synchronized movement)
 * @param positions Array of SERVO_COUNT positions (-1000 to +1000)
 * @return ESP_OK on success
 */
esp_err_t servo_set_all(const int16_t positions[SERVO_COUNT]);

/**
 * @brief Get current servo pulse width
 * @param servo Servo ID
 * @return Current pulse width in microseconds
 */
uint16_t servo_get_pulse(servo_id_t servo);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Convert normalized value to pulse width
 * @param value Normalized value: -1000 to +1000
 * @param min_us Minimum pulse width
 * @param center_us Center pulse width
 * @param max_us Maximum pulse width
 * @return Pulse width in microseconds
 */
uint16_t value_to_pulse(int16_t value, uint16_t min_us, uint16_t center_us, uint16_t max_us);

/**
 * @brief Convert pulse width to normalized value
 * @param pulse_us Pulse width in microseconds
 * @param min_us Minimum pulse width
 * @param center_us Center pulse width
 * @param max_us Maximum pulse width
 * @return Normalized value: -1000 to +1000
 */
int16_t pulse_to_value(uint16_t pulse_us, uint16_t min_us, uint16_t center_us, uint16_t max_us);

#endif // PWM_OUTPUT_H
