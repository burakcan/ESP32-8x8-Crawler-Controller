/**
 * @file led_rgb.c
 * @brief RGB LED driver implementation using RMT for WS2812
 */

#include "led_rgb.h"
#include "config.h"
#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_random.h"
#include <string.h>

static const char *TAG = "LED_RGB";

// WS2812 timing (in RMT ticks at 10MHz resolution)
#define WS2812_T0H_TICKS    3   // 0.3us
#define WS2812_T0L_TICKS    9   // 0.9us
#define WS2812_T1H_TICKS    9   // 0.9us
#define WS2812_T1L_TICKS    3   // 0.3us
#define WS2812_RESET_TICKS  500 // 50us reset

// RMT resolution
#define RMT_RESOLUTION_HZ   10000000  // 10MHz = 0.1us resolution

// Animation timing (at 100Hz update rate)
#define BREATHE_PERIOD      200     // 2 seconds per breathe cycle
#define PULSE_PERIOD        30      // 0.3 second pulse
#define RAINBOW_PERIOD      300     // 3 seconds for full rainbow
#define BLINK_PERIOD        50      // 0.5 second blink
#define FAST_BLINK_PERIOD   10      // 0.1 second fast blink
#define HEARTBEAT_PERIOD    100     // 1 second heartbeat
#define BEACON_PERIOD       80      // 0.8 second per rotation

// RMT handles
static rmt_channel_handle_t led_channel = NULL;
static rmt_encoder_handle_t led_encoder = NULL;

// Current state
static led_effect_t current_effect = LED_EFFECT_OFF;
static rgb_color_t current_color = {0, 0, 0};
static rgb_color_t secondary_color = {0, 0, 0};
static uint8_t brightness = 50;  // Default 50% brightness
static uint32_t animation_tick = 0;
static bool initialized = false;

// WS2812 encoder
typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t *bytes_encoder;
    rmt_encoder_t *copy_encoder;
    int state;
    rmt_symbol_word_t reset_code;
} ws2812_encoder_t;

// Encoder callbacks
static size_t ws2812_encode(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                            const void *primary_data, size_t data_size, rmt_encode_state_t *ret_state)
{
    ws2812_encoder_t *ws2812_encoder = __containerof(encoder, ws2812_encoder_t, base);
    rmt_encoder_handle_t bytes_encoder = ws2812_encoder->bytes_encoder;
    rmt_encoder_handle_t copy_encoder = ws2812_encoder->copy_encoder;
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;

    switch (ws2812_encoder->state) {
        case 0: // Send RGB data
            encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, primary_data, data_size, &session_state);
            if (session_state & RMT_ENCODING_COMPLETE) {
                ws2812_encoder->state = 1;
            }
            if (session_state & RMT_ENCODING_MEM_FULL) {
                *ret_state = (rmt_encode_state_t)(session_state & (~RMT_ENCODING_COMPLETE));
                return encoded_symbols;
            }
            // Fall through
        case 1: // Send reset code
            encoded_symbols += copy_encoder->encode(copy_encoder, channel, &ws2812_encoder->reset_code,
                                                     sizeof(ws2812_encoder->reset_code), &session_state);
            if (session_state & RMT_ENCODING_COMPLETE) {
                ws2812_encoder->state = RMT_ENCODING_RESET;
                *ret_state = RMT_ENCODING_COMPLETE;
            }
            if (session_state & RMT_ENCODING_MEM_FULL) {
                *ret_state = (rmt_encode_state_t)(session_state & (~RMT_ENCODING_COMPLETE));
            }
            break;
    }
    return encoded_symbols;
}

static esp_err_t ws2812_encoder_reset(rmt_encoder_t *encoder)
{
    ws2812_encoder_t *ws2812_encoder = __containerof(encoder, ws2812_encoder_t, base);
    rmt_encoder_reset(ws2812_encoder->bytes_encoder);
    rmt_encoder_reset(ws2812_encoder->copy_encoder);
    ws2812_encoder->state = RMT_ENCODING_RESET;
    return ESP_OK;
}

static esp_err_t ws2812_encoder_del(rmt_encoder_t *encoder)
{
    ws2812_encoder_t *ws2812_encoder = __containerof(encoder, ws2812_encoder_t, base);
    rmt_del_encoder(ws2812_encoder->bytes_encoder);
    rmt_del_encoder(ws2812_encoder->copy_encoder);
    free(ws2812_encoder);
    return ESP_OK;
}

