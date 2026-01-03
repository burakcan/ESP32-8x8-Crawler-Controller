/**
 * @file engine_sound.c
 * @brief Realistic engine sound simulation for 8x8 Crawler
 *
 * Based on the Rc_Engine_Sound_ESP32 project architecture.
 * Features:
 * - Variable pitch engine sound based on RPM
 * - Idle/Rev sound crossfading
 * - Diesel knock overlay
 * - Engine start sound
 * - Jake brake sound
 *
 * The engine sound runs in a dedicated FreeRTOS task, mixing
 * samples at variable rates to simulate RPM changes.
 */

#include "engine_sound.h"
#include "config.h"
#include "sound.h"
#include "nvs_storage.h"
#include "tuning.h"

#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_timer.h"

// Sound profiles system
#include "sounds/sound_profiles.h"

// Sound effect samples
#include "sounds/effects/air_brake.h"
#include "sounds/effects/reverse_beep.h"
#include "sounds/effects/gear_shift.h"
#include "sounds/effects/turbo_whistle.h"
#include "sounds/effects/wastegate.h"

static const char *TAG = "ENGINE_SND";

// Current sound profile
static const sound_profile_def_t *current_profile = NULL;

// Audio parameters - use 22050 Hz to match source samples
#define ENGINE_SAMPLE_RATE      22050
#define ENGINE_BITS_PER_SAMPLE  16
#define ENGINE_BUFFER_SIZE      512   // Match sound.c DMA frame size

// RPM parameters
#define IDLE_RPM                100     // Base idle RPM (normalized scale)
#define MAX_RPM                 500     // Maximum RPM (normalized scale)

// Default configuration for CAT 3408
static engine_sound_config_t config = {
    .profile = SOUND_PROFILE_CAT_3408,
    .master_volume = 100,
    .idle_volume = 100,
    .rev_volume = 80,         // Rev slightly quieter since it's layered on top
    .knock_volume = 80,       // Reduced - knock is subtle diesel "tick"
    .start_volume = 90,
    .max_rpm_percentage = 300,
    .acceleration = 2,
    .deceleration = 1,
    .rev_switch_point = 120,  // Rev starts mixing above idle (IDLE_RPM=100)
    .idle_end_point = 450,    // Gradual transition - only full rev near max
    .knock_start_point = 150, // Only knock above this RPM (not at idle)
    .knock_interval = 8,      // V8 engine = 8 cylinders
    .jake_brake_enabled = true,
    .v8_mode = true,
    // Sound effects defaults - all enabled at 70% volume
    .air_brake_enabled = true,
    .air_brake_volume = 70,
    .reverse_beep_enabled = true,
    .reverse_beep_volume = 70,
    .gear_shift_enabled = true,
    .gear_shift_volume = 70,
    .turbo_enabled = true,
    .turbo_volume = 70,
    .wastegate_enabled = true,
    .wastegate_volume = 70
};

// Engine state
static engine_state_t engine_state = ENGINE_OFF;
static bool engine_enabled = false;
static bool engine_task_running = false;
static TaskHandle_t engine_task_handle = NULL;
static SemaphoreHandle_t engine_mutex = NULL;

// RPM tracking
static volatile uint16_t current_rpm = IDLE_RPM;
static volatile uint16_t target_rpm = IDLE_RPM;
static volatile bool jake_brake_active = false;

// Shutdown state (for gradual engine stop)
static volatile uint8_t shutdown_attenuation = 1;    // Volume divider (1 = full, higher = quieter)
static volatile uint16_t shutdown_speed_pct = 100;   // Speed percentage (100 = normal, higher = slower)

// Transmission simulation (3-speed automatic)
// Gear ratios (x10): reverse, 1st, 2nd, 3rd - based on GM Turbo HydraMatic 400
static const int16_t gear_ratios[4] = {10, 25, 15, 10};  // 1.0, 2.5, 1.5, 1.0
static volatile uint8_t current_gear = 1;                 // Current gear (0=reverse, 1-3=forward)
static volatile int16_t engine_load = 0;                  // 0-180 (throttle - rpm difference)
static volatile int16_t vehicle_speed = 0;                // 0-500 normalized vehicle speed
static volatile int16_t last_throttle = 0;                // For wastegate detection
static int64_t last_upshift_time = 0;                     // Shift lockout timer
static int64_t last_downshift_time = 0;                   // Shift lockout timer

// Clutch engaging point - below this speed, RPM follows throttle not speed
#define CLUTCH_ENGAGING_POINT   80

// Throttle-dependent volume (like reference project - smooth fading)
// Volume ranges from idle to full throttle - increased for more output
#define ENGINE_IDLE_VOLUME_PCT  100     // Volume percentage at idle
#define ENGINE_FULL_VOLUME_PCT  250     // Volume percentage at full throttle
#define REV_IDLE_VOLUME_PCT     80      // Rev volume at idle
#define REV_FULL_VOLUME_PCT     220     // Rev volume at full throttle

static volatile int16_t current_throttle_faded = 0;    // Smoothed throttle for volume
static volatile int16_t throttle_dependent_volume = ENGINE_IDLE_VOLUME_PCT;
static volatile int16_t throttle_dependent_rev_volume = REV_IDLE_VOLUME_PCT;

// Gear shift effect (brief power cut and RPM drop like real automatic)
static volatile bool gear_shift_trigger = false;
static volatile uint8_t prev_gear = 1;
static volatile int64_t gear_shift_start_time = 0;
static volatile uint8_t gear_shift_attenuation = 0;  // 0-100, current shift effect intensity
static volatile bool rpm_settled_after_upshift = true;  // Must see low RPM before next upshift

#define GEAR_SHIFT_DURATION_MS  200   // Duration of shift effect

// Sample playback positions (using fixed-point for sub-sample accuracy)
// Format: 16.16 fixed point (upper 16 bits = integer, lower 16 bits = fraction)
static uint32_t idle_sample_pos = 0;
static uint32_t rev_sample_pos = 0;
static uint32_t knock_sample_pos = 0;
static uint32_t jake_sample_pos = 0;
static uint32_t start_sample_pos = 0;

// Sound effect playback state
static bool air_brake_trigger = false;
static uint32_t air_brake_sample_pos = 0;

static bool reverse_beep_playing = false;
static uint32_t reverse_beep_sample_pos = 0;

