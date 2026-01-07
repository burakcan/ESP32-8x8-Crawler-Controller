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
#include "engine_sound.h"
#include "mode_switch.h"
#include "menu.h"

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
    ESP_LOGI(TAG, "║             GPIO %2d (horn)              ║", PIN_RC_AUX1);
    ESP_LOGI(TAG, "║             GPIO %2d (mode switch)       ║", PIN_RC_AUX2);
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
    rc_input_get_calibrated(RC_CH_AUX1, &cal->channels[RC_CH_AUX1], &aux1_data);  // Horn button
    rc_input_get_calibrated(RC_CH_AUX2, &cal->channels[RC_CH_AUX2], &aux2_data);  // Mode switch button
    rc_input_get_calibrated(RC_CH_AUX3, &cal->channels[RC_CH_AUX3], &aux3_data);
    rc_input_get_calibrated(RC_CH_AUX4, &cal->channels[RC_CH_AUX4], &aux4_data);

    bool signal_lost = throttle_data.signal_lost || steering_data.signal_lost;

    // Get button states
    bool aux1_pressed = (aux1_data.value > 400);  // Horn / Menu confirm
    bool aux2_pressed = (aux2_data.value > 400);  // Mode switch / Menu navigate

    // Update menu state machine (handles AUX2 when menu is active)
    menu_update(aux2_pressed);

    // AUX1 - Horn or Menu Confirm
    // When menu is active, AUX1 is used for menu confirmation
    // When menu is inactive, AUX1 is the horn button
    if (menu_is_active()) {
        menu_handle_confirm(aux1_pressed);
        engine_sound_set_horn(false);  // No horn while in menu
    } else {
        engine_sound_set_horn(aux1_pressed);
    }

    // AUX3 - Throttle Mode (3-position switch)
    // SWAPPED: Was AUX4, now AUX3
    // Low (<-400): Direct pass-through
    // Center (-400 to 400): Neutral - rev engine but no ESC output
    // High (>400): Realistic throttle physics
    static throttle_mode_t prev_throttle_mode = THROTTLE_MODE_DIRECT;
    throttle_mode_t throttle_mode;
    if (aux3_data.value > 400) {
        throttle_mode = THROTTLE_MODE_REALISTIC;
    } else if (aux3_data.value > -400) {
        throttle_mode = THROTTLE_MODE_NEUTRAL;
    } else {
        throttle_mode = THROTTLE_MODE_DIRECT;
    }
    if (throttle_mode != prev_throttle_mode) {
        ESP_LOGI(TAG, "Throttle mode: %d (aux3=%d)", throttle_mode, aux3_data.value);
        prev_throttle_mode = throttle_mode;
    }
    tuning_set_throttle_mode(throttle_mode);

    // AUX4 - Engine On/Off (momentary button)
    // SWAPPED: Was AUX3 (complex state machine), now simple single-press toggle
    static bool aux4_was_pressed = false;
    bool aux4_pressed = (aux4_data.value > 400);
    if (aux4_pressed && !aux4_was_pressed) {
        // Rising edge - toggle engine
        if (engine_sound_get_state() == ENGINE_OFF) {
            ESP_LOGI(TAG, "Engine start (AUX4)");
            engine_sound_start();
        } else {
            ESP_LOGI(TAG, "Engine stop (AUX4)");
            engine_sound_stop();
        }
    }
    aux4_was_pressed = aux4_pressed;

    // Check for signal loss
    if (signal_lost) {
        if (app_state != APP_STATE_FAILSAFE) {
            ESP_LOGW(TAG, "Signal lost! Entering failsafe mode");
            app_state = APP_STATE_FAILSAFE;
            menu_force_exit();  // Exit menu on signal loss
            esc_set_neutral();
            servo_center_all();
            tuning_reset_realistic_throttle();  // Reset simulated velocity
            tuning_reset_realistic_steering();  // Reset steering positions
        }
        return;
    }

    // Recover from failsafe
    if (app_state == APP_STATE_FAILSAFE) {
        ESP_LOGI(TAG, "Signal recovered, resuming operation");
        app_state = APP_STATE_RUNNING;
    }

    // Apply throttle to ESC with tuning (limits, subtrim, deadzone, reverse)
    // Skip ESC output in neutral mode (rev engine sound only)
    if (!tuning_is_neutral_mode()) {
        uint16_t esc_pulse = tuning_calc_esc_pulse(throttle_data.value);
        esc_set_pulse(esc_pulse);
    } else {
        esc_set_neutral();
    }

    // Update engine sound based on throttle and velocity
    // In realistic mode: use simulated velocity for natural physics
    // In direct/neutral mode: use throttle directly as pseudo-velocity for effects
    int16_t sound_velocity;
    if (throttle_mode == THROTTLE_MODE_REALISTIC) {
        sound_velocity = tuning_get_simulated_velocity();
    } else {
        // Use throttle as velocity - this makes effects work in all modes
        sound_velocity = throttle_data.value;
    }
    engine_sound_update(throttle_data.value, sound_velocity);

    // Apply steering expo curve
    int16_t steer = tuning_apply_expo(steering_data.value);

    // Apply speed-dependent steering reduction
    steer = tuning_apply_speed_steering(steer);

    // Update mode switch with button state (AUX2 = Channel 3 momentary button)
    // Priority: UI override > mode switch button
    steering_mode_t new_mode;
    uint8_t ui_mode;

    if (web_server_get_mode_override(&ui_mode)) {
        // UI has selected a mode - update mode_switch to stay in sync
        mode_switch_set_mode((steering_mode_t)ui_mode);
        new_mode = (steering_mode_t)ui_mode;
    } else {
        // RC: Use momentary button on Channel 3 (AUX2)
        // Single press: Toggle between Front and All-Axle
        // Double press: Crab mode
        // Triple press: Rear mode
        // In Crab/Rear: Single press returns to last normal mode
        bool mode_btn_pressed = (aux2_data.value > 400);
        mode_switch_update(mode_btn_pressed);
        new_mode = mode_switch_get_mode();
    }

    // Log mode changes
    if (new_mode != current_steering_mode) {
        const char *mode_names[] = {"Front", "Rear", "All-Axle", "Crab"};
        ESP_LOGI(TAG, "Steering mode: %s", mode_names[new_mode]);
        current_steering_mode = new_mode;
    }

    // Apply realistic steering if enabled (smooth interpolation of input)
    // This must happen BEFORE applying ratios - like a mechanical linkage,
    // all axles follow one smoothed steering input proportionally
    int16_t smoothed_steer = steer;
    if (tuning_is_realistic_steering_enabled()) {
        smoothed_steer = tuning_apply_realistic_steering(steer);
    }

    // Calculate per-axle steering based on mode and axle ratios
    int16_t final_positions[SERVO_COUNT] = {0, 0, 0, 0};

    switch (current_steering_mode) {
        case STEER_MODE_FRONT:
            // Axles 1-2 steer, 3-4 fixed (like a car)
            final_positions[0] = (smoothed_steer * tuning_get_axle_ratio(0, current_steering_mode)) / 100;
            final_positions[1] = (smoothed_steer * tuning_get_axle_ratio(1, current_steering_mode)) / 100;
            final_positions[2] = 0;
            final_positions[3] = 0;
            break;

        case STEER_MODE_REAR:
            // Axles 3-4 steer, 1-2 fixed (reverse direction for intuitive control)
            final_positions[0] = 0;
            final_positions[1] = 0;
            final_positions[2] = (-smoothed_steer * tuning_get_axle_ratio(2, current_steering_mode)) / 100;
            final_positions[3] = (-smoothed_steer * tuning_get_axle_ratio(3, current_steering_mode)) / 100;
            break;

        case STEER_MODE_ALL_AXLE:
            // All axles steer (1-2 opposite to 3-4 for tighter turning)
            // Rear gets additional ratio reduction via tuning_get_axle_ratio
            final_positions[0] = (smoothed_steer * tuning_get_axle_ratio(0, current_steering_mode)) / 100;
            final_positions[1] = (smoothed_steer * tuning_get_axle_ratio(1, current_steering_mode)) / 100;
            final_positions[2] = (-smoothed_steer * tuning_get_axle_ratio(2, current_steering_mode)) / 100;
            final_positions[3] = (-smoothed_steer * tuning_get_axle_ratio(3, current_steering_mode)) / 100;
            break;

        case STEER_MODE_CRAB:
            // All axles same direction at 100% (crab walk / sideways)
            // No ratios applied - all wheels must point the same direction
            final_positions[0] = smoothed_steer;
            final_positions[1] = smoothed_steer;
            final_positions[2] = smoothed_steer;
            final_positions[3] = smoothed_steer;
            break;

        default:
            break;
    }

    // Set servo positions with tuning (endpoints, subtrim, trim, reverse)
    // Skip if servo test mode is active (UI controls servos directly)
    if (!web_server_is_servo_test_active()) {
        for (int i = 0; i < SERVO_COUNT; i++) {
            uint16_t pulse = tuning_calc_servo_pulse(i, final_positions[i]);
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

    // Initialize engine sound system
    ESP_LOGI(TAG, "Initializing engine sound...");
    ESP_ERROR_CHECK(engine_sound_init());

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

    // Initialize mode switch (starts in Front steering mode)
    ESP_LOGI(TAG, "Initializing mode switch...");
    mode_switch_init();

    // Initialize menu system (registers long-press callback with mode_switch)
    ESP_LOGI(TAG, "Initializing menu system...");
    menu_init();

    // Initialize web server (WiFi OFF by default - use menu to enable)
    ESP_LOGI(TAG, "Initializing web server (WiFi OFF)...");
    ESP_ERROR_CHECK(web_server_init_no_wifi());

    // OTA update module (initialized when WiFi is enabled)
    // Note: OTA only works when WiFi is on

    // Mark this firmware as valid (cancels automatic rollback)
    ota_mark_valid();

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  AUX1: Horn (hold) / Menu confirm        ║");
    ESP_LOGI(TAG, "║  AUX2: Steering mode (1x/2x/3x press)    ║");
    ESP_LOGI(TAG, "║        Hold 1.5s = Enter settings menu   ║");
    ESP_LOGI(TAG, "║  AUX3: Throttle mode (3-pos switch)      ║");
    ESP_LOGI(TAG, "║  AUX4: Engine on/off (press)             ║");
    ESP_LOGI(TAG, "╠══════════════════════════════════════════╣");
    ESP_LOGI(TAG, "║  Menu: Volume / Profile / WiFi           ║");
    ESP_LOGI(TAG, "║  WiFi: %s / %s           ║", WIFI_AP_SSID, WIFI_AP_PASS);
    ESP_LOGI(TAG, "║  (WiFi auto-enables if no RC for 5 sec)  ║");
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

    // Engine starts OFF - user can start it with AUX3 short press

    // Main control loop
    uint32_t loop_count = 0;
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t loop_period_ticks = pdMS_TO_TICKS(MAIN_LOOP_PERIOD_MS);

    // Auto-WiFi: enable WiFi automatically if no RC signal for 5 seconds
    bool auto_wifi_enabled = false;
    #define AUTO_WIFI_TIMEOUT_MS 5000

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

        // Auto-WiFi: enable WiFi if no RC signal detected for 5 seconds
        // This allows configuration even when RC receiver is not connected
        if (!auto_wifi_enabled && !web_server_wifi_is_enabled()) {
            uint32_t signal_age = rc_input_signal_age_ms();
            if (signal_age >= AUTO_WIFI_TIMEOUT_MS) {
                ESP_LOGI(TAG, "No RC signal for %lu ms - enabling WiFi automatically",
                         (unsigned long)signal_age);
                auto_wifi_enabled = true;
                sound_play(SOUND_WIFI_ON);
                web_server_wifi_enable();
                udp_log_init();
                ota_update_init();

                // Set LED notification for 2 seconds
                uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
                wifi_switch_notify_until = now_ms + 2000;
                wifi_switch_notify_on = true;
            }
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
