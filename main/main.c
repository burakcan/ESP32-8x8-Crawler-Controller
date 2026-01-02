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
#include "esp_task_wdt.h"
#include "driver/gpio.h"

#include "config.h"
#include "version.h"
#include "nvs_storage.h"
#include "rc_input.h"
#include "pwm_output.h"
#include "calibration.h"
#include "tuning.h"
#include "web_server.h"
#include "ota_update.h"
#include "led_rgb.h"
#include "udp_log.h"
#include "sound.h"

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

// LED state tracking
static led_state_t current_led_state = LED_STATE_BOOT;
static bool wifi_sta_was_connected = false;  // Track WiFi STA state changes
static uint32_t wifi_notify_until = 0;       // Show WiFi STA pattern until this loop count
static uint32_t wifi_switch_notify_until = 0; // Show WiFi on/off pattern until this timestamp (ms)
static bool wifi_switch_notify_on = false;    // true = show ON pattern, false = show OFF pattern

/**
 * @brief Print startup banner
 */
static void print_banner(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   8x8 CRAWLER CONTROLLER v%s  %s  ║", FW_VERSION, FW_BUILD_DATE);
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

    // Get RC input
    rc_channel_data_t throttle_data, steering_data, aux1_data, aux2_data, aux3_data, aux4_data;
    rc_input_get_calibrated(RC_CH_THROTTLE, &cal->channels[RC_CH_THROTTLE], &throttle_data);
    rc_input_get_calibrated(RC_CH_STEERING, &cal->channels[RC_CH_STEERING], &steering_data);
    rc_input_get_calibrated(RC_CH_AUX1, &cal->channels[RC_CH_AUX1], &aux1_data);
    rc_input_get_calibrated(RC_CH_AUX2, &cal->channels[RC_CH_AUX2], &aux2_data);
    rc_input_get_calibrated(RC_CH_AUX3, &cal->channels[RC_CH_AUX3], &aux3_data);
    rc_input_get_calibrated(RC_CH_AUX4, &cal->channels[RC_CH_AUX4], &aux4_data);

    bool signal_lost = throttle_data.signal_lost || steering_data.signal_lost;

    // AUX3 controls WiFi power (ON when switch is high)
    static bool last_wifi_switch = false;
    bool wifi_switch = (aux3_data.value > 200);  // High = WiFi ON

    if (wifi_switch != last_wifi_switch) {
        if (wifi_switch) {
            web_server_wifi_enable();
            // Initialize UDP logging when WiFi comes on
            udp_log_init();
            // Initialize OTA when WiFi comes on
            ota_update_init();
        } else {
            web_server_wifi_disable();
        }
        // Set LED notification for 2 seconds
        wifi_switch_notify_until = (uint32_t)(esp_timer_get_time() / 1000) + 2000;
        wifi_switch_notify_on = wifi_switch;
        last_wifi_switch = wifi_switch;
    }

    // AUX4 controls realistic throttle mode (ON when switch is high)
    bool realistic_switch = (aux4_data.value > 200);
    tuning_set_realistic_override(realistic_switch);

    // Check for signal loss
    if (signal_lost) {
        if (app_state != APP_STATE_FAILSAFE) {
            ESP_LOGW(TAG, "Signal lost! Entering failsafe mode");
            app_state = APP_STATE_FAILSAFE;
            esc_set_neutral();
            servo_center_all();
            tuning_reset_realistic_throttle();  // Reset simulated velocity
        }
        return;
    }

    // Recover from failsafe
    if (app_state == APP_STATE_FAILSAFE) {
        ESP_LOGI(TAG, "Signal recovered, resuming operation");
        app_state = APP_STATE_RUNNING;
    }

    // Apply throttle to ESC with tuning (limits, subtrim, deadzone, reverse)
    uint16_t esc_pulse = tuning_calc_esc_pulse(throttle_data.value);
    esc_set_pulse(esc_pulse);

    // Apply steering expo curve
    int16_t steer = tuning_apply_expo(steering_data.value);

    // Apply speed-dependent steering reduction
    steer = tuning_apply_speed_steering(steer);

    // Determine steering mode
    // Priority: UI override > RC AUX switches
    steering_mode_t new_mode;
    uint8_t ui_mode;

    if (web_server_get_mode_override(&ui_mode)) {
        // UI has selected a mode
        new_mode = (steering_mode_t)ui_mode;
    } else {
        // RC: Use AUX switch values
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
        ESP_LOGI(TAG, "Steering mode: %s (steer=%d)", mode_names[new_mode], steer);
        current_steering_mode = new_mode;
    }

    // Calculate per-axle steering based on mode and axle ratios
    int16_t axle_values[4] = {0, 0, 0, 0};

    switch (current_steering_mode) {
        case STEER_MODE_FRONT:
            // Axles 1-2 steer, 3-4 fixed (like a car)
            axle_values[0] = (steer * tuning_get_axle_ratio(0, current_steering_mode)) / 100;
            axle_values[1] = (steer * tuning_get_axle_ratio(1, current_steering_mode)) / 100;
            axle_values[2] = 0;
            axle_values[3] = 0;
            break;

        case STEER_MODE_REAR:
            // Axles 3-4 steer, 1-2 fixed (reverse direction for intuitive control)
            axle_values[0] = 0;
            axle_values[1] = 0;
            axle_values[2] = (-steer * tuning_get_axle_ratio(2, current_steering_mode)) / 100;
            axle_values[3] = (-steer * tuning_get_axle_ratio(3, current_steering_mode)) / 100;
            break;

        case STEER_MODE_ALL_AXLE:
            // All axles steer (1-2 opposite to 3-4 for tighter turning)
            // Rear gets additional ratio reduction via tuning_get_axle_ratio
            axle_values[0] = (steer * tuning_get_axle_ratio(0, current_steering_mode)) / 100;
            axle_values[1] = (steer * tuning_get_axle_ratio(1, current_steering_mode)) / 100;
            axle_values[2] = (-steer * tuning_get_axle_ratio(2, current_steering_mode)) / 100;
            axle_values[3] = (-steer * tuning_get_axle_ratio(3, current_steering_mode)) / 100;
            break;

        case STEER_MODE_CRAB:
            // All axles same direction at 100% (crab walk / sideways)
            // No ratios applied - all wheels must point the same direction
            axle_values[0] = steer;
            axle_values[1] = steer;
            axle_values[2] = steer;
            axle_values[3] = steer;
            break;

        default:
            break;
    }

    // Set servo positions with tuning (endpoints, subtrim, trim, reverse)
    // Skip if servo test mode is active (UI controls servos directly)
    if (!web_server_is_servo_test_active()) {
        for (int i = 0; i < SERVO_COUNT; i++) {
            uint16_t pulse = tuning_calc_servo_pulse(i, axle_values[i]);
            servo_set_pulse((servo_id_t)i, pulse);
        }
    }
}

