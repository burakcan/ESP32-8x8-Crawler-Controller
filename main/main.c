/**
 * @file main.c
 * @brief 8x8 Crawler Controller - Main Application
 * 
 * Main entry point and control loop for the 8x8 RC crawler controller.
 * Handles RC input, calibration, steering modes, and output to ESC/servos.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#include "config.h"
#include "version.h"
#include "nvs_storage.h"
#include "rc_input.h"
#include "pwm_output.h"
#include "calibration.h"
#include "web_server.h"
#include "ota_update.h"

static const char *TAG = "MAIN";

// Application state
typedef enum {
    APP_STATE_INIT,
    APP_STATE_CALIBRATING,
    APP_STATE_RUNNING,
    APP_STATE_FAILSAFE
} app_state_t;

static app_state_t app_state = APP_STATE_INIT;
static steering_mode_t current_steering_mode = STEER_MODE_FRONT;

// Status LED control
static void status_led_init(void)
{
    gpio_reset_pin(PIN_STATUS_LED);
    gpio_set_direction(PIN_STATUS_LED, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_STATUS_LED, 0);
}

static void status_led_set(bool on)
{
    gpio_set_level(PIN_STATUS_LED, on ? 1 : 0);
}

/**
 * @brief Print startup banner
 */
static void print_banner(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║     8x8 CRAWLER CONTROLLER v%s b%d    ║", FW_VERSION, FW_BUILD_NUMBER);
    ESP_LOGI(TAG, "╠══════════════════════════════════════════╣");
    ESP_LOGI(TAG, "║  RC Input:   GPIO %2d (throttle)          ║", PIN_RC_THROTTLE);
    ESP_LOGI(TAG, "║             GPIO %2d (steering)          ║", PIN_RC_STEERING);
    ESP_LOGI(TAG, "║             GPIO %2d (aux1)              ║", PIN_RC_AUX1);
    ESP_LOGI(TAG, "║             GPIO %2d (aux2)              ║", PIN_RC_AUX2);
    ESP_LOGI(TAG, "║  ESC:        GPIO %2d                     ║", PIN_ESC);
    ESP_LOGI(TAG, "║  Servos:     A1:%2d A2:%2d A3:%2d A4:%2d    ║", 
             PIN_SERVO_AXLE_1, PIN_SERVO_AXLE_2, PIN_SERVO_AXLE_3, PIN_SERVO_AXLE_4);
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
}

/**
 * @brief Process RC input and update outputs
 */