static bool gear_shift_sound_trigger = false;
static uint32_t gear_shift_sound_sample_pos = 0;

static bool turbo_playing = false;
static uint32_t turbo_sample_pos = 0;
static uint16_t turbo_volume_faded = 0;  // Throttle-dependent turbo volume

static bool wastegate_trigger = false;
static uint32_t wastegate_sample_pos = 0;
static int64_t wastegate_lockout_time = 0;  // Cooldown timer
static int16_t prev_throttle_for_wastegate = 0;  // Track throttle changes

// I2S handle (shared with sound.c - we'll get it from there)
extern i2s_chan_handle_t tx_handle;

/**
 * @brief Get sample from idle sound with interpolation
 */
static inline int16_t get_idle_sample(uint32_t pos_fixed) {
    uint32_t idx = pos_fixed >> 16;
    if (idx >= current_profile->idle.sample_count) {
        idle_sample_pos = 0;
        idx = 0;
    }
    // 8-bit to 16-bit conversion
    return ((int16_t)current_profile->idle.samples[idx]) << 8;
}

/**
 * @brief Get sample from rev sound with interpolation
 */
static inline int16_t get_rev_sample(uint32_t pos_fixed) {
    uint32_t idx = pos_fixed >> 16;
    if (idx >= current_profile->rev.sample_count) {
        rev_sample_pos = 0;
        idx = 0;
    }
    return ((int16_t)current_profile->rev.samples[idx]) << 8;
}

/**
 * @brief Get sample from knock sound
 */
static inline int16_t get_knock_sample(uint32_t pos_fixed) {
    uint32_t idx = pos_fixed >> 16;
    if (idx >= current_profile->knock.sample_count) {
        return 0;  // Knock is one-shot
    }
    return ((int16_t)current_profile->knock.samples[idx]) << 8;
}

/**
 * @brief Get sample from jake brake sound
 */
static inline int16_t get_jake_sample(uint32_t pos_fixed) {
    if (!current_profile->has_jake_brake) {
        return 0;
    }
    uint32_t idx = pos_fixed >> 16;
    if (idx >= current_profile->jake_brake.sample_count) {
        jake_sample_pos = 0;
        idx = 0;
    }
    return ((int16_t)current_profile->jake_brake.samples[idx]) << 8;
}

/**
 * @brief Get sample from start sound
 */
static inline int16_t get_start_sample(uint32_t pos_fixed) {
    uint32_t idx = pos_fixed >> 16;
    if (idx >= current_profile->start.sample_count) {
        return -1;  // Signal end of start sound
    }
    return ((int16_t)current_profile->start.samples[idx]) << 8;
}

/**
 * @brief Calculate sample rate increment based on RPM
 *
 * Higher RPM = faster sample playback = higher pitch
 * At idle RPM (100), we play at normal rate
 * At max RPM (500), we play at max_rpm_percentage/100 times faster
 */
static inline uint32_t calc_sample_increment(uint16_t rpm) {
    // Fixed point increment (16.16)
    // Base increment is 1.0 (0x10000) at idle
    // Scale by RPM relative to idle
    uint32_t increment = ((uint32_t)rpm << 16) / IDLE_RPM;
    return increment;
}

/**
 * @brief Calculate idle volume proportion based on RPM
 *
 * Reference project approach: idle stays at 90% below switch point,
 * then gradually fades to 0% at idle_end_point. Rev is added on top.
 * Returns idle_proportion (0-100%)
 */
static inline uint8_t calc_idle_proportion(uint16_t rpm) {
    const uint8_t IDLE_PROPORTION_MAX = 90;  // Idle is 90% even when rev starts

    if (rpm <= config.rev_switch_point) {
        return IDLE_PROPORTION_MAX;  // 90% idle below switch point
    }
    if (rpm >= config.idle_end_point) {
        return 0;  // 0% idle at max RPM
    }
    // Linear interpolation: 90% at switch point -> 0% at end point
    uint32_t range = config.idle_end_point - config.rev_switch_point;
    uint32_t pos = rpm - config.rev_switch_point;
    return IDLE_PROPORTION_MAX - (uint8_t)((pos * IDLE_PROPORTION_MAX) / range);
}

/**
 * @brief Calculate rev volume proportion based on RPM
 *
 * Rev starts at 0% and increases to 100% as idle decreases.
 * Total can exceed 100% when layering (this is intentional for fuller sound).
 */
static inline uint8_t calc_rev_proportion(uint16_t rpm) {
    if (rpm <= config.rev_switch_point) {
        return 0;  // No rev below switch point
    }
    if (rpm >= config.idle_end_point) {
        return 100;  // 100% rev at max RPM
    }
    // Linear interpolation: 0% at switch point -> 100% at end point
    uint32_t range = config.idle_end_point - config.rev_switch_point;
    uint32_t pos = rpm - config.rev_switch_point;
    return (uint8_t)((pos * 100) / range);
}

// Knock timing state
static uint32_t last_knock_pos = 0;
static uint8_t knock_counter = 0;

/**
 * @brief Check if knock should trigger
 *
 * Knock triggers once per cylinder firing, synchronized with idle loop.
 * For V8: 8 knocks per full idle sample loop.
 */
static inline bool should_trigger_knock(uint16_t rpm) {
    if (rpm < config.knock_start_point) {
        return false;
    }

    // Calculate knock interval in samples
    uint32_t idle_idx = idle_sample_pos >> 16;
    uint32_t knock_interval = current_profile->idle.sample_count / config.knock_interval;

    // Check if we've crossed a knock boundary
    uint32_t current_knock_num = idle_idx / knock_interval;
    uint32_t last_knock_num = last_knock_pos / knock_interval;

    if (current_knock_num != last_knock_num) {
        last_knock_pos = idle_idx;
        knock_counter++;
        return true;
    }

    return false;
}

/**
 * @brief Update RPM with acceleration/deceleration smoothing
 */