static esp_err_t create_ws2812_encoder(rmt_encoder_handle_t *ret_encoder)
{
    ws2812_encoder_t *ws2812_encoder = calloc(1, sizeof(ws2812_encoder_t));
    if (!ws2812_encoder) {
        return ESP_ERR_NO_MEM;
    }

    ws2812_encoder->base.encode = ws2812_encode;
    ws2812_encoder->base.reset = ws2812_encoder_reset;
    ws2812_encoder->base.del = ws2812_encoder_del;

    // Create bytes encoder for the LED data
    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = WS2812_T0H_TICKS,
            .level1 = 0,
            .duration1 = WS2812_T0L_TICKS,
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = WS2812_T1H_TICKS,
            .level1 = 0,
            .duration1 = WS2812_T1L_TICKS,
        },
        .flags.msb_first = true,
    };
    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&bytes_encoder_config, &ws2812_encoder->bytes_encoder));

    // Create copy encoder for the reset code
    rmt_copy_encoder_config_t copy_encoder_config = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy_encoder_config, &ws2812_encoder->copy_encoder));

    // Reset code
    ws2812_encoder->reset_code = (rmt_symbol_word_t){
        .level0 = 0,
        .duration0 = WS2812_RESET_TICKS,
        .level1 = 0,
        .duration1 = WS2812_RESET_TICKS,
    };

    *ret_encoder = &ws2812_encoder->base;
    return ESP_OK;
}

// Send color to LED
static void send_color(rgb_color_t color)
{
    if (!initialized) return;

    // Apply brightness
    uint8_t r = (color.r * brightness) / 100;
    uint8_t g = (color.g * brightness) / 100;
    uint8_t b = (color.b * brightness) / 100;

    // WS2812 expects GRB order
    uint8_t grb[3] = {g, r, b};

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };
    rmt_transmit(led_channel, led_encoder, grb, sizeof(grb), &tx_config);
    rmt_tx_wait_all_done(led_channel, portMAX_DELAY);
}

// HSV to RGB conversion
rgb_color_t led_rgb_hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v)
{
    rgb_color_t rgb;
    uint8_t region, remainder, p, q, t;

    if (s == 0) {
        rgb.r = rgb.g = rgb.b = (v * 255) / 100;
        return rgb;
    }

    h = h % 360;
    region = h / 60;
    remainder = (h - (region * 60)) * 6;

    uint16_t val = (v * 255) / 100;
    uint16_t sat = (s * 255) / 100;

    p = (val * (255 - sat)) >> 8;
    q = (val * (255 - ((sat * remainder) >> 8))) >> 8;
    t = (val * (255 - ((sat * (255 - remainder)) >> 8))) >> 8;

    switch (region) {
        case 0:  rgb.r = val; rgb.g = t;   rgb.b = p;   break;
        case 1:  rgb.r = q;   rgb.g = val; rgb.b = p;   break;
        case 2:  rgb.r = p;   rgb.g = val; rgb.b = t;   break;
        case 3:  rgb.r = p;   rgb.g = q;   rgb.b = val; break;
        case 4:  rgb.r = t;   rgb.g = p;   rgb.b = val; break;
        default: rgb.r = val; rgb.g = p;   rgb.b = q;   break;
    }

    return rgb;
}

// Interpolate between two colors
static rgb_color_t interpolate_color(rgb_color_t c1, rgb_color_t c2, uint8_t factor)
{
    rgb_color_t result;
    result.r = c1.r + ((int16_t)(c2.r - c1.r) * factor) / 255;
    result.g = c1.g + ((int16_t)(c2.g - c1.g) * factor) / 255;
    result.b = c1.b + ((int16_t)(c2.b - c1.b) * factor) / 255;
    return result;
}

// Calculate sine wave (0-255 output for 0-period input)
static uint8_t sine_wave(uint32_t tick, uint32_t period)
{
    // Simple sine approximation using parabola
    uint32_t phase = (tick % period) * 512 / period;  // 0-511
    int32_t val;

    if (phase < 256) {
        val = phase;
    } else {
        val = 511 - phase;
    }

    // Quadratic approximation of sine
    val = (val * val) / 256;
    return (uint8_t)val;
}

// Breathe effect (smooth sine wave)
static rgb_color_t effect_breathe(rgb_color_t base, uint32_t tick)
{
    uint8_t intensity = sine_wave(tick, BREATHE_PERIOD);
    // Map to 10-100% brightness range
    intensity = 25 + (intensity * 230) / 255;

    rgb_color_t result;
    result.r = (base.r * intensity) / 255;
    result.g = (base.g * intensity) / 255;
    result.b = (base.b * intensity) / 255;
    return result;
}

