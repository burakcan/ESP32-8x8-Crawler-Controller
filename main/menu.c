/**
 * @file menu.c
 * @brief 2-Level menu system implementation
 *
 * Provides settings access via RC controller:
 * - Long-press AUX2 to enter menu
 * - Press AUX2 to cycle items
 * - Press AUX1 to enter/confirm
 * - Timeout or long-press to exit
 */

#include "menu.h"
#include "mode_switch.h"
#include "sound.h"
#include "engine_sound.h"
#include "web_server.h"
#include "nvs_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"

// TTS sound samples
#include "sounds/menu/menu_enter.h"
#include "sounds/menu/menu_back.h"
#include "sounds/menu/menu_confirm.h"
#include "sounds/menu/menu_cancel.h"
#include "sounds/menu/cat_volume.h"
#include "sounds/menu/cat_profile.h"
#include "sounds/menu/cat_horn.h"
#include "sounds/menu/cat_wifi.h"
#include "sounds/menu/opt_vol_low.h"
#include "sounds/menu/opt_vol_medium.h"
#include "sounds/menu/opt_vol_high.h"
#include "sounds/menu/opt_profile_cat.h"
#include "sounds/menu/opt_profile_unimog.h"
#include "sounds/menu/opt_profile_man.h"
#include "sounds/menu/opt_horn_truck.h"
#include "sounds/menu/opt_horn_mantge.h"
#include "sounds/menu/opt_horn_cucaracha.h"
#include "sounds/menu/opt_horn_2tone.h"
#include "sounds/menu/opt_horn_dixie.h"
#include "sounds/menu/opt_horn_peterbilt.h"
#include "sounds/menu/opt_horn_outlaw.h"
#include "sounds/menu/opt_wifi_on.h"
#include "sounds/menu/opt_wifi_off.h"

static const char *TAG = "MENU";

// Timing constants
#define MENU_LONGPRESS_MS       1500    // Long-press to enter menu / back / exit
#define MENU_TIMEOUT_MS         10000   // Auto-exit timeout (10 seconds)
#define MENU_DEBOUNCE_MS        50      // Button debounce

// Menu state
static menu_state_t state = MENU_STATE_INACTIVE;
static uint8_t category_index = 0;
static uint8_t option_index = 0;
static int64_t last_activity_time = 0;

// Button tracking (for edge detection and long-press in menu mode)
static bool aux2_was_pressed = false;
static int64_t aux2_press_start = 0;
static bool aux2_longpress_handled = false;

static bool aux1_was_pressed = false;

// Forward declarations
static void enter_menu(void);
static void exit_menu(bool cancelled);
static void play_category_sound(uint8_t cat);
static void play_option_sound(uint8_t cat, uint8_t opt);
static uint8_t get_current_option(uint8_t cat);
static void apply_option(uint8_t cat, uint8_t opt);
static uint8_t get_option_count(uint8_t cat);

/**
 * @brief Long-press callback from mode_switch module
 *
 * This is called when AUX2 is held for 1.5s while menu is INACTIVE.
 * When menu is ACTIVE, we handle long-press ourselves in menu_update().
 */
static void on_longpress_callback(void)
{
    if (state == MENU_STATE_INACTIVE) {
        enter_menu();
    }
    // When menu is active, long-press is handled in menu_update()
}

/**
 * @brief Enter menu mode
 */
static void enter_menu(void)
{
    ESP_LOGI(TAG, "=== MENU ENTERED (Level 1: Categories) ===");

    state = MENU_STATE_LEVEL1;
    category_index = 0;
    option_index = 0;
    last_activity_time = esp_timer_get_time() / 1000;

    // Mark long-press as handled so the button release doesn't trigger a category cycle
    aux2_longpress_handled = true;
    aux2_was_pressed = true;  // Sync button state

    // Disable steering mode changes while in menu
    mode_switch_set_enabled(false);

    // Mute engine sound during menu (so TTS is clear)
    engine_sound_enable(false);

    // Play "Menu" TTS
    sound_play_sample(menu_menu_enterSamples, menu_menu_enterSampleCount,
                      menu_menu_enterSampleRate, 80);

    // Then play current category
    vTaskDelay(pdMS_TO_TICKS(200));
    const char *cat_names[] = {"Volume", "Profile", "Horn", "WiFi"};
    ESP_LOGI(TAG, "Category: %s", cat_names[category_index]);
    play_category_sound(category_index);
}