static void process_control_loop(void)
{
    const calibration_data_t *cal = calibration_get_data();
    
    // Get calibrated channel values
    rc_channel_data_t throttle_data, steering_data;
    rc_input_get_calibrated(RC_CH_THROTTLE, &cal->channels[RC_CH_THROTTLE], &throttle_data);
    rc_input_get_calibrated(RC_CH_STEERING, &cal->channels[RC_CH_STEERING], &steering_data);
    
    // Check for signal loss
    if (throttle_data.signal_lost || steering_data.signal_lost) {
        if (app_state != APP_STATE_FAILSAFE) {
            ESP_LOGW(TAG, "Signal lost! Entering failsafe mode");
            app_state = APP_STATE_FAILSAFE;
            esc_set_neutral();
            servo_center_all();
        }
        return;
    }
    
    // Recover from failsafe
    if (app_state == APP_STATE_FAILSAFE) {
        ESP_LOGI(TAG, "Signal recovered, resuming operation");
        app_state = APP_STATE_RUNNING;
    }
    
    // Determine steering mode
    // Priority: UI override > AUX switches
    steering_mode_t new_mode;
    uint8_t ui_mode;
    
    if (web_server_get_mode_override(&ui_mode)) {
        // UI has selected a mode
        new_mode = (steering_mode_t)ui_mode;
    } else {
        // Use AUX switches
        rc_channel_data_t aux1_data, aux2_data;
        rc_input_get_calibrated(RC_CH_AUX1, &cal->channels[RC_CH_AUX1], &aux1_data);
        rc_input_get_calibrated(RC_CH_AUX2, &cal->channels[RC_CH_AUX2], &aux2_data);
        
        // AUX1 OFF + AUX2 OFF = Front (normal)
        // AUX1 ON  + AUX2 OFF = All Axle (tight turns)
        // AUX1 OFF + AUX2 ON  = Crab (sideways)
        // AUX1 ON  + AUX2 ON  = Rear (rear axles steer)
        bool aux1_on = aux1_data.value > 200;
        bool aux2_on = aux2_data.value > 200;
        
        if (!aux1_on && !aux2_on) {
            new_mode = STEER_MODE_FRONT;
        } else if (aux1_on && !aux2_on) {
            new_mode = STEER_MODE_ALL_AXLE;
        } else if (!aux1_on && aux2_on) {
            new_mode = STEER_MODE_CRAB;
        } else {
            new_mode = STEER_MODE_REAR;
        }
    }
    
    // Log mode changes
    if (new_mode != current_steering_mode) {
        const char *mode_names[] = {"Front", "Rear", "All-Axle", "Crab"};
        ESP_LOGI(TAG, "Steering mode: %s", mode_names[new_mode]);
        current_steering_mode = new_mode;
    }
    
    // Apply throttle to ESC
    esc_set_throttle(throttle_data.value);
    
    // Apply steering based on current mode
    // Axles 1-2 are front pair, Axles 3-4 are rear pair
    int16_t axle_1 = 0, axle_2 = 0, axle_3 = 0, axle_4 = 0;
    int16_t steer = steering_data.value;
    
    switch (current_steering_mode) {
        case STEER_MODE_FRONT:
            // Axles 1-2 steer, 3-4 fixed (like a car)
            axle_1 = steer;
            axle_2 = steer;
            axle_3 = 0;
            axle_4 = 0;
            break;
            
        case STEER_MODE_REAR:
            // Axles 3-4 steer, 1-2 fixed (reverse direction for intuitive control)
            axle_1 = 0;
            axle_2 = 0;
            axle_3 = -steer;
            axle_4 = -steer;
            break;
            
        case STEER_MODE_ALL_AXLE:
            // All axles steer (1-2 opposite to 3-4 for tighter turning)
            axle_1 = steer;
            axle_2 = steer;
            axle_3 = -steer;
            axle_4 = -steer;
            break;
            
        case STEER_MODE_CRAB:
            // All axles same direction (crab walk / sideways)
            axle_1 = steer;
            axle_2 = steer;
            axle_3 = steer;
            axle_4 = steer;
            break;
            
        default:
            break;
    }
    
    // Set servo positions (one servo per axle)
    servo_set_position(SERVO_AXLE_1, axle_1);
    servo_set_position(SERVO_AXLE_2, axle_2);
    servo_set_position(SERVO_AXLE_3, axle_3);
    servo_set_position(SERVO_AXLE_4, axle_4);
}

/**
 * @brief Update web UI and optionally print to serial
 */
static void update_status(void)
{
    static uint32_t last_update = 0;
    static uint32_t last_serial = 0;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    
    // Update web UI at 10Hz
    if (now - last_update < 100) {
        return;
    }
    last_update = now;
    
    const calibration_data_t *cal = calibration_get_data();
    
    rc_channel_data_t ch[RC_CHANNEL_COUNT];
    for (int i = 0; i < RC_CHANNEL_COUNT; i++) {
        rc_input_get_calibrated((rc_channel_t)i, &cal->channels[i], &ch[i]);
    }
    
    // Get calibration status
    calibration_status_t cal_status;
    calibration_get_status(&cal_status);
    
    // Build web status
    web_status_t web_status = {
        .rc_throttle = ch[RC_CH_THROTTLE].value,
        .rc_steering = ch[RC_CH_STEERING].value,
        .rc_aux1 = ch[RC_CH_AUX1].value,
        .rc_aux2 = ch[RC_CH_AUX2].value,
        .rc_throttle_us = ch[RC_CH_THROTTLE].pulse_us,
        .rc_steering_us = ch[RC_CH_STEERING].pulse_us,
        .esc_pulse = esc_get_pulse(),
        .servo_a1 = servo_get_pulse(SERVO_AXLE_1),
        .servo_a2 = servo_get_pulse(SERVO_AXLE_2),
        .servo_a3 = servo_get_pulse(SERVO_AXLE_3),
        .servo_a4 = servo_get_pulse(SERVO_AXLE_4),
        .steering_mode = current_steering_mode,
        .signal_lost = ch[RC_CH_THROTTLE].signal_lost,
        .calibrated = calibration_is_valid(),
        .calibrating = calibration_in_progress(),
        .cal_progress = cal_status.progress_percent,
        .uptime_sec = now / 1000
    };
    
    web_server_update_status(&web_status);
    
    // Print to serial less frequently (every 2 seconds)
    if (now - last_serial >= 2000) {
        last_serial = now;
        if (!ch[RC_CH_THROTTLE].signal_lost) {
            ESP_LOGI(TAG, "THR:%+5d STR:%+5d | ESC:%4d | Mode:%d",
                     ch[RC_CH_THROTTLE].value,
                     ch[RC_CH_STEERING].value,
                     esc_get_pulse(),
                     current_steering_mode);
        }
    }
}

