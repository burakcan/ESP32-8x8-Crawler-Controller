/**
 * @file calibration.c
 * @brief RC calibration system implementation
 *
 * Manual per-channel calibration with step-by-step user control.
 */

#include "calibration.h"
#include "rc_input.h"
#include "nvs_storage.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "CALIBRATION";

// Channel names for logging
static const char *channel_names[RC_CHANNEL_COUNT] = {
    "Throttle", "Steering", "Aux1", "Aux2", "Aux3", "Aux4"
};

// Current calibration data
static calibration_data_t cal_data;

// Calibration state
static calibration_step_t cal_step = CAL_STEP_IDLE;
static int8_t cal_channel = -1;
static const char *cal_message = "Not calibrating";

// Recorded values during calibration
static uint16_t cal_current_pulse = 1500;
static uint16_t cal_recorded_center = 1500;
static uint16_t cal_recorded_min = 1000;
static uint16_t cal_recorded_max = 2000;

esp_err_t calibration_init(calibration_data_t *data)
{
    ESP_LOGI(TAG, "Initializing calibration system...");

    // Try to load from NVS
    esp_err_t ret = nvs_load_calibration(&cal_data);

    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "No saved calibration, using defaults");
        nvs_get_default_calibration(&cal_data);
    } else {
        ESP_LOGI(TAG, "Loaded calibration from NVS");

        // Log loaded values
        for (int i = 0; i < RC_CHANNEL_COUNT; i++) {
            ESP_LOGI(TAG, "  %s: min=%d center=%d max=%d deadzone=%d reversed=%d",
                     channel_names[i],
                     cal_data.channels[i].min,
                     cal_data.channels[i].center,
                     cal_data.channels[i].max,
                     cal_data.channels[i].deadzone,
                     cal_data.channels[i].reversed);
        }
    }

    // Copy to output if provided
    if (data != NULL) {
        memcpy(data, &cal_data, sizeof(calibration_data_t));
    }

    return ESP_OK;
}

esp_err_t calibration_start_channel(rc_channel_t channel)
{
    if (channel >= RC_CHANNEL_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    if (cal_step != CAL_STEP_IDLE && cal_step != CAL_STEP_COMPLETE) {
        ESP_LOGW(TAG, "Calibration already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting calibration for %s", channel_names[channel]);

    cal_channel = channel;
    cal_step = CAL_STEP_CENTER;
    cal_message = "Center the stick, then press Next";
    cal_recorded_center = 1500;
    cal_recorded_min = 1000;
    cal_recorded_max = 2000;

    return ESP_OK;
}

esp_err_t calibration_confirm_step(void)
{
    if (cal_step == CAL_STEP_IDLE || cal_channel < 0) {
        return ESP_ERR_INVALID_STATE;
    }

    switch (cal_step) {
        case CAL_STEP_CENTER:
            // Record center value
            cal_recorded_center = cal_current_pulse;
            ESP_LOGI(TAG, "  %s center recorded: %d us",
                     channel_names[cal_channel], cal_recorded_center);

            cal_step = CAL_STEP_MIN;
            cal_message = "Move to MIN position, then press Next";
            break;

        case CAL_STEP_MIN:
            // Record min value
            cal_recorded_min = cal_current_pulse;
            ESP_LOGI(TAG, "  %s min recorded: %d us",
                     channel_names[cal_channel], cal_recorded_min);

            cal_step = CAL_STEP_MAX;
            cal_message = "Move to MAX position, then press Next";
            break;

        case CAL_STEP_MAX:
            // Record max value
            cal_recorded_max = cal_current_pulse;
            ESP_LOGI(TAG, "  %s max recorded: %d us",
                     channel_names[cal_channel], cal_recorded_max);

            // Validate and save
            if (cal_recorded_min >= cal_recorded_max) {
                // Swap if reversed
                uint16_t tmp = cal_recorded_min;
                cal_recorded_min = cal_recorded_max;
                cal_recorded_max = tmp;
            }

            // Apply to calibration data
            cal_data.channels[cal_channel].center = cal_recorded_center;
            cal_data.channels[cal_channel].min = cal_recorded_min;
            cal_data.channels[cal_channel].max = cal_recorded_max;
            cal_data.channels[cal_channel].deadzone = DEFAULT_DEADZONE_US;

            // Mark as calibrated
            cal_data.calibrated = true;
            cal_data.magic = CALIBRATION_MAGIC;
            cal_data.version = CALIBRATION_VERSION;

            // Save to NVS
            esp_err_t ret = nvs_save_calibration(&cal_data);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Calibration saved: %s = %d / %d / %d",
                         channel_names[cal_channel],
                         cal_recorded_min, cal_recorded_center, cal_recorded_max);
                cal_step = CAL_STEP_COMPLETE;
                cal_message = "Calibration complete!";
            } else {
                ESP_LOGE(TAG, "Failed to save calibration!");
                cal_step = CAL_STEP_IDLE;
                cal_message = "Failed to save";
                return ESP_FAIL;
            }
            break;

        case CAL_STEP_COMPLETE:
            // Reset to idle
            cal_step = CAL_STEP_IDLE;
            cal_channel = -1;
            cal_message = "Not calibrating";
            break;

        default:
            break;
    }

    return ESP_OK;
}

