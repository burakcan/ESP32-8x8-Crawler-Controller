/**
 * @file mode_switch.h
 * @brief Steering mode switching via momentary button (Channel 3)
 *
 * Handles button press detection (single/double/triple) for mode switching:
 * - Single press: Toggle between Front Wheel Steering (FWS) and All Wheel Steering (AWS)
 * - Double press: Switch to Crab mode
 * - Triple press: Switch to Rear-only steering
 * - In Crab or Rear mode, single press returns to last FWS/AWS mode
 */

#ifndef MODE_SWITCH_H
#define MODE_SWITCH_H

#include "config.h"

/**
 * @brief Initialize the mode switch module
 *
 * Sets initial mode to STEER_MODE_FRONT
 */
void mode_switch_init(void);

/**
 * @brief Update mode switch state based on button input
 *
 * Call this every control loop iteration with the current button state.
 * Handles debouncing and press pattern detection.
 *
 * @param button_pressed true if the button is currently pressed (AUX2 > 400)
 */
void mode_switch_update(bool button_pressed);

/**
 * @brief Get the current steering mode
 *
 * @return Current steering mode
 */
steering_mode_t mode_switch_get_mode(void);

/**
 * @brief Set the steering mode directly (e.g., from web UI)
 *
 * This also updates the "last normal mode" if setting to FWS or AWS.
 *
 * @param mode Mode to set
 */
void mode_switch_set_mode(steering_mode_t mode);

/**
 * @brief Check if mode was changed since last call
 *
 * Resets the changed flag after reading.
 *
 * @return true if mode changed
 */
bool mode_switch_mode_changed(void);

#endif // MODE_SWITCH_H