static void update_rpm(void) {
    if (current_rpm < target_rpm) {
        // Accelerating
        int16_t delta = (target_rpm - current_rpm) / 10;
        if (delta < config.acceleration) delta = config.acceleration;
        current_rpm += delta;
        if (current_rpm > target_rpm) current_rpm = target_rpm;
    } else if (current_rpm > target_rpm) {
        // Decelerating
        int16_t delta = (current_rpm - target_rpm) / 10;
        if (delta < config.deceleration) delta = config.deceleration;
        current_rpm -= delta;
        if (current_rpm < target_rpm) current_rpm = target_rpm;
    }

    // Clamp RPM
    if (current_rpm < IDLE_RPM) current_rpm = IDLE_RPM;
    uint16_t max = (IDLE_RPM * config.max_rpm_percentage) / 100;
    if (current_rpm > max) current_rpm = max;
}

/**
 * @brief Mix engine sound samples
 *
 * Uses crossfade approach like reference project:
 * - Idle proportion decreases from 90% to 0% as RPM increases
 * - Rev proportion increases from 0% to 100% as RPM increases
 * - Total proportion is always ~100% (crossfade, not pure layering)
 * - Volume is throttle-dependent (louder at higher throttle)
 */
static void mix_engine_samples(int16_t *buffer, size_t num_samples) {
    uint32_t increment = calc_sample_increment(current_rpm);

    // Get crossfade proportions (like reference: a1Multi and 100-a1Multi)
    uint8_t idle_prop = calc_idle_proportion(current_rpm);  // 0-90%
    uint8_t rev_prop = 100 - idle_prop;                     // Inverse for crossfade

    // Apply throttle-dependent volume (key to natural sound!)
    // This makes the engine louder when accelerating, quieter when coasting
    int32_t idle_vol = (config.idle_volume * config.master_volume * throttle_dependent_volume) / 10000;
    int32_t rev_vol = (config.rev_volume * config.master_volume * throttle_dependent_rev_volume) / 10000;
    int32_t knock_vol = (config.knock_volume * config.master_volume * throttle_dependent_volume) / 10000;
    int32_t jake_vol = jake_brake_active ?
                       (180 * config.master_volume) / 100 : 0;  // Jake brake volume

    // Apply crossfade proportions
    idle_vol = (idle_vol * idle_prop) / 100;
    rev_vol = (rev_vol * rev_prop) / 100;

    // Apply gear shift attenuation (brief power cut during shift)
    if (gear_shift_attenuation > 0) {
        // Reduce volume during shift (up to 50% reduction at peak)
        int32_t shift_factor = 100 - (gear_shift_attenuation / 2);
        idle_vol = (idle_vol * shift_factor) / 100;
        rev_vol = (rev_vol * shift_factor) / 100;
    }

    for (size_t i = 0; i < num_samples; i++) {
        int32_t mix = 0;

        // Get idle sample - LAYER (add) not crossfade
        int16_t idle_sample = get_idle_sample(idle_sample_pos);
        mix += ((int32_t)idle_sample * idle_vol) >> 8;

        // Get rev sample - LAYER on top of idle
        int16_t rev_sample = get_rev_sample(rev_sample_pos);
        mix += ((int32_t)rev_sample * rev_vol) >> 8;

        // Advance sample positions
        idle_sample_pos += increment;
        if ((idle_sample_pos >> 16) >= current_profile->idle.sample_count) {
            idle_sample_pos = 0;
            last_knock_pos = 0;  // Reset knock tracking on loop
        }

        rev_sample_pos += increment;
        if ((rev_sample_pos >> 16) >= current_profile->rev.sample_count) {
            rev_sample_pos = 0;
        }

        // Diesel knock overlay - check once per sample
        if (should_trigger_knock(current_rpm)) {
            knock_sample_pos = 0;  // Start new knock
        }

        // Play knock sample if active
        if ((knock_sample_pos >> 16) < current_profile->knock.sample_count) {
            int16_t knock_sample = get_knock_sample(knock_sample_pos);

            // V8 mode: pulses 4 and 8 are louder (cylinders sharing manifold)
            int32_t knock_volume = knock_vol / 4;  // Base knock quieter
            if (config.v8_mode) {
                uint8_t pulse = knock_counter % 8;
                if (pulse == 3 || pulse == 7) {
                    // Louder knock for paired cylinders
                    knock_volume = knock_vol / 2;
                }
            }

            mix += ((int32_t)knock_sample * knock_volume) >> 8;
            knock_sample_pos += 0x10000;  // Normal speed for knock
        }

        // Jake brake sound when decelerating
        if (jake_brake_active && current_rpm > 150 && current_profile->has_jake_brake) {
            int16_t jake_sample = get_jake_sample(jake_sample_pos);
            mix += ((int32_t)jake_sample * jake_vol) >> 8;
            jake_sample_pos += increment;
            if ((jake_sample_pos >> 16) >= current_profile->jake_brake.sample_count) {
                jake_sample_pos = 0;
            }
        }

        // =====================================================================
        // SOUND EFFECTS MIXING
        // =====================================================================

        // Air brake release sound (one-shot, triggered after stop)
        if (air_brake_trigger && config.air_brake_enabled) {
            uint32_t idx = air_brake_sample_pos >> 16;
            if (idx < effect_airBrakeSampleCount) {
                int16_t sample = ((int16_t)effect_airBrakeSamples[idx]) << 8;
                int32_t vol = (config.air_brake_volume * config.master_volume) / 100;
                mix += ((int32_t)sample * vol) >> 8;
                air_brake_sample_pos += 0x10000;  // Normal rate
            } else {
                air_brake_trigger = false;
                air_brake_sample_pos = 0;
            }
        }

        // Reversing beep sound (looping while in reverse)
        if (reverse_beep_playing && config.reverse_beep_enabled) {
            uint32_t idx = reverse_beep_sample_pos >> 16;
            if (idx < effect_reverseBeepSampleCount) {
                int16_t sample = ((int16_t)effect_reverseBeepSamples[idx]) << 8;
                int32_t vol = (config.reverse_beep_volume * config.master_volume) / 100;
                mix += ((int32_t)sample * vol) >> 8;
                reverse_beep_sample_pos += 0x10000;  // Normal rate
            } else {
                reverse_beep_sample_pos = 0;  // Loop
            }
        }

        // Gear shift clunk sound (one-shot on gear change)
        if (gear_shift_sound_trigger && config.gear_shift_enabled) {
            uint32_t idx = gear_shift_sound_sample_pos >> 16;
            if (idx < effect_gearShiftSampleCount) {
                int16_t sample = ((int16_t)effect_gearShiftSamples[idx]) << 8;
                int32_t vol = (config.gear_shift_volume * config.master_volume) / 100;
                mix += ((int32_t)sample * vol) >> 8;
                gear_shift_sound_sample_pos += 0x10000;  // Normal rate
            } else {
                gear_shift_sound_trigger = false;
                gear_shift_sound_sample_pos = 0;
            }
        }

        // Turbo whistle sound (looping, volume varies with throttle)
        if (turbo_playing && config.turbo_enabled) {
            uint32_t idx = turbo_sample_pos >> 16;
            if (idx < effect_turboSampleCount) {
                int16_t sample = ((int16_t)effect_turboSamples[idx]) << 8;
                int32_t vol = (config.turbo_volume * turbo_volume_faded * config.master_volume) / 10000;
                mix += ((int32_t)sample * vol) >> 8;
                turbo_sample_pos += 0x10000;  // Normal rate
            } else {
                turbo_sample_pos = 0;  // Loop
            }
        }

        // Wastegate/blowoff sound (one-shot after rapid throttle drop)
        if (wastegate_trigger && config.wastegate_enabled) {
            uint32_t idx = wastegate_sample_pos >> 16;
            if (idx < effect_wastegateSampleCount) {
                int16_t sample = ((int16_t)effect_wastegateSamples[idx]) << 8;
                // RPM-dependent wastegate volume (louder at higher RPM)
                int32_t rpm_vol = 50 + (current_rpm * 50 / MAX_RPM);  // 50-100%
                int32_t vol = (config.wastegate_volume * rpm_vol * config.master_volume) / 10000;
                mix += ((int32_t)sample * vol) >> 8;
                wastegate_sample_pos += 0x10000;  // Normal rate
            } else {
                wastegate_trigger = false;
                wastegate_sample_pos = 0;
            }
        }

        // Clamp to 16-bit range
        if (mix > 32767) mix = 32767;
        if (mix < -32768) mix = -32768;

        // Stereo output
        buffer[i * 2] = (int16_t)mix;
        buffer[i * 2 + 1] = (int16_t)mix;
    }
}