// Pulse effect (quick bright flash)
static rgb_color_t effect_pulse(rgb_color_t base, uint32_t tick)
{
    uint32_t phase = tick % PULSE_PERIOD;
    uint8_t intensity;

    if (phase < PULSE_PERIOD / 3) {
        // Quick rise
        intensity = (phase * 255) / (PULSE_PERIOD / 3);
    } else {
        // Slow fall
        intensity = 255 - ((phase - PULSE_PERIOD / 3) * 255) / (PULSE_PERIOD * 2 / 3);
    }

    rgb_color_t result;
    result.r = (base.r * intensity) / 255;
    result.g = (base.g * intensity) / 255;
    result.b = (base.b * intensity) / 255;
    return result;
}

// Rainbow effect
static rgb_color_t effect_rainbow(uint32_t tick)
{
    uint16_t hue = (tick * 360) / RAINBOW_PERIOD;
    return led_rgb_hsv_to_rgb(hue % 360, 100, 100);
}

// Blink effect
static rgb_color_t effect_blink(rgb_color_t base, uint32_t tick, uint32_t period)
{
    if ((tick % period) < (period / 2)) {
        return base;
    }
    return COLOR_OFF;
}

// Double blink effect
static rgb_color_t effect_double_blink(rgb_color_t base, uint32_t tick)
{
    uint32_t phase = tick % 100;  // 1 second cycle

    // Two quick blinks in first half
    if (phase < 10 || (phase >= 20 && phase < 30)) {
        return base;
    }
    return COLOR_OFF;
}

// Heartbeat effect
static rgb_color_t effect_heartbeat(rgb_color_t base, uint32_t tick)
{
    uint32_t phase = tick % HEARTBEAT_PERIOD;
    uint8_t intensity = 0;

    // First beat (quick pulse)
    if (phase < 10) {
        intensity = (phase * 255) / 10;
    } else if (phase < 20) {
        intensity = 255 - ((phase - 10) * 255) / 10;
    }
    // Second beat (smaller)
    else if (phase >= 25 && phase < 32) {
        intensity = ((phase - 25) * 180) / 7;
    } else if (phase >= 32 && phase < 40) {
        intensity = 180 - ((phase - 32) * 180) / 8;
    }

    rgb_color_t result;
    result.r = (base.r * intensity) / 255;
    result.g = (base.g * intensity) / 255;
    result.b = (base.b * intensity) / 255;
    return result;
}

// Sparkle effect
static rgb_color_t effect_sparkle(rgb_color_t base, uint32_t tick)
{
    // Random brightness variations
    uint8_t rnd = esp_random() & 0xFF;
    uint8_t intensity = 100 + (rnd * 155) / 255;

    // Occasional bright flash
    if ((esp_random() & 0x1F) == 0) {
        intensity = 255;
    }

    rgb_color_t result;
    result.r = (base.r * intensity) / 255;
    result.g = (base.g * intensity) / 255;
    result.b = (base.b * intensity) / 255;
    return result;
}

// Fire flicker effect
static rgb_color_t effect_fire(uint32_t tick)
{
    // Random orange/red/yellow
    uint8_t r = 255;
    uint8_t g = 50 + (esp_random() % 100);
    uint8_t b = (esp_random() % 30);

    // Intensity flicker
    uint8_t intensity = 150 + (esp_random() % 105);

    rgb_color_t result;
    result.r = (r * intensity) / 255;
    result.g = (g * intensity) / 255;
    result.b = (b * intensity) / 255;
    return result;
}

// Beacon/rotating light effect - simulates rotating warning beacon
static rgb_color_t effect_beacon(rgb_color_t base, uint32_t tick)
{
    uint32_t phase = tick % BEACON_PERIOD;
    uint8_t intensity = 0;

    // The beacon has a bright peak that sweeps through
    // Peak with quick falloff to simulate rotating light
    uint32_t peak_width = BEACON_PERIOD / 4;

    if (phase < peak_width) {
        // Rising edge
        intensity = (phase * 255) / peak_width;
    } else if (phase < peak_width * 2) {
        // Falling edge
        intensity = 255 - ((phase - peak_width) * 255) / peak_width;
    } else {
        // Dark period - dim glow
        intensity = 8;
    }

    rgb_color_t result;
    result.r = (base.r * intensity) / 255;
    result.g = (base.g * intensity) / 255;
    result.b = (base.b * intensity) / 255;
    return result;
}

// ============================================================================
// Public API
// ============================================================================

