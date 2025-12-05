/**
 * @file led_rgb.h
 * @brief RGB LED driver for WS2812 LED on ESP32-S3
 *
 * Provides colorful status indication with smooth effects and transitions.
 */

#ifndef LED_RGB_H
#define LED_RGB_H

#include <stdint.h>
#include "esp_err.h"

// RGB color structure
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_color_t;

// Predefined colors
#define COLOR_OFF           (rgb_color_t){0, 0, 0}
#define COLOR_WHITE         (rgb_color_t){255, 255, 255}
#define COLOR_RED           (rgb_color_t){255, 0, 0}
#define COLOR_GREEN         (rgb_color_t){0, 255, 0}
#define COLOR_BLUE          (rgb_color_t){0, 0, 255}
#define COLOR_YELLOW        (rgb_color_t){255, 200, 0}
#define COLOR_ORANGE        (rgb_color_t){255, 80, 0}
#define COLOR_PURPLE        (rgb_color_t){128, 0, 255}
#define COLOR_CYAN          (rgb_color_t){0, 255, 255}
#define COLOR_MAGENTA       (rgb_color_t){255, 0, 255}
#define COLOR_PINK          (rgb_color_t){255, 100, 150}

// LED effect patterns
typedef enum {
    LED_EFFECT_OFF = 0,         // LED off
    LED_EFFECT_SOLID,           // Solid color
    LED_EFFECT_BREATHE,         // Smooth breathing effect
    LED_EFFECT_PULSE,           // Quick pulse effect
    LED_EFFECT_RAINBOW,         // Smooth rainbow cycle
    LED_EFFECT_BLINK,           // Simple on/off blink
    LED_EFFECT_FAST_BLINK,      // Fast blink for alerts
    LED_EFFECT_DOUBLE_BLINK,    // Double blink pattern
    LED_EFFECT_HEARTBEAT,       // Heartbeat pulse pattern
    LED_EFFECT_FADE_IN_OUT,     // Fade between two colors
    LED_EFFECT_SPARKLE,         // Random sparkle effect
    LED_EFFECT_FIRE,            // Fire flicker effect
    LED_EFFECT_BEACON,          // Beacon/rotating light effect
} led_effect_t;

// LED state for different system states
typedef enum {
    LED_STATE_BOOT,             // Booting up - rainbow
    LED_STATE_IDLE,             // Idle, waiting - slow breathe cyan
    LED_STATE_RUNNING,          // Normal operation - green heartbeat
    LED_STATE_FAILSAFE,         // No signal - fast red blink
    LED_STATE_CALIBRATING,      // Calibration mode - yellow breathe
    LED_STATE_OTA,              // OTA update - purple pulse
    LED_STATE_WIFI_CONNECTED,   // WiFi STA connected - blue flash
    LED_STATE_WIFI_ON,          // WiFi enabled - cyan double blink
    LED_STATE_WIFI_OFF,         // WiFi disabled - orange double blink
    LED_STATE_ERROR,            // Error state - red solid
} led_state_t;

/**
 * @brief Initialize the RGB LED driver
 * @return ESP_OK on success
 */
esp_err_t led_rgb_init(void);

/**
 * @brief Set LED to a specific effect with color
 * @param effect Effect pattern to use
 * @param color Primary color for the effect
 */
void led_rgb_set_effect(led_effect_t effect, rgb_color_t color);

/**
 * @brief Set LED to a predefined system state
 * @param state System state to display
 */
void led_rgb_set_state(led_state_t state);

/**
 * @brief Set LED to a solid color
 * @param color Color to display
 */
void led_rgb_set_color(rgb_color_t color);

/**
 * @brief Turn LED off
 */
void led_rgb_off(void);

/**
 * @brief Update LED animation (call from main loop at ~100Hz)
 * This updates the LED based on current effect/animation state
 */
void led_rgb_update(void);

/**
 * @brief Set LED brightness (0-100%)
 * @param brightness Brightness percentage
 */
void led_rgb_set_brightness(uint8_t brightness);

/**
 * @brief Create an RGB color from HSV values
 * @param h Hue (0-360)
 * @param s Saturation (0-100)
 * @param v Value/brightness (0-100)
 * @return RGB color
 */
rgb_color_t led_rgb_hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v);

#endif // LED_RGB_H