/**
 * @brief Mix engine shutdown samples (fade out and slow down)
 *
 * Similar to reference project: gradually attenuate volume and slow down pitch
 * to simulate engine winding down.
 */
static void mix_shutdown_samples(int16_t *buffer, size_t num_samples) {
    // Calculate slowed-down sample increment
    // As shutdown_speed_pct increases (100 -> 500), playback gets slower
    uint32_t base_increment = calc_sample_increment(IDLE_RPM);
    uint32_t increment = (base_increment * 100) / shutdown_speed_pct;

    // Base volume with shutdown attenuation
    int32_t idle_vol = (config.idle_volume * config.master_volume) / 100;
    idle_vol = idle_vol / shutdown_attenuation;

    for (size_t i = 0; i < num_samples; i++) {
        // Get idle sample only (no rev, no knock during shutdown)
        int16_t idle_sample = get_idle_sample(idle_sample_pos);
        int32_t mix = ((int32_t)idle_sample * idle_vol) >> 8;

        // Advance sample position (slowed down)
        idle_sample_pos += increment;
        if ((idle_sample_pos >> 16) >= current_profile->idle.sample_count) {
            idle_sample_pos = 0;
        }

        // Clamp to 16-bit range
        if (mix > 32767) mix = 32767;
        if (mix < -32768) mix = -32768;

        // Stereo output
        buffer[i * 2] = (int16_t)mix;
        buffer[i * 2 + 1] = (int16_t)mix;
    }
}

/**
 * @brief Play engine start sound
 */
static esp_err_t play_start_sound(void) {
    ESP_LOGI(TAG, "Playing engine start sound (%lu samples)", current_profile->start.sample_count);

    int16_t *buffer = heap_caps_malloc(ENGINE_BUFFER_SIZE * sizeof(int16_t) * 2, MALLOC_CAP_DMA);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate start sound buffer");
        return ESP_ERR_NO_MEM;
    }

    start_sample_pos = 0;
    size_t bytes_written;
    int32_t vol = (config.start_volume * config.master_volume) / 100;

    while ((start_sample_pos >> 16) < current_profile->start.sample_count) {
        for (size_t i = 0; i < ENGINE_BUFFER_SIZE; i++) {
            int16_t sample = get_start_sample(start_sample_pos);
            if (sample == -1) {
                // End of start sound
                sample = 0;
            }

            int32_t scaled = ((int32_t)sample * vol) >> 8;
            buffer[i * 2] = (int16_t)scaled;
            buffer[i * 2 + 1] = (int16_t)scaled;

            start_sample_pos += 0x10000;  // Normal speed
        }

        esp_err_t ret = i2s_channel_write(tx_handle, buffer,
                                          ENGINE_BUFFER_SIZE * sizeof(int16_t) * 2,
                                          &bytes_written, pdMS_TO_TICKS(500));
        if (ret != ESP_OK) {
            // Timeout during start sound is less critical, just continue
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        // Check for abort
        if (engine_state != ENGINE_STARTING) {
            break;
        }
    }

    free(buffer);
    return ESP_OK;
}

/**
 * @brief Engine sound task
 */
