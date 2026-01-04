/**
 * @file sound.h
 * @brief Sound system for 8x8 Crawler Controller
 *
 * I2S audio output driver for MAX98357A amplifier.
 * Provides boot chime and sound effect playback.
 */

#ifndef SOUND_H
#define SOUND_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Sound effect identifiers
 */
typedef enum {
    SOUND_BOOT_CHIME,       // Startup chime
    SOUND_WIFI_ON,          // WiFi enabled
    SOUND_WIFI_OFF,         // WiFi disabled
    SOUND_CALIBRATION,      // Calibration mode
    SOUND_ERROR,            // Error beep
    SOUND_MODE_CHANGE,      // Steering mode changed
    SOUND_COUNT
} sound_effect_t;

/**
 * @brief Initialize the sound system
 *
 * Configures I2S peripheral for MAX98357A amplifier output.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sound_init(void);

/**
 * @brief Deinitialize the sound system
 *
 * Releases I2S resources.
 *
 * @return ESP_OK on success
 */
esp_err_t sound_deinit(void);

/**
 * @brief Play a sound effect (non-blocking)
 *
 * Starts playback of the specified sound effect.
 * If a sound is already playing, it will be interrupted.
 *
 * @param effect Sound effect to play
 * @return ESP_OK on success
 */
esp_err_t sound_play(sound_effect_t effect);

/**
 * @brief Play the boot chime (blocking)
 *
 * Plays the startup chime and waits for completion.
 * Call this during initialization.
 *
 * @return ESP_OK on success
 */
esp_err_t sound_play_boot_chime(void);

/**
 * @brief Play a tone at specified frequency
 *
 * @param frequency_hz Tone frequency in Hz
 * @param duration_ms Duration in milliseconds
 * @param volume Volume level 0-100
 * @return ESP_OK on success
 */
esp_err_t sound_play_tone(uint32_t frequency_hz, uint32_t duration_ms, uint8_t volume);

/**
 * @brief Stop any currently playing sound
 *
 * @return ESP_OK on success
 */
esp_err_t sound_stop(void);

/**
 * @brief Check if sound is currently playing
 *
 * @return true if sound is playing
 */
bool sound_is_playing(void);

/**
 * @brief Set master volume
 *
 * @param volume Volume level 0-100
 */
void sound_set_volume(uint8_t volume);

/**
 * @brief Get current master volume
 *
 * @return Current volume level 0-100
 */
uint8_t sound_get_volume(void);

#endif // SOUND_H