/**
 * @brief Exit menu mode
 */
static void exit_menu(bool cancelled)
{
    if (state == MENU_STATE_INACTIVE) {
        return;
    }

    ESP_LOGI(TAG, "Exiting menu (%s)", cancelled ? "cancelled" : "confirmed");

    state = MENU_STATE_INACTIVE;
    category_index = 0;
    option_index = 0;

    // Re-enable steering mode changes
    mode_switch_set_enabled(true);

    if (cancelled) {
        // Play "Cancel" TTS
        sound_play_sample(menu_menu_cancelSamples, menu_menu_cancelSampleCount,
                          menu_menu_cancelSampleRate, 80);
    }
    // If not cancelled, confirm sound was already played

    // Re-enable engine sound after TTS plays
    vTaskDelay(pdMS_TO_TICKS(100));
    engine_sound_enable(true);
}

/**
 * @brief Play category indication sound (TTS)
 */
static void play_category_sound(uint8_t cat)
{
    switch (cat) {
        case MENU_CAT_VOLUME:
            sound_play_sample(menu_cat_volumeSamples, menu_cat_volumeSampleCount,
                              menu_cat_volumeSampleRate, 80);
            break;
        case MENU_CAT_PROFILE:
            sound_play_sample(menu_cat_profileSamples, menu_cat_profileSampleCount,
                              menu_cat_profileSampleRate, 80);
            break;
        case MENU_CAT_HORN:
            sound_play_sample(menu_cat_hornSamples, menu_cat_hornSampleCount,
                              menu_cat_hornSampleRate, 80);
            break;
        case MENU_CAT_WIFI:
            sound_play_sample(menu_cat_wifiSamples, menu_cat_wifiSampleCount,
                              menu_cat_wifiSampleRate, 80);
            break;
        default:
            sound_play_sample(menu_cat_volumeSamples, menu_cat_volumeSampleCount,
                              menu_cat_volumeSampleRate, 80);
            break;
    }
}

/**
 * @brief Play option indication sound (TTS)
 */