/**
 * @brief Main application entry point
 */
void app_main(void)
{
    print_banner();
    
    // Initialize NVS (required for calibration storage)
    ESP_LOGI(TAG, "Initializing NVS...");
    ESP_ERROR_CHECK(nvs_storage_init());
    
    // Initialize status LED
    status_led_init();
    
    // Initialize RC input capture
    ESP_LOGI(TAG, "Initializing RC input...");
    ESP_ERROR_CHECK(rc_input_init());
    
    // Initialize PWM outputs (ESC + servos)
    ESP_LOGI(TAG, "Initializing PWM outputs...");
    ESP_ERROR_CHECK(pwm_output_init());
    
    // Initialize calibration system (loads from NVS or defaults)
    ESP_LOGI(TAG, "Initializing calibration...");
    calibration_data_t cal_data;
    ESP_ERROR_CHECK(calibration_init(&cal_data));
    
    // Initialize web server (WiFi AP + HTTP)
    ESP_LOGI(TAG, "Initializing web server...");
    ESP_ERROR_CHECK(web_server_init());

    // Initialize OTA update module
    ESP_LOGI(TAG, "Initializing OTA update...");
    ESP_ERROR_CHECK(ota_update_init());

    // Mark this firmware as valid (cancels automatic rollback)
    // This should be called after all critical init is successful
    ota_mark_valid();

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  Web UI: http://%s              ║", web_server_get_ip());
    ESP_LOGI(TAG, "║  WiFi:   %s / %s         ║", WIFI_AP_SSID, WIFI_AP_PASS);
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    
    // Set ESC and servos to safe positions
    esc_set_neutral();
    servo_center_all();
    
    // Brief delay to let RC receiver boot
    ESP_LOGI(TAG, "Waiting for RC signal...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Check for calibration trigger (sticks in corners)
    bool need_calibration = false;
    
    if (!calibration_is_valid()) {
        ESP_LOGW(TAG, "No valid calibration found!");
        need_calibration = true;
    } else if (calibration_check_trigger()) {
        ESP_LOGI(TAG, "Calibration trigger detected!");
        need_calibration = true;
    }
    
    if (need_calibration) {
        ESP_LOGI(TAG, "Entering calibration mode...");
        app_state = APP_STATE_CALIBRATING;
        calibration_start();
    } else {
        ESP_LOGI(TAG, "Starting normal operation...");
        app_state = APP_STATE_RUNNING;
    }
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║            SYSTEM READY                  ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    
    // Blink LED to indicate ready
    for (int i = 0; i < 3; i++) {
        status_led_set(true);
        vTaskDelay(pdMS_TO_TICKS(100));
        status_led_set(false);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Main control loop
    uint32_t loop_count = 0;
    while (1) {
        uint32_t loop_start = (uint32_t)(esp_timer_get_time() / 1000);
        
        if (app_state == APP_STATE_CALIBRATING) {
            // Update calibration state machine
            calibration_update();
            
            // Blink LED during calibration
            status_led_set((loop_count / 10) % 2);
            
            // Check if calibration finished
            if (!calibration_in_progress()) {
                calibration_status_t status;
                calibration_get_status(&status);
                
                if (status.state == CAL_STATE_COMPLETE) {
                    ESP_LOGI(TAG, "Calibration complete! Starting normal operation...");
                    app_state = APP_STATE_RUNNING;
                } else if (status.state == CAL_STATE_FAILED) {
                    ESP_LOGE(TAG, "Calibration failed: %s", status.status_message);
                    ESP_LOGI(TAG, "Using default calibration values...");
                    app_state = APP_STATE_RUNNING;
                }
            }
        } else {
            // Normal operation
            process_control_loop();
            
            // Solid LED when running, off in failsafe
            status_led_set(app_state == APP_STATE_RUNNING);
        }
        
        // Update web UI (runs in all states)
        update_status();
        
        // Maintain consistent loop timing
        uint32_t loop_time = (uint32_t)(esp_timer_get_time() / 1000) - loop_start;
        if (loop_time < MAIN_LOOP_PERIOD_MS) {
            vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_PERIOD_MS - loop_time));
        }
        
        loop_count++;
    }
}
