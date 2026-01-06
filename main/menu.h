/**
 * @file menu.h
 * @brief 2-Level menu system for RC controller settings
 *
 * Provides a hierarchical menu accessible via long-press on AUX2:
 * - Level 1: Category selection (Volume, Profile, Horn, WiFi)
 * - Level 2: Option selection within each category
 *
 * Navigation:
 * - Long-press AUX2 (1.5s): Enter menu / exit menu / back to L1
 * - Press AUX2: Cycle items at current level
 * - Press AUX1: Enter category (L1) / Confirm option (L2)
 * - Timeout (4s): Auto-exit menu
 */

#ifndef MENU_H
#define MENU_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Menu state enumeration
 */
typedef enum {
    MENU_STATE_INACTIVE = 0,    // Normal operation, menu not active
    MENU_STATE_LEVEL1,          // Category selection level
    MENU_STATE_LEVEL2           // Option selection level
} menu_state_t;

/**
 * @brief Menu category enumeration
 */
typedef enum {
    MENU_CAT_VOLUME = 0,        // Master volume setting
    MENU_CAT_PROFILE,           // Sound profile selection
    MENU_CAT_HORN,              // Horn type selection
    MENU_CAT_WIFI,              // WiFi enable/disable
    MENU_CAT_COUNT
} menu_category_t;

/**
 * @brief Volume level options
 */
typedef enum {
    MENU_VOL_LOW = 0,           // 50%
    MENU_VOL_MEDIUM,            // 100%
    MENU_VOL_HIGH,              // 150%
    MENU_VOL_COUNT
} menu_volume_option_t;

/**
 * @brief Sound profile options (matches sound_profile_t)
 */
typedef enum {
    MENU_PROFILE_CAT = 0,       // CAT 3408
    MENU_PROFILE_UNIMOG,        // Unimog U1000
    MENU_PROFILE_MAN,           // MAN TGX
    MENU_PROFILE_COUNT
} menu_profile_option_t;

/**
 * @brief WiFi options
 */
typedef enum {
    MENU_WIFI_ON = 0,
    MENU_WIFI_OFF,
    MENU_WIFI_COUNT
} menu_wifi_option_t;

/**
 * @brief Horn type options (matches horn_type_t)
 */
typedef enum {
    MENU_HORN_TRUCK = 0,        // Classic truck air horn
    MENU_HORN_MANTGE,           // MAN TGE horn
    MENU_HORN_CUCARACHA,        // La Cucaracha musical horn
    MENU_HORN_2TONE,            // Two-tone truck horn
    MENU_HORN_DIXIE,            // Dixie horn (General Lee)
    MENU_HORN_PETERBILT,        // Peterbilt truck horn
    MENU_HORN_OUTLAW,           // Outlaw train horn
    MENU_HORN_COUNT
} menu_horn_option_t;

/**
 * @brief Initialize the menu system
 *
 * Registers long-press callback with mode_switch module.
 * Must be called after mode_switch_init().
 *
 * @return ESP_OK on success
 */
esp_err_t menu_init(void);

/**
 * @brief Update menu state machine
 *
 * Call this from the main control loop at 100Hz.
 * Handles timeout and button edge detection when menu is active.
 *
 * @param aux2_pressed Current state of AUX2 button
 */
void menu_update(bool aux2_pressed);

/**
 * @brief Handle AUX1 confirm button press
 *
 * In Level 1: Enters selected category
 * In Level 2: Confirms selection and exits menu
 *
 * @param aux1_pressed Current state of AUX1 button
 */
void menu_handle_confirm(bool aux1_pressed);

/**
 * @brief Check if menu is currently active
 *
 * @return true if in LEVEL1 or LEVEL2 state
 */
bool menu_is_active(void);

/**
 * @brief Get current menu state
 *
 * @return Current menu state
 */
menu_state_t menu_get_state(void);

/**
 * @brief Get current category index
 *
 * @return Current category (0 to MENU_CAT_COUNT-1)
 */
uint8_t menu_get_category(void);

/**
 * @brief Get current option index
 *
 * @return Current option within the active category
 */
uint8_t menu_get_option(void);

/**
 * @brief Force exit menu
 *
 * Use this when signal is lost or system needs to reset menu state.
 * Does not apply any pending changes.
 */
void menu_force_exit(void);

#endif // MENU_H