static void play_option_sound(uint8_t cat, uint8_t opt)
{
    switch (cat) {
        case MENU_CAT_VOLUME:
            switch (opt) {
                case MENU_VOL_LOW:
                    sound_play_sample(menu_opt_vol_lowSamples, menu_opt_vol_lowSampleCount,
                                      menu_opt_vol_lowSampleRate, 80);
                    break;
                case MENU_VOL_MEDIUM:
                    sound_play_sample(menu_opt_vol_mediumSamples, menu_opt_vol_mediumSampleCount,
                                      menu_opt_vol_mediumSampleRate, 80);
                    break;
                case MENU_VOL_HIGH:
                    sound_play_sample(menu_opt_vol_highSamples, menu_opt_vol_highSampleCount,
                                      menu_opt_vol_highSampleRate, 80);
                    break;
            }
            break;

        case MENU_CAT_PROFILE:
            switch (opt) {
                case MENU_PROFILE_CAT:
                    sound_play_sample(menu_opt_profile_catSamples, menu_opt_profile_catSampleCount,
                                      menu_opt_profile_catSampleRate, 80);
                    break;
                case MENU_PROFILE_UNIMOG:
                    sound_play_sample(menu_opt_profile_unimogSamples, menu_opt_profile_unimogSampleCount,
                                      menu_opt_profile_unimogSampleRate, 80);
                    break;
                case MENU_PROFILE_MAN:
                    sound_play_sample(menu_opt_profile_manSamples, menu_opt_profile_manSampleCount,
                                      menu_opt_profile_manSampleRate, 80);
                    break;
            }
            break;

        case MENU_CAT_HORN:
            switch (opt) {
                case MENU_HORN_TRUCK:
                    sound_play_sample(menu_opt_horn_truckSamples, menu_opt_horn_truckSampleCount,
                                      menu_opt_horn_truckSampleRate, 80);
                    break;
                case MENU_HORN_MANTGE:
                    sound_play_sample(menu_opt_horn_mantgeSamples, menu_opt_horn_mantgeSampleCount,
                                      menu_opt_horn_mantgeSampleRate, 80);
                    break;
                case MENU_HORN_CUCARACHA:
                    sound_play_sample(menu_opt_horn_cucarachaSamples, menu_opt_horn_cucarachaSampleCount,
                                      menu_opt_horn_cucarachaSampleRate, 80);
                    break;
                case MENU_HORN_2TONE:
                    sound_play_sample(menu_opt_horn_2toneSamples, menu_opt_horn_2toneSampleCount,
                                      menu_opt_horn_2toneSampleRate, 80);
                    break;
                case MENU_HORN_DIXIE:
                    sound_play_sample(menu_opt_horn_dixieSamples, menu_opt_horn_dixieSampleCount,
                                      menu_opt_horn_dixieSampleRate, 80);
                    break;
                case MENU_HORN_PETERBILT:
                    sound_play_sample(menu_opt_horn_peterbiltSamples, menu_opt_horn_peterbiltSampleCount,
                                      menu_opt_horn_peterbiltSampleRate, 80);
                    break;
                case MENU_HORN_OUTLAW:
                    sound_play_sample(menu_opt_horn_outlawSamples, menu_opt_horn_outlawSampleCount,
                                      menu_opt_horn_outlawSampleRate, 80);
                    break;
            }
            break;

        case MENU_CAT_WIFI:
            if (opt == MENU_WIFI_ON) {
                sound_play_sample(menu_opt_wifi_onSamples, menu_opt_wifi_onSampleCount,
                                  menu_opt_wifi_onSampleRate, 80);
            } else {
                sound_play_sample(menu_opt_wifi_offSamples, menu_opt_wifi_offSampleCount,
                                  menu_opt_wifi_offSampleRate, 80);
            }
            break;
    }
}

/**
 * @brief Get current option for a category
 */
static uint8_t get_current_option(uint8_t cat)
{
    switch (cat) {
        case MENU_CAT_VOLUME:
            // Use the new preset index function
            return engine_sound_get_current_volume_preset_index();

        case MENU_CAT_PROFILE:
            return (uint8_t)engine_sound_get_profile();

        case MENU_CAT_HORN: {
            const engine_sound_config_t *cfg = engine_sound_get_config();
            return (uint8_t)cfg->horn_type;
        }

        case MENU_CAT_WIFI:
            return web_server_wifi_is_enabled() ? MENU_WIFI_ON : MENU_WIFI_OFF;

        default:
            return 0;
    }
}

/**
 * @brief Get option count for a category
 */
static uint8_t get_option_count(uint8_t cat)
{
    switch (cat) {
        case MENU_CAT_VOLUME:
            return MENU_VOL_COUNT;
        case MENU_CAT_PROFILE:
            return MENU_PROFILE_COUNT;
        case MENU_CAT_HORN:
            return MENU_HORN_COUNT;
        case MENU_CAT_WIFI:
            return MENU_WIFI_COUNT;
        default:
            return 1;
    }
}

/**
 * @brief Apply selected option
 */
static void apply_option(uint8_t cat, uint8_t opt)
{
    switch (cat) {
        case MENU_CAT_VOLUME: {
            uint8_t new_vol = engine_sound_get_volume_preset(opt);
            ESP_LOGI(TAG, "Setting volume to preset %d (%d%%)", opt, new_vol);

            // Use the new preset function which handles NVS save
            engine_sound_set_volume_preset(opt);
            break;
        }

        case MENU_CAT_PROFILE: {
            sound_profile_t profile = (sound_profile_t)opt;
            ESP_LOGI(TAG, "Setting profile to %d", opt);
            engine_sound_set_profile(profile);

            // Save to NVS
            const engine_sound_config_t *current = engine_sound_get_config();
            nvs_save_sound_config(current, sizeof(engine_sound_config_t));
            break;
        }

        case MENU_CAT_HORN: {
            horn_type_t horn = (horn_type_t)opt;
            const char *horn_names[] = {"Truck", "MAN TGE", "La Cucaracha", "2-Tone", "Dixie", "Peterbilt", "Outlaw"};
            ESP_LOGI(TAG, "Setting horn to %s", horn_names[opt]);

            // Get current config, update horn type, save
            const engine_sound_config_t *current = engine_sound_get_config();
            engine_sound_config_t new_config = *current;
            new_config.horn_type = horn;
            engine_sound_set_config(&new_config);
            nvs_save_sound_config(&new_config, sizeof(engine_sound_config_t));
            break;
        }

        case MENU_CAT_WIFI:
            if (opt == MENU_WIFI_ON) {
                ESP_LOGI(TAG, "Enabling WiFi");
                web_server_wifi_enable();
            } else {
                ESP_LOGI(TAG, "Disabling WiFi");
                web_server_wifi_disable();
            }
            break;
    }
}