/**
 * @brief Update web UI and optionally print to serial
 */
static void update_status(void)
{
    // Skip status updates if WiFi is off
    if (!web_server_wifi_is_enabled()) {
        return;
    }

    static uint32_t last_update = 0;
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
    
    // Build web status
    web_status_t web_status = {
        .rc_throttle = ch[RC_CH_THROTTLE].value,
        .rc_steering = ch[RC_CH_STEERING].value,
        .rc_aux1 = ch[RC_CH_AUX1].value,
        .rc_aux2 = ch[RC_CH_AUX2].value,
        .rc_aux3 = ch[RC_CH_AUX3].value,
        .rc_aux4 = ch[RC_CH_AUX4].value,
        .rc_raw = {
            ch[RC_CH_THROTTLE].pulse_us,
            ch[RC_CH_STEERING].pulse_us,
            ch[RC_CH_AUX1].pulse_us,
            ch[RC_CH_AUX2].pulse_us,
            ch[RC_CH_AUX3].pulse_us,
            ch[RC_CH_AUX4].pulse_us
        },
        .esc_pulse = esc_get_pulse(),
        .servo_a1 = servo_get_pulse(SERVO_AXLE_1),
        .servo_a2 = servo_get_pulse(SERVO_AXLE_2),
        .servo_a3 = servo_get_pulse(SERVO_AXLE_3),
        .servo_a4 = servo_get_pulse(SERVO_AXLE_4),
        .steering_mode = current_steering_mode,
        .signal_lost = ch[RC_CH_THROTTLE].signal_lost,
        .calibrated = calibration_is_valid(),
        .calibrating = calibration_in_progress(),
        .cal_progress = 0,  // No longer used - calibration is step-based now
        .uptime_ms = now,
        .heap_free = esp_get_free_heap_size(),
        .heap_min = esp_get_minimum_free_heap_size(),
        .wifi_rssi = 0  // TODO: Get actual RSSI if connected to STA
    };
    
    web_server_update_status(&web_status);
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
    // Initialize RGB LED (shows rainbow boot animation)
    ESP_LOGI(TAG, "Initializing RGB LED...");
    ESP_ERROR_CHECK(led_rgb_init());

    // Initialize sound system
    ESP_LOGI(TAG, "Initializing sound system...");
    ESP_ERROR_CHECK(sound_init());

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

    // Initialize tuning system (loads from NVS or defaults)
    ESP_LOGI(TAG, "Initializing tuning...");
    ESP_ERROR_CHECK(tuning_init(NULL));

    // Initialize web server (WiFi OFF by default - controlled by AUX3)
    ESP_LOGI(TAG, "Initializing web server (WiFi OFF)...");
    ESP_ERROR_CHECK(web_server_init_no_wifi());

    // OTA update module (initialized when WiFi is enabled)
    // Note: OTA only works when WiFi is on

    // Mark this firmware as valid (cancels automatic rollback)
    ota_mark_valid();

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  WiFi OFF by default (save power)        ║");
    ESP_LOGI(TAG, "║  Flip AUX3 switch ON to enable WiFi      ║");
    ESP_LOGI(TAG, "║  WiFi:   %s / %s         ║", WIFI_AP_SSID, WIFI_AP_PASS);
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    
    // Set ESC and servos to safe positions
    esc_set_neutral();
    servo_center_all();
    
    // Brief delay to let RC receiver boot
    ESP_LOGI(TAG, "Waiting for RC signal...");
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Check calibration status
    if (!calibration_is_valid()) {
        ESP_LOGW(TAG, "No valid calibration - use web UI to calibrate");
    }

    ESP_LOGI(TAG, "Starting normal operation...");
    app_state = APP_STATE_RUNNING;

    // Initialize Task Watchdog Timer (5 second timeout)
    // This will reset the device if the main loop hangs
    ESP_LOGI(TAG, "Initializing watchdog timer...");
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 5000,
        .idle_core_mask = 0,  // Don't watch idle tasks
        .trigger_panic = true  // Reset on timeout
    };
    ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&wdt_config));
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));  // Add current task to watchdog

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║            SYSTEM READY                  ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");

    // Play boot chime to indicate system is ready
    sound_play_boot_chime();

    // Main control loop
    uint32_t loop_count = 0;
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t loop_period_ticks = pdMS_TO_TICKS(MAIN_LOOP_PERIOD_MS);

    while (1) {
        // Check if calibration is running (can be started via web UI)
        bool calibrating = calibration_in_progress();

        if (calibrating) {
            // Update calibration to read current pulse values
            calibration_update();
            app_state = APP_STATE_CALIBRATING;
        } else {
            // Check if we just finished calibration
            if (app_state == APP_STATE_CALIBRATING) {
                ESP_LOGI(TAG, "Calibration finished, resuming normal operation");
                app_state = APP_STATE_RUNNING;
            }

            // Normal operation
            process_control_loop();
        }

        // Check for WiFi STA connection state change (only if WiFi enabled)
        if (web_server_wifi_is_enabled()) {
            bool wifi_connected = web_server_is_sta_connected();
            if (wifi_connected && !wifi_sta_was_connected) {
                // Just connected - show notification for 2 seconds (200 loops)
                wifi_notify_until = loop_count + 200;
            }
            wifi_sta_was_connected = wifi_connected;
        }

        // Update LED state based on system state (priority order)
        led_state_t new_led_state = LED_STATE_IDLE;
        ota_progress_t ota = ota_get_progress();
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

        if (ota.status == OTA_STATUS_IN_PROGRESS) {
            new_led_state = LED_STATE_OTA;
        } else if (app_state == APP_STATE_CALIBRATING) {
            new_led_state = LED_STATE_CALIBRATING;
        } else if (app_state == APP_STATE_FAILSAFE) {
            new_led_state = LED_STATE_FAILSAFE;
        } else if (now_ms < wifi_switch_notify_until) {
            // WiFi switch changed - show on/off notification
            new_led_state = wifi_switch_notify_on ? LED_STATE_WIFI_ON : LED_STATE_WIFI_OFF;
        } else if (loop_count < wifi_notify_until) {
            // WiFi STA just connected
            new_led_state = LED_STATE_WIFI_CONNECTED;
        } else if (app_state == APP_STATE_RUNNING) {
            new_led_state = LED_STATE_RUNNING;
        } else {
            new_led_state = LED_STATE_IDLE;
        }

        // Only update state if changed (prevents resetting animations)
        if (new_led_state != current_led_state) {
            current_led_state = new_led_state;
            led_rgb_set_state(new_led_state);
        }

        // Update LED animation
        led_rgb_update();

        // Only update web server stuff if WiFi is on
        if (web_server_wifi_is_enabled()) {
            // Update servo test mode timeout
            web_server_update_servo_test();

            // Update web UI
            update_status();
        }

        // Feed watchdog to prevent reset
        esp_task_wdt_reset();

        // Maintain consistent loop timing (compensates for execution time)
        vTaskDelayUntil(&last_wake_time, loop_period_ticks);

        loop_count++;
    }
}
