/**
 * @file mode_switch.c
 * @brief Steering mode switching via momentary button (Channel 3)
 *
 * Button press detection for mode switching:
 * - Single press: Toggle between FWS and AWS (or return from Crab/Rear to last normal mode)
 * - Double press: Switch to Crab mode
 * - Triple press: Switch to Rear-only steering
 */

#include "mode_switch.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "sound.h"
#include "engine_sound.h"

static const char *TAG = "MODE_SW";

// Timing constants (in milliseconds)
#define DEBOUNCE_MS         50      // Minimum time between state changes
#define MULTI_PRESS_WINDOW  400     // Max time between presses for multi-press detection
#define PRESS_TIMEOUT       500     // Time after last release to commit the press count

// State machine
typedef enum {
    BTN_STATE_IDLE,         // Waiting for press
    BTN_STATE_PRESSED,      // Button is held down
    BTN_STATE_RELEASED,     // Button released, counting presses
    BTN_STATE_WAIT_COMMIT   // Waiting for timeout to commit action
} button_state_t;

// Module state
static button_state_t btn_state = BTN_STATE_IDLE;
static int64_t last_press_time = 0;
static int64_t last_release_time = 0;
static int press_count = 0;

static steering_mode_t current_mode = STEER_MODE_FRONT;
static steering_mode_t last_normal_mode = STEER_MODE_FRONT;  // FWS or AWS only
static bool mode_changed = false;

// Long-press callback support
static mode_switch_longpress_cb_t longpress_callback = NULL;
static uint32_t longpress_threshold_ms = 1500;
static bool longpress_handled = false;

// Steering enable flag (for menu mode)
static bool steering_enabled = true;

// Last known button state
static bool last_button_state = false;

/**
 * @brief Check if current mode is a "special" mode (Crab or Rear)
 */
static bool is_special_mode(steering_mode_t mode)
{
    return (mode == STEER_MODE_CRAB || mode == STEER_MODE_REAR);
}

/**
 * @brief Execute mode change based on press count
 */
static void execute_mode_change(int presses)
{
    steering_mode_t new_mode = current_mode;

    if (is_special_mode(current_mode)) {
        // In Crab or Rear mode: any single press returns to last normal mode
        if (presses == 1) {
            new_mode = last_normal_mode;
            ESP_LOGI(TAG, "Single press in special mode -> returning to %s",
                     new_mode == STEER_MODE_FRONT ? "Front" : "All-Axle");
        } else if (presses == 2) {
            // Double press in special mode: switch to Crab (or stay if already crab)
            new_mode = STEER_MODE_CRAB;
            ESP_LOGI(TAG, "Double press -> Crab mode");
        } else if (presses >= 3) {
            // Triple press in special mode: switch to Rear
            new_mode = STEER_MODE_REAR;
            ESP_LOGI(TAG, "Triple press -> Rear mode");
        }
    } else {
        // In normal mode (FWS or AWS)
        if (presses == 1) {
            // Toggle between FWS and AWS
            if (current_mode == STEER_MODE_FRONT) {
                new_mode = STEER_MODE_ALL_AXLE;
                ESP_LOGI(TAG, "Single press -> All-Axle mode");
            } else {
                new_mode = STEER_MODE_FRONT;
                ESP_LOGI(TAG, "Single press -> Front mode");
            }
        } else if (presses == 2) {
            new_mode = STEER_MODE_CRAB;
            ESP_LOGI(TAG, "Double press -> Crab mode");
        } else if (presses >= 3) {
            new_mode = STEER_MODE_REAR;
            ESP_LOGI(TAG, "Triple press -> Rear mode");
        }
    }

    // Update modes if changed
    if (new_mode != current_mode) {
        // Update last_normal_mode if we're switching to a normal mode
        if (!is_special_mode(new_mode)) {
            last_normal_mode = new_mode;
        }

        current_mode = new_mode;
        mode_changed = true;

        // Play sound feedback - air shift sound when engine running, beep when off
        if (engine_sound_get_state() == ENGINE_RUNNING) {
            engine_sound_play_mode_switch();
        } else {
            sound_play_mode_beep(new_mode);
        }
    }
}

