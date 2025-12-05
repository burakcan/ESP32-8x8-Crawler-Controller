/**
 * @file calibration.c
 * @brief RC calibration system implementation
 */

#include "calibration.h"
#include "rc_input.h"
#include "nvs_storage.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <limits.h>

static const char *TAG = "CALIBRATION";

// Calibration timing parameters
#define CAL_CENTER_HOLD_TIME_MS     2000    // Time to hold sticks at center
#define CAL_ENDPOINT_TIME_MS        8000    // Time to move sticks to all endpoints
#define CAL_STABLE_THRESHOLD        10      // Max variation for "stable" reading
#define CAL_SAMPLE_COUNT            20      // Samples to average for center

// Channel names
static const char *channel_names[RC_CHANNEL_COUNT] = {
    "Throttle", "Steering", "Aux1", "Aux2"
};

// Current calibration data
static calibration_data_t cal_data;

// Calibration state
static calibration_state_t cal_state = CAL_STATE_IDLE;
static uint32_t cal_state_start_time = 0;
static const char *cal_status_msg = "Not calibrating";

// Temporary calibration data during process
static uint32_t center_samples[RC_CHANNEL_COUNT][CAL_SAMPLE_COUNT];
static int center_sample_idx = 0;
static uint16_t temp_min[RC_CHANNEL_COUNT];
static uint16_t temp_max[RC_CHANNEL_COUNT];

/**
 * @brief Get current time in milliseconds
 */
static uint32_t get_time_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/**
 * @brief Enter a new calibration state
 */
static void enter_state(calibration_state_t new_state, const char *message)
{
    cal_state = new_state;
    cal_state_start_time = get_time_ms();
    cal_status_msg = message;
    ESP_LOGI(TAG, "Calibration state: %s", message);
}

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