esp_err_t calibration_cancel(void)
{
    if (cal_step == CAL_STEP_IDLE) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Calibration cancelled");
    cal_step = CAL_STEP_IDLE;
    cal_channel = -1;
    cal_message = "Calibration cancelled";

    return ESP_OK;
}

esp_err_t calibration_update(void)
{
    // Update current pulse reading for the channel being calibrated
    if (cal_channel >= 0 && cal_channel < RC_CHANNEL_COUNT) {
        rc_channel_raw_t raw;
        rc_input_get_raw((rc_channel_t)cal_channel, &raw);
        if (raw.valid) {
            cal_current_pulse = raw.pulse_us;
        }
    }

    return ESP_OK;
}

esp_err_t calibration_get_status(calibration_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    status->step = cal_step;
    status->channel = cal_channel;
    status->current_pulse = cal_current_pulse;
    status->recorded_center = cal_recorded_center;
    status->recorded_min = cal_recorded_min;
    status->recorded_max = cal_recorded_max;
    status->message = cal_message;

    return ESP_OK;
}

bool calibration_in_progress(void)
{
    return (cal_step != CAL_STEP_IDLE && cal_step != CAL_STEP_COMPLETE);
}

bool calibration_is_valid(void)
{
    return cal_data.calibrated && cal_data.magic == CALIBRATION_MAGIC;
}

const calibration_data_t* calibration_get_data(void)
{
    return &cal_data;
}

esp_err_t calibration_set_reversed(rc_channel_t channel, bool reversed)
{
    if (channel >= RC_CHANNEL_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    cal_data.channels[channel].reversed = reversed;

    // Save to NVS
    esp_err_t ret = nvs_save_calibration(&cal_data);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "%s reversed = %d", channel_names[channel], reversed);
    }

    return ret;
}

esp_err_t calibration_clear_channel(rc_channel_t channel)
{
    if (channel >= RC_CHANNEL_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Clearing calibration for %s", channel_names[channel]);

    // Reset to defaults
    cal_data.channels[channel].min = RC_DEFAULT_MIN_US;
    cal_data.channels[channel].center = RC_DEFAULT_CENTER_US;
    cal_data.channels[channel].max = RC_DEFAULT_MAX_US;
    cal_data.channels[channel].deadzone = DEFAULT_DEADZONE_US;
    cal_data.channels[channel].reversed = false;

    return nvs_save_calibration(&cal_data);
}

esp_err_t calibration_clear(void)
{
    ESP_LOGI(TAG, "Clearing all calibration...");

    nvs_clear_calibration();
    nvs_get_default_calibration(&cal_data);
    cal_step = CAL_STEP_IDLE;
    cal_channel = -1;

    return ESP_OK;
}

bool calibration_check_trigger(void)
{
    // Trigger calibration if throttle is at max and steering is at max
    // (both sticks pushed to top-right corner at boot)

    rc_channel_raw_t raw[RC_CHANNEL_COUNT];
    rc_input_get_all_raw(raw);

    // Check if throttle and steering are both valid and at extremes
    if (raw[RC_CH_THROTTLE].valid && raw[RC_CH_STEERING].valid) {
        bool throttle_high = raw[RC_CH_THROTTLE].pulse_us > 1800;
        bool steering_high = raw[RC_CH_STEERING].pulse_us > 1800;

        if (throttle_high && steering_high) {
            ESP_LOGI(TAG, "Calibration trigger detected!");
            return true;
        }
    }

    return false;
}