void mode_switch_init(void)
{
    btn_state = BTN_STATE_IDLE;
    press_count = 0;
    current_mode = STEER_MODE_FRONT;
    last_normal_mode = STEER_MODE_FRONT;
    mode_changed = false;

    ESP_LOGI(TAG, "Mode switch initialized (Front steering)");
}

void mode_switch_update(bool button_pressed)
{
    int64_t now_ms = esp_timer_get_time() / 1000;

    // Track button state for external queries
    last_button_state = button_pressed;

    switch (btn_state) {
        case BTN_STATE_IDLE:
            if (button_pressed) {
                // Button just pressed
                btn_state = BTN_STATE_PRESSED;
                last_press_time = now_ms;
                press_count = 1;
                longpress_handled = false;  // Reset long-press flag
            }
            break;

        case BTN_STATE_PRESSED:
            if (button_pressed) {
                // Still held - check for long press
                if (!longpress_handled &&
                    longpress_callback != NULL &&
                    (now_ms - last_press_time) >= longpress_threshold_ms) {
                    // Long press detected!
                    longpress_handled = true;
                    ESP_LOGI(TAG, "Long press detected (%lu ms), firing callback",
                             (unsigned long)(now_ms - last_press_time));
                    // Fire callback
                    longpress_callback();
                    // Reset state machine - don't execute multi-click action
                    press_count = 0;
                    btn_state = BTN_STATE_IDLE;
                }
            } else {
                // Button just released (with debounce)
                if ((now_ms - last_press_time) >= DEBOUNCE_MS) {
                    btn_state = BTN_STATE_WAIT_COMMIT;
                    last_release_time = now_ms;
                }
            }
            break;

        case BTN_STATE_WAIT_COMMIT:
            if (button_pressed) {
                // Another press within the window
                if ((now_ms - last_release_time) <= MULTI_PRESS_WINDOW) {
                    press_count++;
                    btn_state = BTN_STATE_PRESSED;
                    last_press_time = now_ms;
                    longpress_handled = false;  // Reset for new press
                } else {
                    // Too late, this is a new sequence
                    // First commit the previous action (if enabled)
                    if (steering_enabled) {
                        execute_mode_change(press_count);
                    }
                    // Then start new sequence
                    press_count = 1;
                    btn_state = BTN_STATE_PRESSED;
                    last_press_time = now_ms;
                    longpress_handled = false;
                }
            } else {
                // Still released, check for timeout
                if ((now_ms - last_release_time) >= PRESS_TIMEOUT) {
                    // Timeout reached, commit the action (if enabled)
                    if (steering_enabled) {
                        execute_mode_change(press_count);
                    }
                    press_count = 0;
                    btn_state = BTN_STATE_IDLE;
                }
            }
            break;

        default:
            btn_state = BTN_STATE_IDLE;
            break;
    }
}

steering_mode_t mode_switch_get_mode(void)
{
    return current_mode;
}

void mode_switch_set_mode(steering_mode_t mode)
{
    if (mode >= STEER_MODE_COUNT) {
        return;
    }

    if (mode != current_mode) {
        // Update last_normal_mode if setting to a normal mode
        if (!is_special_mode(mode)) {
            last_normal_mode = mode;
        }

        current_mode = mode;
        mode_changed = true;

        ESP_LOGI(TAG, "Mode set externally to %d", mode);
    }
}

bool mode_switch_mode_changed(void)
{
    bool changed = mode_changed;
    mode_changed = false;
    return changed;
}

void mode_switch_set_longpress_callback(mode_switch_longpress_cb_t cb, uint32_t threshold_ms)
{
    longpress_callback = cb;
    longpress_threshold_ms = threshold_ms;
    ESP_LOGI(TAG, "Long-press callback %s (threshold: %lu ms)",
             cb ? "registered" : "cleared", (unsigned long)threshold_ms);
}

void mode_switch_set_enabled(bool enabled)
{
    if (steering_enabled != enabled) {
        steering_enabled = enabled;
        ESP_LOGI(TAG, "Steering mode changes %s", enabled ? "enabled" : "disabled");

        if (!enabled) {
            // Reset state machine when disabling
            btn_state = BTN_STATE_IDLE;
            press_count = 0;
            longpress_handled = false;
        }
    }
}

bool mode_switch_is_enabled(void)
{
    return steering_enabled;
}

bool mode_switch_get_button_pressed(void)
{
    return last_button_state;
}
