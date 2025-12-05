/**
 * @file rc_input.c
 * @brief RC receiver input capture implementation
 */

#include "rc_input.h"
#include "driver/mcpwm_cap.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "RC_INPUT";

// GPIO pins for each channel
static const int rc_gpio_pins[RC_CHANNEL_COUNT] = {
    PIN_RC_THROTTLE,    // Channel 0
    PIN_RC_STEERING,    // Channel 1
    PIN_RC_AUX1,        // Channel 2
    PIN_RC_AUX2,        // Channel 3
    PIN_RC_AUX3,        // Channel 4
    PIN_RC_AUX4         // Channel 5
};

// Channel names for logging
static const char *rc_channel_names[RC_CHANNEL_COUNT] = {
    "Throttle",
    "Steering",
    "Aux1",
    "Aux2",
    "Aux3",
    "Aux4"
};

// Capture channel handles
static mcpwm_cap_channel_handle_t cap_channels[RC_CHANNEL_COUNT] = {NULL};

// Raw channel data (updated by ISR)
static volatile rc_channel_raw_t channel_data[RC_CHANNEL_COUNT];

// Edge timestamps for pulse measurement
static volatile uint32_t rising_edge[RC_CHANNEL_COUNT] = {0};
static volatile bool got_rising[RC_CHANNEL_COUNT] = {false};

// Mutex for thread-safe access
static SemaphoreHandle_t data_mutex = NULL;

// ESP32 MCPWM capture runs at 80MHz
#define TICKS_PER_US    80

/**
 * @brief Capture callback - called on rising and falling edges
 */
static bool IRAM_ATTR capture_callback(mcpwm_cap_channel_handle_t cap_chan,
                                        const mcpwm_capture_event_data_t *edata,
                                        void *user_data)
{
    int channel = (int)(intptr_t)user_data;
    
    if (channel < 0 || channel >= RC_CHANNEL_COUNT) {
        return false;
    }
    
    if (edata->cap_edge == MCPWM_CAP_EDGE_POS) {
        // Rising edge - start of pulse
        rising_edge[channel] = edata->cap_value;
        got_rising[channel] = true;
    } else if (edata->cap_edge == MCPWM_CAP_EDGE_NEG && got_rising[channel]) {
        // Falling edge - end of pulse
        uint32_t pulse_ticks = edata->cap_value - rising_edge[channel];
        uint16_t pulse_us = pulse_ticks / TICKS_PER_US;
        
        // Validate pulse width
        if (pulse_us >= RC_VALID_MIN_US && pulse_us <= RC_VALID_MAX_US) {
            channel_data[channel].pulse_us = pulse_us;
            channel_data[channel].valid = true;
            channel_data[channel].last_update = (uint32_t)(esp_timer_get_time() / 1000);
        }
        
        got_rising[channel] = false;
    }
    
    return false;  // No high-priority task wakeup needed
}