static void engine_sound_task(void *arg) {
    ESP_LOGI(TAG, "Engine sound task started");

    // Wait for sound.c to finish any playback (like boot chime)
    while (sound_is_playing()) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ESP_LOGI(TAG, "Sound system free, engine task ready");

    int16_t *buffer = heap_caps_malloc(ENGINE_BUFFER_SIZE * sizeof(int16_t) * 2, MALLOC_CAP_DMA);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate engine sound buffer");
        engine_task_running = false;
        vTaskDelete(NULL);
        return;
    }

    size_t bytes_written;
    uint32_t rpm_update_counter = 0;

    static int64_t last_shutdown_update = 0;

    while (engine_task_running) {
        // Wait if sound.c is playing something
        if (sound_is_playing()) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (engine_state == ENGINE_RUNNING && engine_enabled) {
            // Update RPM every few iterations for smoother transitions
            if (++rpm_update_counter >= 4) {
                rpm_update_counter = 0;
                update_rpm();
            }

            // Process gear shift effect
            int64_t now_ms = esp_timer_get_time() / 1000;
            if (gear_shift_trigger) {
                gear_shift_trigger = false;
                gear_shift_start_time = now_ms;
                gear_shift_attenuation = 100;  // Start at max attenuation
            }

            // Fade out gear shift effect over GEAR_SHIFT_DURATION_MS
            if (gear_shift_attenuation > 0) {
                int64_t elapsed = now_ms - gear_shift_start_time;
                if (elapsed >= GEAR_SHIFT_DURATION_MS) {
                    gear_shift_attenuation = 0;
                } else {
                    // Linear fade from 100 to 0
                    gear_shift_attenuation = 100 - (uint8_t)((elapsed * 100) / GEAR_SHIFT_DURATION_MS);
                }
            }

            // Mix and output engine sound
            mix_engine_samples(buffer, ENGINE_BUFFER_SIZE);

            esp_err_t ret = i2s_channel_write(tx_handle, buffer,
                                              ENGINE_BUFFER_SIZE * sizeof(int16_t) * 2,
                                              &bytes_written, pdMS_TO_TICKS(500));
            if (ret != ESP_OK) {
                // Only log occasionally to avoid flooding
                static uint32_t error_count = 0;
                if (++error_count % 100 == 1) {
                    ESP_LOGW(TAG, "I2S write error: %s (count=%lu)", esp_err_to_name(ret), error_count);
                }
                vTaskDelay(pdMS_TO_TICKS(5));  // Brief delay on error
            }
        } else if (engine_state == ENGINE_STOPPING) {
            // Gradual engine shutdown with fade-out and slow-down
            int64_t now = esp_timer_get_time() / 1000;

            // Update shutdown parameters every 100ms
            if (now - last_shutdown_update > 100) {
                last_shutdown_update = now;
                shutdown_attenuation++;      // Reduce volume
                shutdown_speed_pct += 15;    // Slow down pitch
            }

            // Check if shutdown is complete
            if (shutdown_attenuation >= 40 || shutdown_speed_pct >= 400) {
                ESP_LOGI(TAG, "Engine stopped");
                engine_state = ENGINE_OFF;
                current_rpm = IDLE_RPM;
                // Reset shutdown state for next time
                shutdown_attenuation = 1;
                shutdown_speed_pct = 100;
                continue;
            }

            // Mix shutdown sound (fading out and slowing down)
            mix_shutdown_samples(buffer, ENGINE_BUFFER_SIZE);

            esp_err_t ret = i2s_channel_write(tx_handle, buffer,
                                              ENGINE_BUFFER_SIZE * sizeof(int16_t) * 2,
                                              &bytes_written, pdMS_TO_TICKS(500));
            if (ret != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(5));
            }
        } else {
            // Engine not running, just wait
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }

    free(buffer);
    ESP_LOGI(TAG, "Engine sound task stopped");
    vTaskDelete(NULL);
}

// ============================================================================
// Public API
// ============================================================================

