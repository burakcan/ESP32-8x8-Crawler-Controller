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
#include "config.h"
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
    // Menu sounds
    SOUND_MENU_ENTER,       // Rising 3-tone chime (entering menu)
    SOUND_MENU_BACK,        // Single mid tone (back to previous level)
    SOUND_MENU_CONFIRM,     // Happy double-beep (selection confirmed)
    SOUND_MENU_CANCEL,      // Falling tone (menu cancelled/timeout)
    SOUND_BEEP_1,           // 1 beep (category 1 / volume low)
    SOUND_BEEP_2,           // 2 beeps (category 2 / volume medium)
    SOUND_BEEP_3,           // 3 beeps (category 3 / volume high)
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
 * @brief Play a distinctive beep for steering mode change
 *
 * Different beep patterns for each steering mode so user can identify
 * the mode by sound alone:
 * - Front: Single high beep
 * - All-axle: Rising two-tone
 * - Crab: Three quick beeps
 * - Rear: Low beep
 *
 * @param mode The steering mode to announce
 * @return ESP_OK on success
 */
esp_err_t sound_play_mode_beep(steering_mode_t mode);

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

/**
 * @brief Play an 8-bit signed sample array
 *
 * Plays audio from a signed 8-bit sample array (like TTS or WAV data).
 * Converts to 16-bit stereo for I2S output.
 *
 * @param samples Pointer to signed 8-bit sample array
 * @param sample_count Number of samples
 * @param sample_rate Sample rate in Hz (e.g., 22050)
 * @param volume Volume level 0-100
 * @return ESP_OK on success
 */
esp_err_t sound_play_sample(const int8_t *samples, uint32_t sample_count,
                            uint32_t sample_rate, uint8_t volume);

#endif // SOUND_H
