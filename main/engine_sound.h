/**
 * @file engine_sound.h
 * @brief Realistic engine sound simulation for 8x8 Crawler
 *
 * Based on the Rc_Engine_Sound_ESP32 project architecture.
 * Features:
 * - Variable pitch engine sound based on RPM
 * - Idle/Rev sound crossfading
 * - Diesel knock overlay
 * - Engine start sound
 * - Jake brake sound
 */

#ifndef ENGINE_SOUND_H
#define ENGINE_SOUND_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include "sounds/sound_profiles.h"

/**
 * @brief Engine state machine
 */
typedef enum {
    ENGINE_OFF,
    ENGINE_STARTING,
    ENGINE_RUNNING,
    ENGINE_STOPPING
} engine_state_t;

/**
 * @brief Engine sound configuration
 */
typedef struct {
    // Sound profile selection
    sound_profile_t profile;            // Selected sound profile

    // Volume settings (0-200%)
    uint8_t master_volume;              // Overall volume
    uint8_t idle_volume;                // Idle sound volume
    uint8_t rev_volume;                 // Rev sound volume
    uint8_t knock_volume;               // Diesel knock volume
    uint8_t start_volume;               // Start sound volume

    // Engine characteristics
    uint16_t max_rpm_percentage;        // Max RPM as % of idle (200-400)
    uint8_t acceleration;               // Acceleration rate (1-9)
    uint8_t deceleration;               // Deceleration rate (1-5)

    // Sound mixing points
    uint16_t rev_switch_point;          // RPM where rev starts mixing in
    uint16_t idle_end_point;            // RPM where idle fades out completely
    uint16_t knock_start_point;         // RPM where knock starts
    uint8_t knock_interval;             // Knock timing (usually = cylinder count)

    // Flags
    bool jake_brake_enabled;            // Enable jake brake sound
    bool v8_mode;                       // V8 firing pattern (pulses 4,8 louder)
} engine_sound_config_t;

/**
 * @brief Initialize the engine sound system
 *
 * Must be called after sound_init() since it shares the I2S output.
 *
 * @return ESP_OK on success
 */
esp_err_t engine_sound_init(void);

/**
 * @brief Deinitialize the engine sound system
 */
esp_err_t engine_sound_deinit(void);

/**
 * @brief Start the engine (plays start sound, transitions to running)
 */
esp_err_t engine_sound_start(void);

/**
 * @brief Stop the engine
 */
esp_err_t engine_sound_stop(void);

/**
 * @brief Update engine sound based on throttle input
 *
 * Call this from the main control loop at regular intervals (10-20ms).
 *
 * @param throttle Current throttle value (-1000 to +1000)
 * @param speed Current vehicle speed for clutch simulation (-1000 to +1000)
 */
void engine_sound_update(int16_t throttle, int16_t speed);

/**
 * @brief Set engine RPM directly (for testing)
 *
 * @param rpm RPM value (0-500 normalized scale)
 */
void engine_sound_set_rpm(uint16_t rpm);

/**
 * @brief Get current engine RPM
 *
 * @return Current RPM (0-500 normalized scale)
 */
uint16_t engine_sound_get_rpm(void);

/**
 * @brief Get current engine state
 */
engine_state_t engine_sound_get_state(void);

/**
 * @brief Set engine sound configuration
 */
void engine_sound_set_config(const engine_sound_config_t *config);

/**
 * @brief Get current engine sound configuration
 */
const engine_sound_config_t* engine_sound_get_config(void);

/**
 * @brief Enable/disable engine sound output
 */
void engine_sound_enable(bool enable);

/**
 * @brief Check if engine sound is enabled
 */
bool engine_sound_is_enabled(void);

/**
 * @brief Set jake brake active state
 *
 * @param active true when decelerating with engine braking
 */
void engine_sound_set_jake_brake(bool active);

/**
 * @brief Set sound profile
 * @param profile Profile to switch to
 * @return ESP_OK on success
 */
esp_err_t engine_sound_set_profile(sound_profile_t profile);

/**
 * @brief Get current sound profile
 */
sound_profile_t engine_sound_get_profile(void);

/**
 * @brief Get current gear (0=reverse, 1-3=forward gears)
 */
uint8_t engine_sound_get_gear(void);

/**
 * @brief Get current engine load (0-180)
 */
int16_t engine_sound_get_load(void);

#endif // ENGINE_SOUND_H