esp_err_t calibration_start(void)
{
    if (cal_state != CAL_STATE_IDLE) {
        ESP_LOGW(TAG, "Calibration already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   STARTING CALIBRATION MODE");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Step 1: Center all sticks and hold...");
    
    // Reset temporary data
    center_sample_idx = 0;
    for (int i = 0; i < RC_CHANNEL_COUNT; i++) {
        temp_min[i] = UINT16_MAX;
        temp_max[i] = 0;
    }
    
    enter_state(CAL_STATE_WAIT_CENTER, "Center all sticks and hold steady");
    return ESP_OK;
}

esp_err_t calibration_cancel(void)
{
    if (cal_state == CAL_STATE_IDLE) {
        return ESP_OK;
    }
    
    ESP_LOGW(TAG, "Calibration cancelled");
    enter_state(CAL_STATE_IDLE, "Calibration cancelled");
    return ESP_OK;
}

esp_err_t calibration_update(void)
{
    if (cal_state == CAL_STATE_IDLE || cal_state == CAL_STATE_COMPLETE || 
        cal_state == CAL_STATE_FAILED) {
        return ESP_OK;
    }
    
    uint32_t now = get_time_ms();
    uint32_t elapsed = now - cal_state_start_time;
    
    // Get current raw values
    rc_channel_raw_t raw[RC_CHANNEL_COUNT];
    rc_input_get_all_raw(raw);
    
    // Check if all channels have valid signal
    bool all_valid = true;
    for (int i = 0; i < RC_CHANNEL_COUNT; i++) {
        if (!raw[i].valid) {
            all_valid = false;
            break;
        }
    }
    
    if (!all_valid && cal_state != CAL_STATE_WAIT_CENTER) {
        ESP_LOGW(TAG, "Lost RC signal during calibration!");
        enter_state(CAL_STATE_FAILED, "Signal lost - calibration failed");
        return ESP_OK;
    }
    
    switch (cal_state) {
        case CAL_STATE_WAIT_CENTER:
            // Wait for valid signal on all channels
            if (all_valid) {
                enter_state(CAL_STATE_RECORD_CENTER, "Recording center positions...");
                center_sample_idx = 0;
            } else if (elapsed > 10000) {
                enter_state(CAL_STATE_FAILED, "No RC signal detected");
            }
            break;
            
        case CAL_STATE_RECORD_CENTER:
            // Record center samples
            if (all_valid) {
                for (int i = 0; i < RC_CHANNEL_COUNT; i++) {
                    center_samples[i][center_sample_idx] = raw[i].pulse_us;
                }
                center_sample_idx++;
                
                if (center_sample_idx >= CAL_SAMPLE_COUNT) {
                    // Calculate average center for each channel
                    for (int i = 0; i < RC_CHANNEL_COUNT; i++) {
                        uint32_t sum = 0;
                        for (int j = 0; j < CAL_SAMPLE_COUNT; j++) {
                            sum += center_samples[i][j];
                        }
                        cal_data.channels[i].center = sum / CAL_SAMPLE_COUNT;
                        
                        // Initialize min/max to center
                        temp_min[i] = cal_data.channels[i].center;
                        temp_max[i] = cal_data.channels[i].center;
                        
                        ESP_LOGI(TAG, "  %s center: %d us", 
                                 channel_names[i], cal_data.channels[i].center);
                    }
                    
                    ESP_LOGI(TAG, "Step 2: Move all sticks to their extreme positions...");
                    enter_state(CAL_STATE_WAIT_ENDPOINTS, 
                               "Move all sticks to ALL endpoints slowly");
                }
            }
            break;
            
        case CAL_STATE_WAIT_ENDPOINTS:
            // Give user a moment to start moving sticks
            if (elapsed > 500) {
                enter_state(CAL_STATE_RECORD_ENDPOINTS, 
                           "Recording endpoints - move sticks to ALL corners!");
            }
            break;
            
        case CAL_STATE_RECORD_ENDPOINTS:
            // Record min/max values
            if (all_valid) {
                for (int i = 0; i < RC_CHANNEL_COUNT; i++) {
                    if (raw[i].pulse_us < temp_min[i]) {
                        temp_min[i] = raw[i].pulse_us;
                    }
                    if (raw[i].pulse_us > temp_max[i]) {
                        temp_max[i] = raw[i].pulse_us;
                    }
                }
            }
            
            // Show progress
            if (elapsed % 1000 < 50) {  // Every second
                ESP_LOGI(TAG, "Recording... %lu/%d seconds", 
                         elapsed / 1000, CAL_ENDPOINT_TIME_MS / 1000);
                for (int i = 0; i < RC_CHANNEL_COUNT; i++) {
                    ESP_LOGI(TAG, "  %s: %d - %d - %d", 
                             channel_names[i], temp_min[i], 
                             cal_data.channels[i].center, temp_max[i]);
                }
            }
            
            if (elapsed >= CAL_ENDPOINT_TIME_MS) {
                // Finalize calibration
                bool essential_valid = true;  // Only throttle + steering are essential
                
                for (int i = 0; i < RC_CHANNEL_COUNT; i++) {
                    cal_data.channels[i].min = temp_min[i];
                    cal_data.channels[i].max = temp_max[i];
                    
                    // Validate ranges
                    uint16_t range_low = cal_data.channels[i].center - cal_data.channels[i].min;
                    uint16_t range_high = cal_data.channels[i].max - cal_data.channels[i].center;
                    
                    bool channel_valid = (range_low >= 100 && range_high >= 100);
                    
                    if (!channel_valid) {
                        if (i <= RC_CH_STEERING) {
                            // Throttle and steering are essential
                            ESP_LOGE(TAG, "  %s: Insufficient range - REQUIRED channel!", channel_names[i]);
                            essential_valid = false;
                        } else {
                            // Aux channels are optional - use defaults
                            ESP_LOGW(TAG, "  %s: No movement detected - using defaults", channel_names[i]);
                            cal_data.channels[i].min = RC_DEFAULT_MIN_US;
                            cal_data.channels[i].center = RC_DEFAULT_CENTER_US;
                            cal_data.channels[i].max = RC_DEFAULT_MAX_US;
                        }
                    }
                    
                    // Set default deadzone
                    cal_data.channels[i].deadzone = DEFAULT_DEADZONE_US;
                    cal_data.channels[i].reversed = false;
                }
                
                if (essential_valid) {
                    // Mark as calibrated and save
                    cal_data.calibrated = true;
                    cal_data.magic = CALIBRATION_MAGIC;
                    cal_data.version = CALIBRATION_VERSION;
                    
                    esp_err_t ret = nvs_save_calibration(&cal_data);
                    if (ret == ESP_OK) {
                        ESP_LOGI(TAG, "========================================");
                        ESP_LOGI(TAG, "   CALIBRATION COMPLETE!");
                        ESP_LOGI(TAG, "========================================");
                        for (int i = 0; i < RC_CHANNEL_COUNT; i++) {
                            ESP_LOGI(TAG, "  %s: %d - %d - %d", 
                                     channel_names[i],
                                     cal_data.channels[i].min,
                                     cal_data.channels[i].center,
                                     cal_data.channels[i].max);
                        }
                        enter_state(CAL_STATE_COMPLETE, "Calibration saved successfully!");
                    } else {
                        ESP_LOGE(TAG, "Failed to save calibration!");
                        enter_state(CAL_STATE_FAILED, "Failed to save calibration");
                    }
                } else {
                    enter_state(CAL_STATE_FAILED, 
                               "Calibration failed - insufficient stick movement");
                }
            }
            break;
            
        default:
            break;
    }
    
    return ESP_OK;
}

esp_err_t calibration_get_status(calibration_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    status->state = cal_state;
    status->status_message = cal_status_msg;
    
    uint32_t elapsed = get_time_ms() - cal_state_start_time;
    
    switch (cal_state) {
        case CAL_STATE_RECORD_CENTER:
            status->progress_percent = (center_sample_idx * 30) / CAL_SAMPLE_COUNT;
            status->time_remaining_ms = 0;
            break;
            
        case CAL_STATE_RECORD_ENDPOINTS:
            status->progress_percent = 30 + (elapsed * 70) / CAL_ENDPOINT_TIME_MS;
            status->time_remaining_ms = (elapsed < CAL_ENDPOINT_TIME_MS) ? 
                                        (CAL_ENDPOINT_TIME_MS - elapsed) : 0;
            break;
            
        case CAL_STATE_COMPLETE:
            status->progress_percent = 100;
            status->time_remaining_ms = 0;
            break;
            
        default:
            status->progress_percent = 0;
            status->time_remaining_ms = 0;
            break;
    }
    
    return ESP_OK;
}

bool calibration_in_progress(void)
{
    return (cal_state != CAL_STATE_IDLE && 
            cal_state != CAL_STATE_COMPLETE && 
            cal_state != CAL_STATE_FAILED);
}

bool calibration_is_valid(void)
{
    return cal_data.calibrated && cal_data.magic == CALIBRATION_MAGIC;
}

const calibration_data_t* calibration_get_data(void)
{
    return &cal_data;
}

esp_err_t calibration_clear(void)
{
    ESP_LOGI(TAG, "Clearing calibration...");
    
    nvs_clear_calibration();
    nvs_get_default_calibration(&cal_data);
    cal_state = CAL_STATE_IDLE;
    
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