// ============================================================================
// Public API
// ============================================================================

esp_err_t menu_init(void)
{
    ESP_LOGI(TAG, "Initializing menu system");

    state = MENU_STATE_INACTIVE;
    category_index = 0;
    option_index = 0;

    // Register long-press callback with mode_switch
    mode_switch_set_longpress_callback(on_longpress_callback, MENU_LONGPRESS_MS);

    ESP_LOGI(TAG, "Menu initialized (long-press threshold: %d ms)", MENU_LONGPRESS_MS);
    return ESP_OK;
}

void menu_update(bool aux2_pressed)
{
    int64_t now_ms = esp_timer_get_time() / 1000;

    // Skip processing if menu is inactive
    if (state == MENU_STATE_INACTIVE) {
        // Just track button state for edge detection
        aux2_was_pressed = aux2_pressed;
        return;
    }

    // Check for timeout
    if ((now_ms - last_activity_time) >= MENU_TIMEOUT_MS) {
        ESP_LOGI(TAG, "Menu timeout");
        exit_menu(true);
        aux2_was_pressed = aux2_pressed;
        return;
    }

    // Handle AUX2 button
    bool aux2_rising_edge = aux2_pressed && !aux2_was_pressed;
    bool aux2_falling_edge = !aux2_pressed && aux2_was_pressed;

    if (aux2_rising_edge) {
        // Button just pressed
        aux2_press_start = now_ms;
        aux2_longpress_handled = false;
    }

    if (aux2_pressed && !aux2_longpress_handled) {
        // Check for long-press while held
        if ((now_ms - aux2_press_start) >= MENU_LONGPRESS_MS) {
            aux2_longpress_handled = true;
            last_activity_time = now_ms;

            if (state == MENU_STATE_LEVEL1) {
                // Long-press in Level 1: exit menu
                ESP_LOGI(TAG, "Long-press in Level 1 -> exit");
                exit_menu(true);
            } else if (state == MENU_STATE_LEVEL2) {
                // Long-press in Level 2: back to Level 1
                ESP_LOGI(TAG, "Long-press in Level 2 -> back to Level 1");
                state = MENU_STATE_LEVEL1;
                // Play "Back" TTS
                sound_play_sample(menu_menu_backSamples, menu_menu_backSampleCount,
                                  menu_menu_backSampleRate, 80);
                vTaskDelay(pdMS_TO_TICKS(200));
                play_category_sound(category_index);
            }
        }
    }

    if (aux2_falling_edge && !aux2_longpress_handled) {
        // Short press (released before long-press threshold)
        if ((now_ms - aux2_press_start) >= MENU_DEBOUNCE_MS) {
            last_activity_time = now_ms;

            if (state == MENU_STATE_LEVEL1) {
                // Cycle to next category
                category_index = (category_index + 1) % MENU_CAT_COUNT;
                const char *cat_names[] = {"Volume", "Profile", "Horn", "WiFi"};
                ESP_LOGI(TAG, "Category: %s (%d beeps)", cat_names[category_index], category_index + 1);
                play_category_sound(category_index);
            } else if (state == MENU_STATE_LEVEL2) {
                // Cycle to next option
                uint8_t count = get_option_count(category_index);
                option_index = (option_index + 1) % count;

                // Log with readable option name
                const char *opt_name;
                if (category_index == MENU_CAT_VOLUME) {
                    opt_name = option_index == MENU_VOL_LOW ? "Low" :
                               option_index == MENU_VOL_MEDIUM ? "Medium" : "High";
                } else if (category_index == MENU_CAT_PROFILE) {
                    opt_name = option_index == MENU_PROFILE_CAT ? "CAT 3408" :
                               option_index == MENU_PROFILE_UNIMOG ? "Unimog" : "MAN TGX";
                } else if (category_index == MENU_CAT_HORN) {
                    const char *horn_names[] = {"Truck", "MAN TGE", "La Cucaracha", "2-Tone", "Dixie", "Peterbilt", "Outlaw"};
                    opt_name = horn_names[option_index < MENU_HORN_COUNT ? option_index : 0];
                } else {
                    opt_name = option_index == MENU_WIFI_ON ? "On" : "Off";
                }
                ESP_LOGI(TAG, "Option: %s (%d beeps)", opt_name, option_index + 1);
                play_option_sound(category_index, option_index);
            }
        }
    }

    aux2_was_pressed = aux2_pressed;
}