esp_err_t rc_input_init(void)
{
    ESP_LOGI(TAG, "Initializing RC input capture...");
    
    // Create mutex
    data_mutex = xSemaphoreCreateMutex();
    if (data_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize channel data
    for (int i = 0; i < RC_CHANNEL_COUNT; i++) {
        channel_data[i].pulse_us = RC_DEFAULT_CENTER_US;
        channel_data[i].valid = false;
        channel_data[i].last_update = 0;
    }
    
    // ESP32 MCPWM has only 3 capture channels per group
    // Use group 0 for channels 0-2, group 1 for channels 3-5
    mcpwm_cap_timer_handle_t cap_timers[2] = {NULL, NULL};

    // Create capture timer for group 0 (channels 0-2: throttle, steering, aux1)
    mcpwm_capture_timer_config_t cap_timer_config_0 = {
        .clk_src = MCPWM_CAPTURE_CLK_SRC_DEFAULT,
        .group_id = 0,
        .resolution_hz = MCPWM_CAPTURE_RESOLUTION_HZ,
    };
    ESP_ERROR_CHECK(mcpwm_new_capture_timer(&cap_timer_config_0, &cap_timers[0]));

    // Create capture timer for group 1 (channels 3-5: aux2, aux3, aux4)
    mcpwm_capture_timer_config_t cap_timer_config_1 = {
        .clk_src = MCPWM_CAPTURE_CLK_SRC_DEFAULT,
        .group_id = 1,
        .resolution_hz = MCPWM_CAPTURE_RESOLUTION_HZ,
    };
    ESP_ERROR_CHECK(mcpwm_new_capture_timer(&cap_timer_config_1, &cap_timers[1]));

    // Create capture channel for each RC input
    for (int i = 0; i < RC_CHANNEL_COUNT; i++) {
        // Select which timer to use: channels 0-2 use group 0, channels 3-5 use group 1
        mcpwm_cap_timer_handle_t timer = (i < 3) ? cap_timers[0] : cap_timers[1];
        
        mcpwm_capture_channel_config_t cap_chan_config = {
            .gpio_num = rc_gpio_pins[i],
            .prescale = 1,
            .flags.neg_edge = true,
            .flags.pos_edge = true,
            .flags.pull_up = false,
            .flags.io_loop_back = false,
        };
        
        esp_err_t ret = mcpwm_new_capture_channel(timer, &cap_chan_config, &cap_channels[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create capture channel %d (%s): %s", 
                     i, rc_channel_names[i], esp_err_to_name(ret));
            return ret;
        }
        
        // Register callback with channel index as user data
        mcpwm_capture_event_callbacks_t callbacks = {
            .on_cap = capture_callback,
        };
        ESP_ERROR_CHECK(mcpwm_capture_channel_register_event_callbacks(
            cap_channels[i], &callbacks, (void*)(intptr_t)i));
        
        // Enable channel
        ESP_ERROR_CHECK(mcpwm_capture_channel_enable(cap_channels[i]));
        
        ESP_LOGI(TAG, "  Channel %d (%s) on GPIO %d (group %d)", 
                 i, rc_channel_names[i], rc_gpio_pins[i], (i < 3) ? 0 : 1);
    }
    
    // Enable and start both capture timers
    ESP_ERROR_CHECK(mcpwm_capture_timer_enable(cap_timers[0]));
    ESP_ERROR_CHECK(mcpwm_capture_timer_start(cap_timers[0]));
    ESP_ERROR_CHECK(mcpwm_capture_timer_enable(cap_timers[1]));
    ESP_ERROR_CHECK(mcpwm_capture_timer_start(cap_timers[1]));
    
    ESP_LOGI(TAG, "RC input capture initialized (%d channels across 2 MCPWM groups)", RC_CHANNEL_COUNT);
    return ESP_OK;
}

esp_err_t rc_input_get_raw(rc_channel_t channel, rc_channel_raw_t *raw)
{
    if (channel >= RC_CHANNEL_COUNT || raw == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    
    raw->pulse_us = channel_data[channel].pulse_us;
    raw->valid = channel_data[channel].valid;
    raw->last_update = channel_data[channel].last_update;
    
    // Check for signal timeout
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (raw->valid && (now - raw->last_update) > RC_SIGNAL_TIMEOUT_MS) {
        raw->valid = false;
    }
    
    xSemaphoreGive(data_mutex);
    return ESP_OK;
}

esp_err_t rc_input_get_all_raw(rc_channel_raw_t raw[RC_CHANNEL_COUNT])
{
    if (raw == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    for (int i = 0; i < RC_CHANNEL_COUNT; i++) {
        rc_input_get_raw((rc_channel_t)i, &raw[i]);
    }
    
    return ESP_OK;
}

esp_err_t rc_input_get_calibrated(rc_channel_t channel,
                                   const channel_calibration_t *calibration,
                                   rc_channel_data_t *data)
{
    if (channel >= RC_CHANNEL_COUNT || calibration == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    rc_channel_raw_t raw;
    rc_input_get_raw(channel, &raw);
    
    data->pulse_us = raw.pulse_us;
    data->valid = raw.valid;
    
    // Check for signal loss
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    data->signal_lost = !raw.valid || (now - raw.last_update) > RC_SIGNAL_TIMEOUT_MS;
    
    if (!data->valid || data->signal_lost) {
        data->value = 0;  // Return center on invalid/lost signal
        return ESP_OK;
    }
    
    // Apply calibration to convert pulse_us to -1000 to +1000 range
    int16_t pulse = raw.pulse_us;
    int16_t center = calibration->center;
    int16_t value;
    
    // Apply deadzone around center
    if (pulse >= (center - calibration->deadzone) && 
        pulse <= (center + calibration->deadzone)) {
        value = 0;
    } else if (pulse < center) {
        // Negative side (min to center)
        int16_t range = center - calibration->min;
        if (range > 0) {
            value = ((pulse - center) * 1000) / range;
        } else {
            value = 0;
        }
    } else {
        // Positive side (center to max)
        int16_t range = calibration->max - center;
        if (range > 0) {
            value = ((pulse - center) * 1000) / range;
        } else {
            value = 0;
        }
    }
    
    // Clamp to valid range
    if (value < -1000) value = -1000;
    if (value > 1000) value = 1000;
    
    // Apply reverse if needed
    if (calibration->reversed) {
        value = -value;
    }
    
    data->value = value;
    return ESP_OK;
}

bool rc_input_has_signal(void)
{
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    
    for (int i = 0; i < RC_CHANNEL_COUNT; i++) {
        if (channel_data[i].valid && 
            (now - channel_data[i].last_update) < RC_SIGNAL_TIMEOUT_MS) {
            return true;
        }
    }
    
    return false;
}

bool rc_input_channel_valid(rc_channel_t channel)
{
    if (channel >= RC_CHANNEL_COUNT) {
        return false;
    }
    
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    return channel_data[channel].valid && 
           (now - channel_data[channel].last_update) < RC_SIGNAL_TIMEOUT_MS;
}

uint32_t rc_input_signal_age_ms(void)
{
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t newest = 0;
    
    for (int i = 0; i < RC_CHANNEL_COUNT; i++) {
        if (channel_data[i].last_update > newest) {
            newest = channel_data[i].last_update;
        }
    }
    
    return (newest > 0) ? (now - newest) : UINT32_MAX;
}