esp_err_t led_rgb_init(void)
{
    ESP_LOGI(TAG, "Initializing RGB LED on GPIO %d", PIN_STATUS_LED);

    // Create RMT TX channel
    rmt_tx_channel_config_t tx_config = {
        .gpio_num = PIN_STATUS_LED,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_config, &led_channel));

    // Create WS2812 encoder
    ESP_ERROR_CHECK(create_ws2812_encoder(&led_encoder));

    // Enable channel
    ESP_ERROR_CHECK(rmt_enable(led_channel));

    initialized = true;

    // Start with rainbow boot animation
    led_rgb_set_state(LED_STATE_BOOT);

    ESP_LOGI(TAG, "RGB LED initialized");
    return ESP_OK;
}

void led_rgb_set_effect(led_effect_t effect, rgb_color_t color)
{
    current_effect = effect;
    current_color = color;
}

void led_rgb_set_state(led_state_t state)
{
    switch (state) {
        case LED_STATE_BOOT:
            current_effect = LED_EFFECT_RAINBOW;
            current_color = COLOR_WHITE;
            break;

        case LED_STATE_IDLE:
            current_effect = LED_EFFECT_BREATHE;
            current_color = COLOR_CYAN;
            break;

        case LED_STATE_RUNNING:
            current_effect = LED_EFFECT_BEACON;
            current_color = COLOR_ORANGE;  // Orange/amber like real beacons
            break;

        case LED_STATE_FAILSAFE:
            current_effect = LED_EFFECT_FAST_BLINK;
            current_color = COLOR_RED;
            break;

        case LED_STATE_CALIBRATING:
            current_effect = LED_EFFECT_BREATHE;
            current_color = COLOR_YELLOW;
            break;

        case LED_STATE_OTA:
            current_effect = LED_EFFECT_PULSE;
            current_color = COLOR_PURPLE;
            break;

        case LED_STATE_WIFI_CONNECTED:
            current_effect = LED_EFFECT_DOUBLE_BLINK;
            current_color = COLOR_BLUE;
            break;

        case LED_STATE_WIFI_ON:
            current_effect = LED_EFFECT_DOUBLE_BLINK;
            current_color = COLOR_CYAN;
            break;

        case LED_STATE_WIFI_OFF:
            current_effect = LED_EFFECT_DOUBLE_BLINK;
            current_color = COLOR_ORANGE;
            break;

        case LED_STATE_ERROR:
            current_effect = LED_EFFECT_SOLID;
            current_color = COLOR_RED;
            break;

        default:
            current_effect = LED_EFFECT_OFF;
            break;
    }
}

void led_rgb_set_color(rgb_color_t color)
{
    current_effect = LED_EFFECT_SOLID;
    current_color = color;
    send_color(color);
}

void led_rgb_off(void)
{
    current_effect = LED_EFFECT_OFF;
    send_color(COLOR_OFF);
}

void led_rgb_update(void)
{
    if (!initialized) return;

    animation_tick++;

    rgb_color_t output_color = COLOR_OFF;

    switch (current_effect) {
        case LED_EFFECT_OFF:
            output_color = COLOR_OFF;
            break;

        case LED_EFFECT_SOLID:
            output_color = current_color;
            break;

        case LED_EFFECT_BEACON:
            output_color = effect_beacon(current_color, animation_tick);
            break;

        case LED_EFFECT_BREATHE:
            output_color = effect_breathe(current_color, animation_tick);
            break;

        case LED_EFFECT_PULSE:
            output_color = effect_pulse(current_color, animation_tick);
            break;

        case LED_EFFECT_RAINBOW:
            output_color = effect_rainbow(animation_tick);
            break;

        case LED_EFFECT_BLINK:
            output_color = effect_blink(current_color, animation_tick, BLINK_PERIOD);
            break;

        case LED_EFFECT_FAST_BLINK:
            output_color = effect_blink(current_color, animation_tick, FAST_BLINK_PERIOD);
            break;

        case LED_EFFECT_DOUBLE_BLINK:
            output_color = effect_double_blink(current_color, animation_tick);
            break;

        case LED_EFFECT_HEARTBEAT:
            output_color = effect_heartbeat(current_color, animation_tick);
            break;

        case LED_EFFECT_FADE_IN_OUT:
            {
                uint8_t factor = sine_wave(animation_tick, BREATHE_PERIOD);
                output_color = interpolate_color(current_color, secondary_color, factor);
            }
            break;

        case LED_EFFECT_SPARKLE:
            output_color = effect_sparkle(current_color, animation_tick);
            break;

        case LED_EFFECT_FIRE:
            output_color = effect_fire(animation_tick);
            break;
    }

    send_color(output_color);
}

void led_rgb_set_brightness(uint8_t new_brightness)
{
    if (new_brightness > 100) new_brightness = 100;
    brightness = new_brightness;
}