esp_err_t engine_sound_init(void) {
    if (engine_task_running) {
        ESP_LOGW(TAG, "Engine sound already initialized");
        return ESP_OK;
    }

    // Create mutex
    engine_mutex = xSemaphoreCreateMutex();
    if (!engine_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Try to load saved config from NVS
    engine_sound_config_t saved_config;
    size_t config_len = sizeof(engine_sound_config_t);
    if (nvs_load_sound_config(&saved_config, &config_len) == ESP_OK &&
        config_len == sizeof(engine_sound_config_t)) {
        ESP_LOGI(TAG, "Loaded sound config from NVS");
        memcpy(&config, &saved_config, sizeof(engine_sound_config_t));
    } else {
        ESP_LOGI(TAG, "Using default sound config");
    }

    // Load profile
    current_profile = sound_profiles_get(config.profile);
    if (!current_profile) {
        ESP_LOGE(TAG, "Failed to load sound profile");
        vSemaphoreDelete(engine_mutex);
        return ESP_FAIL;
    }

    // Update knock interval based on profile cylinder count
    config.knock_interval = current_profile->cylinder_count;

    // Initialize state
    engine_state = ENGINE_OFF;
    current_rpm = IDLE_RPM;
    target_rpm = IDLE_RPM;
    engine_enabled = true;

    // Reset sample positions
    idle_sample_pos = 0;
    rev_sample_pos = 0;
    knock_sample_pos = 0;
    jake_sample_pos = 0;

    // Reset effect state
    air_brake_trigger = false;
    air_brake_sample_pos = 0;
    reverse_beep_playing = false;
    reverse_beep_sample_pos = 0;
    gear_shift_sound_trigger = false;
    gear_shift_sound_sample_pos = 0;
    turbo_playing = false;
    turbo_sample_pos = 0;
    turbo_volume_faded = 0;
    wastegate_trigger = false;
    wastegate_sample_pos = 0;
    wastegate_lockout_time = 0;
    prev_throttle_for_wastegate = 0;

    // Create engine sound task
    engine_task_running = true;
    BaseType_t ret = xTaskCreatePinnedToCore(
        engine_sound_task,
        "engine_snd",
        4096,
        NULL,
        5,  // Higher priority for smooth audio
        &engine_task_handle,
        1   // Run on core 1
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create engine sound task");
        engine_task_running = false;
        vSemaphoreDelete(engine_mutex);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Engine sound system initialized");
    ESP_LOGI(TAG, "  Profile: %s (%s)", current_profile->name, current_profile->description);
    ESP_LOGI(TAG, "  Idle samples: %lu @ %lu Hz", current_profile->idle.sample_count, current_profile->idle.sample_rate);
    ESP_LOGI(TAG, "  Rev samples: %lu @ %lu Hz", current_profile->rev.sample_count, current_profile->rev.sample_rate);
    ESP_LOGI(TAG, "  Knock samples: %lu @ %lu Hz", current_profile->knock.sample_count, current_profile->knock.sample_rate);
    ESP_LOGI(TAG, "  Cylinders: %d, Jake brake: %s", current_profile->cylinder_count,
             current_profile->has_jake_brake ? "yes" : "no");

    return ESP_OK;
}

esp_err_t engine_sound_deinit(void) {
    if (!engine_task_running) {
        return ESP_OK;
    }

    // Stop task
    engine_task_running = false;
    engine_state = ENGINE_OFF;

    // Wait for task to finish
    vTaskDelay(pdMS_TO_TICKS(100));

    if (engine_mutex) {
        vSemaphoreDelete(engine_mutex);
        engine_mutex = NULL;
    }

    ESP_LOGI(TAG, "Engine sound system deinitialized");
    return ESP_OK;
}

esp_err_t engine_sound_start(void) {
    if (!engine_task_running) {
        ESP_LOGE(TAG, "Engine sound not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (engine_state == ENGINE_RUNNING) {
        ESP_LOGW(TAG, "Engine already running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting engine...");
    engine_state = ENGINE_STARTING;

    // Play start sound
    play_start_sound();

    // Transition to running
    engine_state = ENGINE_RUNNING;
    current_rpm = IDLE_RPM;
    target_rpm = IDLE_RPM;

    // Reset transmission state
    current_gear = 1;
    engine_load = 0;
    vehicle_speed = 0;
    last_upshift_time = 0;
    last_downshift_time = 0;
    rpm_settled_after_upshift = true;  // Allow first upshift

    ESP_LOGI(TAG, "Engine started (gear 1)");
    return ESP_OK;
}

esp_err_t engine_sound_stop(void) {
    if (engine_state == ENGINE_OFF || engine_state == ENGINE_STOPPING) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping engine (gradual shutdown)...");

    // Reset shutdown state
    shutdown_attenuation = 1;
    shutdown_speed_pct = 100;

    // Trigger the shutdown sequence - the task will handle the fade-out
    engine_state = ENGINE_STOPPING;

    return ESP_OK;
}

void engine_sound_update(int16_t throttle, int16_t speed) {
    if (engine_state != ENGINE_RUNNING) {
        return;
    }

    int64_t now = esp_timer_get_time() / 1000;

    // Debug logging every 2 seconds
    static int64_t last_debug_log = 0;
    bool should_log = (now - last_debug_log) > 2000;

    // =========================================================================
    // INPUT PROCESSING - Detect braking vs accelerating
    // =========================================================================

    // Speed: positive = forward, negative = reverse
    int16_t abs_speed = (speed < 0) ? -speed : speed;
    vehicle_speed = abs_speed / 2;  // 0-1000 -> 0-500

    // Determine movement direction and throttle intent
    bool moving_reverse = (speed < -50);
    bool throttle_neutral = (throttle > -50 && throttle < 50);

    // Get braking state from tuning layer (more accurate, tracks momentum)
    bool is_braking = tuning_is_braking();

    // Calculate effective throttle for engine (0-500)
    // When braking, throttle contribution is zero
    int16_t effective_throttle = 0;
    if (!is_braking && !throttle_neutral) {
        int16_t abs_throttle = (throttle < 0) ? -throttle : throttle;
        effective_throttle = abs_throttle / 2;  // 0-1000 -> 0-500
    }

    // =========================================================================
    // THROTTLE-DEPENDENT VOLUME FADING (like reference project)
    // Smooth volume transitions - key to natural sound!
    // =========================================================================

    // Fade throttle smoothly (increment/decrement by 2 like reference)
    if (!is_braking && current_throttle_faded < effective_throttle && current_throttle_faded < 499) {
        current_throttle_faded += 2;
    }
    if ((current_throttle_faded > effective_throttle || is_braking) && current_throttle_faded > 2) {
        current_throttle_faded -= 2;
    }

    // Calculate throttle-dependent volumes (maps 0-500 throttle to volume range)
    if (!is_braking && engine_state == ENGINE_RUNNING) {
        // Map faded throttle to volume: idle% at 0, full% at 500
        throttle_dependent_volume = ENGINE_IDLE_VOLUME_PCT +
            (current_throttle_faded * (ENGINE_FULL_VOLUME_PCT - ENGINE_IDLE_VOLUME_PCT)) / 500;
        throttle_dependent_rev_volume = REV_IDLE_VOLUME_PCT +
            (current_throttle_faded * (REV_FULL_VOLUME_PCT - REV_IDLE_VOLUME_PCT)) / 500;
    } else {
        // When braking, gradually decrease volume
        if (throttle_dependent_volume > ENGINE_IDLE_VOLUME_PCT) {
            throttle_dependent_volume--;
        }
        if (throttle_dependent_rev_volume > REV_IDLE_VOLUME_PCT) {
            throttle_dependent_rev_volume--;
        }
    }

    // Determine if we're in reverse gear
    // Use tuning layer's direction tracking for more accurate state
    int8_t direction = tuning_get_last_direction();
    bool in_reverse = (direction == -1) || (moving_reverse && !is_braking);

    // =========================================================================
    // ENGINE LOAD CALCULATION
    // =========================================================================

    // Engine load = how hard we're pushing the engine
    // High load = high throttle + low RPM (engine struggling to accelerate)
    // Low load = low throttle OR RPM has caught up to demand
    if (is_braking || throttle_neutral) {
        engine_load = 0;
    } else {
        // Calculate actual max RPM based on config for load calculation
        uint16_t actual_max = (IDLE_RPM * config.max_rpm_percentage) / 100;
        uint16_t actual_range = actual_max - IDLE_RPM;  // Actual usable range

        // Convert throttle (0-500) to actual RPM scale
        int16_t throttle_as_rpm = (effective_throttle * actual_range) / 500;
        int16_t actual_rpm_offset = current_rpm - IDLE_RPM;

        // Load = demand minus current output
        // Positive when throttle demands more than engine is currently giving
        engine_load = throttle_as_rpm - actual_rpm_offset;
        if (engine_load < 0) engine_load = 0;
        if (engine_load > 180) engine_load = 180;
    }

    // =========================================================================
    // AUTOMATIC TRANSMISSION GEAR SELECTION
    // =========================================================================

    // Calculate actual max RPM based on config
    uint16_t max_rpm = (IDLE_RPM * config.max_rpm_percentage) / 100;
    uint16_t rpm_range = max_rpm - IDLE_RPM;  // Usable RPM range

    // Load-dependent shift points (scaled to actual RPM range)
    // Reference: upshift at 78-98% of max, downshift at 30-50% of max
    // Base upshift: 78% of range + idle, max upshift: 98% of range + idle
    int16_t upshift_base = IDLE_RPM + (rpm_range * 78 / 100);   // ~78% of max
    int16_t upshift_max = IDLE_RPM + (rpm_range * 98 / 100);    // ~98% of max
    int16_t upshift_point = upshift_base + ((upshift_max - upshift_base) * engine_load / 180);

    // Base downshift: 30% of range + idle, max downshift: 50% of range + idle
    int16_t downshift_base = IDLE_RPM + (rpm_range * 30 / 100); // ~30% of max
    int16_t downshift_max = IDLE_RPM + (rpm_range * 50 / 100);  // ~50% of max
    int16_t downshift_point = downshift_base + ((downshift_max - downshift_base) * engine_load / 180);

    if (in_reverse) {
        // Reverse - only one gear
        current_gear = 0;
    } else if (current_gear == 0 && !in_reverse) {
        // Coming out of reverse, start in 1st
        current_gear = 1;
    } else {
        // Forward gears with automatic shifting

        // Track if RPM has settled below upshift point since last upshift
        // This prevents back-to-back upshifts when accelerating hard
        // But after 2 seconds, allow upshift anyway (sustained high RPM)
        bool time_override = (now - last_upshift_time) > 2000;
        if (current_rpm < upshift_point - 30) {  // 30 RPM hysteresis
            rpm_settled_after_upshift = true;
        }

        // Upshift: High RPM + low engine load + lockout timers expired
        // Either RPM settled OR enough time passed (sustained high RPM)
        if ((now - last_downshift_time) > 800 &&
            (now - last_upshift_time) > 800 &&
            (rpm_settled_after_upshift || time_override) &&
            current_rpm >= upshift_point &&
            engine_load < 10 &&
            current_gear < 3 &&
            !is_braking) {
            current_gear++;
            last_upshift_time = now;
            gear_shift_trigger = true;  // Trigger gear shift sound
            rpm_settled_after_upshift = false;  // Must settle again before next upshift
            ESP_LOGI(TAG, "Upshift to gear %d (RPM=%d, load=%d)", current_gear, current_rpm, engine_load);
        }

        // Downshift: Low RPM OR high engine load (kickdown) + lockout timers expired
        // Also downshift when braking (engine braking in lower gear)
        // BUT: Don't kickdown if already at/near max RPM (engine is giving all it can)
        bool at_max_rpm = (current_rpm >= (max_rpm - 20));  // Within 20 RPM of max
        bool kickdown_allowed = (engine_load > 100) && !at_max_rpm && (current_gear > 2);  // Only drop to 2nd, not 1st

        if ((now - last_upshift_time) > 800 &&
            (now - last_downshift_time) > 800 &&
            current_gear > 1 &&
            (current_rpm <= downshift_point || kickdown_allowed || is_braking)) {
            current_gear--;
            last_downshift_time = now;
            gear_shift_trigger = true;  // Trigger gear shift sound
            rpm_settled_after_upshift = true;  // Downshift resets upshift settle requirement
            ESP_LOGI(TAG, "Downshift to gear %d (RPM=%d, load=%d, braking=%d, kickdown=%d)",
                     current_gear, current_rpm, engine_load, is_braking, kickdown_allowed ? 1 : 0);
        }
    }

    // =========================================================================
    // RPM CALCULATION
    // =========================================================================

    int16_t new_target_rpm;

    if (is_braking && vehicle_speed < CLUTCH_ENGAGING_POINT) {
        // Braking at very low speed - engine goes to idle
        new_target_rpm = IDLE_RPM;
    } else if (vehicle_speed < CLUTCH_ENGAGING_POINT && !is_braking) {
        // Below clutch engaging point (not braking): RPM follows throttle
        // This allows revving at low speed
        new_target_rpm = IDLE_RPM + (effective_throttle * (MAX_RPM - IDLE_RPM) / 500);
    } else {
        // Above clutch engaging point OR braking: RPM based on vehicle speed and gear ratio
        // RPM = speed * gear_ratio / 10
        int16_t gear_ratio = gear_ratios[current_gear];
        new_target_rpm = (vehicle_speed * gear_ratio / 10);

        // Add torque converter slip only when accelerating (not braking)
        if (!is_braking) {
            int16_t converter_slip = 0;
            if (current_gear <= 1) {
                converter_slip = engine_load * 2;  // More slip in 1st/reverse
            } else {
                converter_slip = engine_load;
            }
            new_target_rpm += converter_slip;
        }

        // Ensure minimum idle RPM
        if (new_target_rpm < IDLE_RPM) {
            new_target_rpm = IDLE_RPM;
        }
    }

    // Clamp to max RPM (max_rpm already calculated above)
    if (new_target_rpm > max_rpm) {
        new_target_rpm = max_rpm;
    }

    target_rpm = new_target_rpm;

    // =========================================================================
    // JAKE BRAKE DETECTION
    // =========================================================================

    // Jake brake: active when braking OR coasting at high RPM while moving
    // This creates the engine braking sound
    bool coasting = throttle_neutral && vehicle_speed > 100;
    if ((is_braking || coasting) && current_rpm > 200 && vehicle_speed > 100) {
        jake_brake_active = config.jake_brake_enabled;
    } else {
        jake_brake_active = false;
    }

    // =========================================================================
    // SOUND EFFECTS TRIGGER DETECTION
    // =========================================================================

    // Air brake: trigger when coming to a stop from moving
    // Triggers when speed drops from >100 to <30
    static int16_t prev_vehicle_speed = 0;
    static int16_t peak_vehicle_speed = 0;  // Track highest speed reached

    // Track peak speed while moving
    if (vehicle_speed > peak_vehicle_speed) {
        peak_vehicle_speed = vehicle_speed;
    }

    // Trigger when stopping after moving at decent speed
    if (peak_vehicle_speed > 100 && vehicle_speed < 30 && !air_brake_trigger) {
        air_brake_trigger = true;
        air_brake_sample_pos = 0;
        ESP_LOGI(TAG, "Air brake triggered (peak: %d, now: %d)", peak_vehicle_speed, vehicle_speed);
        peak_vehicle_speed = 0;  // Reset after triggering
    }

    // Reset peak tracking when moving again
    if (vehicle_speed > 50) {
        // Don't reset peak while moving - let it accumulate
    } else if (vehicle_speed < 10 && !air_brake_trigger) {
        // Only reset peak after fully stopped and air brake done
        peak_vehicle_speed = 0;
    }

    prev_vehicle_speed = vehicle_speed;

    // Reverse beep: play when in reverse and engine is running
    // Reference: loops continuously while escInReverse is true
    if (in_reverse && engine_state == ENGINE_RUNNING) {
        reverse_beep_playing = true;
    } else {
        reverse_beep_playing = false;
        reverse_beep_sample_pos = 0;  // Reset for next time
    }

    // Gear shift clunk: trigger when gear_shift_trigger is set (already done in transmission logic)
    // We reuse the existing gear_shift_trigger but for sound, not the RPM drop effect
    if (gear_shift_trigger && !gear_shift_sound_trigger) {
        gear_shift_sound_trigger = true;
        gear_shift_sound_sample_pos = 0;
        ESP_LOGI(TAG, "Gear shift sound triggered");
    }

    // Turbo whistle: active when throttle is high, volume depends on throttle
    // Reference: turbo sound volume increases with throttle, loops continuously
    if (effective_throttle > 100) {
        turbo_playing = true;
        // Fade turbo volume based on throttle (100-500 throttle maps to 0-100 volume)
        int16_t target_turbo_vol = ((effective_throttle - 100) * 100) / 400;
        if (target_turbo_vol > 100) target_turbo_vol = 100;
        // Smooth fade
        if (turbo_volume_faded < target_turbo_vol) {
            turbo_volume_faded += 2;
            if (turbo_volume_faded > target_turbo_vol) turbo_volume_faded = target_turbo_vol;
        } else if (turbo_volume_faded > target_turbo_vol) {
            turbo_volume_faded -= 2;
            if (turbo_volume_faded < target_turbo_vol) turbo_volume_faded = target_turbo_vol;
        }
    } else {
        // Fade out turbo when throttle is low
        if (turbo_volume_faded > 2) {
            turbo_volume_faded -= 2;
        } else {
            turbo_volume_faded = 0;
        }
        if (turbo_volume_faded == 0) {
            turbo_playing = false;
            turbo_sample_pos = 0;
        }
    }

    // Wastegate/blowoff: trigger after rapid throttle drop while turbo was spooling
    // Triggers when throttle drops by >80 from a high value, with 1 second cooldown
    if (prev_throttle_for_wastegate > 150 &&
        prev_throttle_for_wastegate - effective_throttle > 80 &&
        !wastegate_trigger &&
        (now - wastegate_lockout_time) > 1000) {
        wastegate_trigger = true;
        wastegate_sample_pos = 0;
        wastegate_lockout_time = now;
        prev_throttle_for_wastegate = 0;  // Reset to prevent repeated triggers
        ESP_LOGI(TAG, "Wastegate triggered (throttle: %d -> %d)", prev_throttle_for_wastegate, effective_throttle);
    }

    // Update throttle tracking - only when throttle is high enough to build boost
    if (effective_throttle > 80) {
        prev_throttle_for_wastegate = effective_throttle;
    } else if (effective_throttle < 30) {
        // Reset tracking when fully off throttle (prevents re-trigger on next throttle up)
        prev_throttle_for_wastegate = 0;
    }

    last_throttle = effective_throttle;

    // Debug logging
    if (should_log) {
        last_debug_log = now;
        uint16_t dbg_max_rpm = (IDLE_RPM * config.max_rpm_percentage) / 100;
        uint16_t dbg_rpm_range = dbg_max_rpm - IDLE_RPM;
        int16_t dbg_upshift_base = IDLE_RPM + (dbg_rpm_range * 78 / 100);
        int16_t dbg_upshift_max = IDLE_RPM + (dbg_rpm_range * 98 / 100);
        int16_t upshift_pt = dbg_upshift_base + ((dbg_upshift_max - dbg_upshift_base) * engine_load / 180);
        ESP_LOGI(TAG, "GEAR: g=%d rpm=%d(need>%d) load=%d spd=%d thr=%d brk=%d",
                 current_gear, current_rpm, upshift_pt, engine_load, vehicle_speed,
                 effective_throttle, is_braking ? 1 : 0);
    }
}

void engine_sound_set_rpm(uint16_t rpm) {
    if (rpm < IDLE_RPM) rpm = IDLE_RPM;
    uint16_t max = (IDLE_RPM * config.max_rpm_percentage) / 100;
    if (rpm > max) rpm = max;
    target_rpm = rpm;
}

uint16_t engine_sound_get_rpm(void) {
    return current_rpm;
}

engine_state_t engine_sound_get_state(void) {
    return engine_state;
}

void engine_sound_set_config(const engine_sound_config_t *new_config) {
    if (new_config) {
        xSemaphoreTake(engine_mutex, portMAX_DELAY);
        memcpy(&config, new_config, sizeof(engine_sound_config_t));
        xSemaphoreGive(engine_mutex);
    }
}

const engine_sound_config_t* engine_sound_get_config(void) {
    return &config;
}

void engine_sound_enable(bool enable) {
    engine_enabled = enable;
    if (!enable && engine_state == ENGINE_RUNNING) {
        engine_sound_stop();
    }
}

bool engine_sound_is_enabled(void) {
    return engine_enabled;
}

void engine_sound_set_jake_brake(bool active) {
    jake_brake_active = active && config.jake_brake_enabled;
}

esp_err_t engine_sound_set_profile(sound_profile_t profile) {
    if (profile >= SOUND_PROFILE_COUNT) {
        ESP_LOGE(TAG, "Invalid profile: %d", profile);
        return ESP_ERR_INVALID_ARG;
    }

    const sound_profile_def_t *new_profile = sound_profiles_get(profile);
    if (!new_profile) {
        ESP_LOGE(TAG, "Failed to load profile: %d", profile);
        return ESP_FAIL;
    }

    xSemaphoreTake(engine_mutex, portMAX_DELAY);

    // Update profile
    config.profile = profile;
    current_profile = new_profile;

    // Update knock interval to match cylinder count
    config.knock_interval = current_profile->cylinder_count;

    // Reset sample positions
    idle_sample_pos = 0;
    rev_sample_pos = 0;
    knock_sample_pos = 0;
    jake_sample_pos = 0;
    last_knock_pos = 0;
    knock_counter = 0;

    xSemaphoreGive(engine_mutex);

    ESP_LOGI(TAG, "Switched to profile: %s", current_profile->name);
    return ESP_OK;
}

sound_profile_t engine_sound_get_profile(void) {
    return config.profile;
}

uint8_t engine_sound_get_gear(void) {
    return current_gear;
}

int16_t engine_sound_get_load(void) {
    return engine_load;
}