void menu_handle_confirm(bool aux1_pressed)
{
    // Skip if menu inactive
    if (state == MENU_STATE_INACTIVE) {
        aux1_was_pressed = aux1_pressed;
        return;
    }

    int64_t now_ms = esp_timer_get_time() / 1000;

    // Detect rising edge
    bool aux1_rising_edge = aux1_pressed && !aux1_was_pressed;

    if (aux1_rising_edge) {
        last_activity_time = now_ms;

        if (state == MENU_STATE_LEVEL1) {
            // Enter selected category
            state = MENU_STATE_LEVEL2;
            option_index = get_current_option(category_index);

            const char *cat_names[] = {"Volume", "Profile", "Horn", "WiFi"};
            ESP_LOGI(TAG, "=== ENTERING %s (Level 2, option %d) ===", cat_names[category_index], option_index);

            // Play current option TTS (no need for transition sound with TTS)
            play_option_sound(category_index, option_index);
        } else if (state == MENU_STATE_LEVEL2) {
            // Confirm selection - log with readable names
            const char *cat_names[] = {"Volume", "Profile", "Horn", "WiFi"};
            const char *opt_name;
            if (category_index == MENU_CAT_VOLUME) {
                opt_name = option_index == MENU_VOL_LOW ? "Low" :
                           option_index == MENU_VOL_MEDIUM ? "Medium" : "High";
            } else if (category_index == MENU_CAT_PROFILE) {
                opt_name = option_index == MENU_PROFILE_CAT ? "CAT 3408" :
                           option_index == MENU_PROFILE_UNIMOG ? "Unimog" : "MAN TGX";
            } else if (category_index == MENU_CAT_HORN) {
                const char *horn_names[] = {"Truck", "MAN TGE", "La Cucaracha", "2-Tone", "Dixie", "Peterbilt", "Outlaw"};
                opt_name = horn_names[option_index < MENU_HORN_COUNT ? option_index : 0];
            } else {
                opt_name = option_index == MENU_WIFI_ON ? "On" : "Off";
            }
            ESP_LOGI(TAG, "=== CONFIRMED: %s -> %s ===", cat_names[category_index], opt_name);

            // Apply the setting
            apply_option(category_index, option_index);

            // Play "OK" TTS and exit
            sound_play_sample(menu_menu_confirmSamples, menu_menu_confirmSampleCount,
                              menu_menu_confirmSampleRate, 80);
            exit_menu(false);  // false = not cancelled
        }
    }

    aux1_was_pressed = aux1_pressed;
}

bool menu_is_active(void)
{
    return state != MENU_STATE_INACTIVE;
}

menu_state_t menu_get_state(void)
{
    return state;
}

uint8_t menu_get_category(void)
{
    return category_index;
}

uint8_t menu_get_option(void)
{
    return option_index;
}

void menu_force_exit(void)
{
    if (state != MENU_STATE_INACTIVE) {
        ESP_LOGI(TAG, "Force exit menu");
        state = MENU_STATE_INACTIVE;
        category_index = 0;
        option_index = 0;
        mode_switch_set_enabled(true);
        // No sound on force exit (signal loss scenario)
    }
}
