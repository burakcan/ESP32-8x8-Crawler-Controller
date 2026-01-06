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
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Callback type for long-press detection
 *
 * Called when button is held for the configured threshold time.
 * The callback should handle entering menu mode or other long-press actions.
 */
typedef void (*mode_switch_longpress_cb_t)(void);

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

/**
 * @brief Register a callback for long-press detection
 *
 * When the button is held for threshold_ms milliseconds, the callback
 * will be fired and the multi-click state machine will be reset.
 *
 * @param cb Callback function (NULL to disable)
 * @param threshold_ms Long-press threshold in milliseconds (e.g., 1500)
 */
void mode_switch_set_longpress_callback(mode_switch_longpress_cb_t cb, uint32_t threshold_ms);

/**
 * @brief Enable or disable steering mode changes
 *
 * When disabled, button presses are still tracked but mode changes
 * are suppressed. Use this when menu is active.
 *
 * @param enabled true to allow mode changes, false to suppress
 */
void mode_switch_set_enabled(bool enabled);

/**
 * @brief Check if steering mode changes are currently enabled
 *
 * @return true if mode changes are enabled
 */
bool mode_switch_is_enabled(void);

/**
 * @brief Get current button pressed state
 *
 * Returns the last known button state from the most recent update.
 *
 * @return true if button is currently pressed
 */
bool mode_switch_get_button_pressed(void);

#endif // MODE_SWITCH_H
