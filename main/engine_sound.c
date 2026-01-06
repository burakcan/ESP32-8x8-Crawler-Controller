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
#include "perf_metrics.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"

// Sound profiles system
#include "sounds/sound_profiles.h"

// Sound effect samples
#include "sounds/effects/air_brake.h"
#include "sounds/effects/reverse_beep.h"
#include "sounds/effects/gear_shift.h"
#include "sounds/effects/wastegate.h"
#include "sounds/effects/truck_horn.h"
#include "sounds/effects/mantge_horn.h"
#include "sounds/effects/la_cucaracha.h"
#include "sounds/effects/horn_2tone.h"
#include "sounds/effects/horn_dixie.h"
#include "sounds/effects/horn_peterbilt.h"
#include "sounds/effects/horn_outlaw.h"
#include "sounds/mode_switch_sound.h"

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
    .magic = SOUND_CONFIG_MAGIC,
    .version = SOUND_CONFIG_VERSION,
    .profile = SOUND_PROFILE_CAT_3408,
    .master_volume_level1 = 100,    // Normal volume
    .master_volume_level2 = 50,     // Quiet mode (half volume)
    .active_volume_level = 0,       // Start on level 1
    .volume_preset_low = 20,        // Menu preset: Low
    .volume_preset_medium = 100,    // Menu preset: Medium
    .volume_preset_high = 170,      // Menu preset: High
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
    .wastegate_enabled = true,
    .wastegate_volume = 70,
    // Horn settings
    .horn_enabled = true,
    .horn_type = HORN_TYPE_TRUCK,
    .horn_volume = 80,
    // Mode switch sound settings
    .mode_switch_sound_enabled = true,
    .mode_switch_volume = 80
};

// Engine state
static engine_state_t engine_state = ENGINE_OFF;
static bool engine_enabled = false;
static bool engine_task_running = false;
TaskHandle_t engine_task_handle = NULL;
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

// Sound effect playback state
static bool air_brake_trigger = false;
static uint32_t air_brake_sample_pos = 0;

static bool reverse_beep_playing = false;
static uint32_t reverse_beep_sample_pos = 0;

static bool gear_shift_sound_trigger = false;
static uint32_t gear_shift_sound_sample_pos = 0;


static bool wastegate_trigger = false;
static uint32_t wastegate_sample_pos = 0;
static int64_t wastegate_lockout_time = 0;  // Cooldown timer
static int16_t prev_throttle_for_wastegate = 0;  // Track throttle changes

// Mode switch sound (air shift sound for steering mode changes)
static bool mode_switch_trigger = false;
static uint32_t mode_switch_sample_pos = 0;

// Horn sound (looping while button held)
static bool horn_active = false;
static uint32_t horn_sample_pos = 0;

// I2S handle (shared with sound.c - we'll get it from there)
extern i2s_chan_handle_t tx_handle;

// Deferred NVS write (avoid blocking audio hot path)
#define NVS_DEBOUNCE_MS 500
static esp_timer_handle_t nvs_save_timer = NULL;
static volatile bool nvs_config_dirty = false;

/**
 * @brief Timer callback to perform deferred NVS write
 *
 * Called from esp_timer context (not ISR) after debounce period.
 * Safe to perform blocking NVS operations here.
 */
static void nvs_save_timer_callback(void *arg) {
    (void)arg;
    if (nvs_config_dirty) {
        nvs_config_dirty = false;
        esp_err_t ret = nvs_save_sound_config(&config, sizeof(engine_sound_config_t));
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Sound config saved to NVS (deferred)");
        } else {
            ESP_LOGE(TAG, "Failed to save sound config: %s", esp_err_to_name(ret));
        }
    }
}

/**
 * @brief Schedule a deferred NVS config save
 *
 * Marks config as dirty and (re)starts the debounce timer.
 * If called multiple times within the debounce period, only one
 * NVS write occurs after the final call + debounce delay.
 */
static void schedule_nvs_save(void) {
    nvs_config_dirty = true;
    if (nvs_save_timer) {
        // Stop any pending timer and restart with fresh debounce period
        esp_timer_stop(nvs_save_timer);
        esp_timer_start_once(nvs_save_timer, NVS_DEBOUNCE_MS * 1000);
    }
}

/**
 * @brief Get the currently active master volume
 *
 * Returns volume from level 1 or level 2 based on active_volume_level setting.
 * This is used for all volume calculations in the engine sound system.
 */
static inline uint8_t get_master_volume(void)
{
    return config.active_volume_level == 0 ?
           config.master_volume_level1 : config.master_volume_level2;
}

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
    int32_t idle_vol = (config.idle_volume * get_master_volume() * throttle_dependent_volume) / 10000;
    int32_t rev_vol = (config.rev_volume * get_master_volume() * throttle_dependent_rev_volume) / 10000;
    int32_t knock_vol = (config.knock_volume * get_master_volume() * throttle_dependent_volume) / 10000;
    int32_t jake_vol = jake_brake_active ?
                       (180 * get_master_volume()) / 100 : 0;  // Jake brake volume

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
                int32_t vol = (config.air_brake_volume * get_master_volume()) / 100;
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
                int32_t vol = (config.reverse_beep_volume * get_master_volume()) / 100;
                mix += ((int32_t)sample * vol) >> 8;
                reverse_beep_sample_pos += 0x10000;  // Normal rate
            } else {
                reverse_beep_sample_pos = 0;  // Loop
            }
        }

        // Gear shift clunk sound (one-shot on gear change)
        // Use profile-specific sound if available, otherwise generic fallback
        if (gear_shift_sound_trigger && config.gear_shift_enabled) {
            const signed char *shift_samples;
            uint32_t shift_count;

            // Profile-based effect lookup with generic fallback
            if (current_profile->shifting.samples != NULL) {
                shift_samples = (const signed char*)current_profile->shifting.samples;
                shift_count = current_profile->shifting.sample_count;
            } else {
                shift_samples = effect_gearShiftSamples;
                shift_count = effect_gearShiftSampleCount;
            }

            uint32_t idx = gear_shift_sound_sample_pos >> 16;
            if (idx < shift_count) {
                int16_t sample = ((int16_t)shift_samples[idx]) << 8;
                int32_t vol = (config.gear_shift_volume * get_master_volume()) / 100;
                mix += ((int32_t)sample * vol) >> 8;
                gear_shift_sound_sample_pos += 0x10000;  // Normal rate
            } else {
                gear_shift_sound_trigger = false;
                gear_shift_sound_sample_pos = 0;
            }
        }

        // Wastegate/blowoff sound (one-shot after rapid throttle drop)
        // Use profile-specific sound if available, otherwise generic fallback
        if (wastegate_trigger && config.wastegate_enabled) {
            const signed char *wg_samples;
            uint32_t wg_count;

            // Profile-based effect lookup with generic fallback
            if (current_profile->wastegate.samples != NULL) {
                wg_samples = (const signed char*)current_profile->wastegate.samples;
                wg_count = current_profile->wastegate.sample_count;
            } else {
                wg_samples = effect_wastegateSamples;
                wg_count = effect_wastegateSampleCount;
            }

            uint32_t idx = wastegate_sample_pos >> 16;
            if (idx < wg_count) {
                int16_t sample = ((int16_t)wg_samples[idx]) << 8;
                // RPM-dependent wastegate volume (louder at higher RPM)
                int32_t rpm_vol = 50 + (current_rpm * 50 / MAX_RPM);  // 50-100%
                int32_t vol = (config.wastegate_volume * rpm_vol * get_master_volume()) / 10000;
                mix += ((int32_t)sample * vol) >> 8;
                wastegate_sample_pos += 0x10000;  // Normal rate
            } else {
                wastegate_trigger = false;
                wastegate_sample_pos = 0;
            }
        }

        // Mode switch sound (one-shot air shift sound for steering mode changes)
        if (mode_switch_trigger && config.mode_switch_sound_enabled) {
            uint32_t idx = mode_switch_sample_pos >> 16;
            if (idx < modeSwitchSampleCount) {
                int16_t sample = ((int16_t)modeSwitchSamples[idx]) << 8;
                int32_t vol = (config.mode_switch_volume * get_master_volume()) / 100;
                mix += ((int32_t)sample * vol) >> 8;
                mode_switch_sample_pos += 0x10000;  // Normal rate
            } else {
                mode_switch_trigger = false;
                mode_switch_sample_pos = 0;
            }
        }

        // Horn sound (looping while button held)
        if (horn_active && config.horn_enabled) {
            const signed char *horn_samples;
            uint32_t horn_count, horn_loop_begin, horn_loop_end;

            // Select horn type
            switch (config.horn_type) {
                case HORN_TYPE_MANTGE:
                    horn_samples = mantgeHornSamples;
                    horn_count = mantgeHornSampleCount;
                    horn_loop_begin = mantgeHornLoopBegin;
                    horn_loop_end = mantgeHornLoopEnd;
                    break;
                case HORN_TYPE_CUCARACHA:
                    horn_samples = cucarachaSamples;
                    horn_count = cucarachaSampleCount;
                    horn_loop_begin = cucarachaLoopBegin;
                    horn_loop_end = cucarachaLoopEnd;
                    break;
                case HORN_TYPE_2TONE:
                    horn_samples = horn2ToneSamples;
                    horn_count = horn2ToneSampleCount;
                    horn_loop_begin = horn2ToneLoopBegin;
                    horn_loop_end = horn2ToneLoopEnd;
                    break;
                case HORN_TYPE_DIXIE:
                    horn_samples = hornDixieSamples;
                    horn_count = hornDixieSampleCount;
                    horn_loop_begin = hornDixieLoopBegin;
                    horn_loop_end = hornDixieLoopEnd;
                    break;
                case HORN_TYPE_PETERBILT:
                    horn_samples = hornPeterbiltSamples;
                    horn_count = hornPeterbiltSampleCount;
                    horn_loop_begin = hornPeterbiltLoopBegin;
                    horn_loop_end = hornPeterbiltLoopEnd;
                    break;
                case HORN_TYPE_OUTLAW:
                    horn_samples = hornOutlawSamples;
                    horn_count = hornOutlawSampleCount;
                    horn_loop_begin = hornOutlawLoopBegin;
                    horn_loop_end = hornOutlawLoopEnd;
                    break;
                default:  // HORN_TYPE_TRUCK
                    horn_samples = truckHornSamples;
                    horn_count = truckHornSampleCount;
                    horn_loop_begin = truckHornLoopBegin;
                    horn_loop_end = truckHornLoopEnd;
                    break;
            }

            uint32_t idx = horn_sample_pos >> 16;
            if (idx < horn_count) {
                int16_t sample = ((int16_t)horn_samples[idx]) << 8;
                int32_t vol = (config.horn_volume * get_master_volume()) / 100;
                mix += ((int32_t)sample * vol) >> 8;
                horn_sample_pos += 0x10000;  // Normal rate

                // Loop within the loop region
                uint32_t next_idx = horn_sample_pos >> 16;
                if (next_idx >= horn_loop_end) {
                    horn_sample_pos = horn_loop_begin << 16;
                }
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
    int32_t idle_vol = (config.idle_volume * get_master_volume()) / 100;
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
 *
 * Note: Uses simple integer index instead of fixed-point to avoid overflow
 * with long start sounds (the start sound plays at normal speed anyway).
 */
static esp_err_t play_start_sound(void) {
    ESP_LOGI(TAG, "Playing engine start sound (%lu samples)", current_profile->start.sample_count);

    int16_t *buffer = heap_caps_malloc(ENGINE_BUFFER_SIZE * sizeof(int16_t) * 2, MALLOC_CAP_DMA);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate start sound buffer");
        return ESP_ERR_NO_MEM;
    }

    uint32_t sample_idx = 0;  // Simple integer index (no fixed-point needed)
    size_t bytes_written;
    int32_t vol = (config.start_volume * get_master_volume()) / 100;
    uint32_t sample_count = current_profile->start.sample_count;

    while (sample_idx < sample_count) {
        for (size_t i = 0; i < ENGINE_BUFFER_SIZE; i++) {
            int16_t sample;
            if (sample_idx < sample_count) {
                // 8-bit to 16-bit conversion
                sample = ((int16_t)current_profile->start.samples[sample_idx]) << 8;
                sample_idx++;
            } else {
                // Past end - output silence
                sample = 0;
            }

            int32_t scaled = ((int32_t)sample * vol) >> 8;
            buffer[i * 2] = (int16_t)scaled;
            buffer[i * 2 + 1] = (int16_t)scaled;
        }

        // Use ~23ms timeout (512 samples at 22050Hz) instead of 500ms to avoid blocking
        esp_err_t ret = i2s_channel_write(tx_handle, buffer,
                                          ENGINE_BUFFER_SIZE * sizeof(int16_t) * 2,
                                          &bytes_written, pdMS_TO_TICKS(25));
        if (ret != ESP_OK) {
            // Timeout during start sound is less critical, just continue
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        // Feed watchdog - start sound can be several seconds long
        esp_task_wdt_reset();

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

            // Use ~23ms timeout to avoid blocking audio task
            esp_err_t ret = i2s_channel_write(tx_handle, buffer,
                                              ENGINE_BUFFER_SIZE * sizeof(int16_t) * 2,
                                              &bytes_written, pdMS_TO_TICKS(25));
            if (ret != ESP_OK) {
                // Record audio underrun for metrics
                perf_metrics_record_underrun();
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

            // Use ~23ms timeout to avoid blocking audio task
            esp_err_t ret = i2s_channel_write(tx_handle, buffer,
                                              ENGINE_BUFFER_SIZE * sizeof(int16_t) * 2,
                                              &bytes_written, pdMS_TO_TICKS(25));
            if (ret != ESP_OK) {
                perf_metrics_record_underrun();
                vTaskDelay(pdMS_TO_TICKS(5));
            }
        } else {
            // Engine not running
            // But still allow horn to play!
            if (horn_active && config.horn_enabled) {
                // Mix horn-only audio
                const signed char *horn_samples;
                uint32_t horn_count, horn_loop_begin, horn_loop_end;

                // Select horn type
                switch (config.horn_type) {
                    case HORN_TYPE_MANTGE:
                        horn_samples = mantgeHornSamples;
                        horn_count = mantgeHornSampleCount;
                        horn_loop_begin = mantgeHornLoopBegin;
                        horn_loop_end = mantgeHornLoopEnd;
                        break;
                    case HORN_TYPE_CUCARACHA:
                        horn_samples = cucarachaSamples;
                        horn_count = cucarachaSampleCount;
                        horn_loop_begin = cucarachaLoopBegin;
                        horn_loop_end = cucarachaLoopEnd;
                        break;
                    case HORN_TYPE_2TONE:
                        horn_samples = horn2ToneSamples;
                        horn_count = horn2ToneSampleCount;
                        horn_loop_begin = horn2ToneLoopBegin;
                        horn_loop_end = horn2ToneLoopEnd;
                        break;
                    case HORN_TYPE_DIXIE:
                        horn_samples = hornDixieSamples;
                        horn_count = hornDixieSampleCount;
                        horn_loop_begin = hornDixieLoopBegin;
                        horn_loop_end = hornDixieLoopEnd;
                        break;
                    case HORN_TYPE_PETERBILT:
                        horn_samples = hornPeterbiltSamples;
                        horn_count = hornPeterbiltSampleCount;
                        horn_loop_begin = hornPeterbiltLoopBegin;
                        horn_loop_end = hornPeterbiltLoopEnd;
                        break;
                    case HORN_TYPE_OUTLAW:
                        horn_samples = hornOutlawSamples;
                        horn_count = hornOutlawSampleCount;
                        horn_loop_begin = hornOutlawLoopBegin;
                        horn_loop_end = hornOutlawLoopEnd;
                        break;
                    default:  // HORN_TYPE_TRUCK
                        horn_samples = truckHornSamples;
                        horn_count = truckHornSampleCount;
                        horn_loop_begin = truckHornLoopBegin;
                        horn_loop_end = truckHornLoopEnd;
                        break;
                }

                // Generate horn samples into buffer
                int32_t vol = (config.horn_volume * get_master_volume()) / 100;
                for (size_t i = 0; i < ENGINE_BUFFER_SIZE; i++) {
                    uint32_t idx = horn_sample_pos >> 16;
                    int32_t mix = 0;
                    if (idx < horn_count) {
                        int16_t sample = ((int16_t)horn_samples[idx]) << 8;
                        mix = ((int32_t)sample * vol) >> 8;
                        horn_sample_pos += 0x10000;

                        // Loop within the loop region
                        uint32_t next_idx = horn_sample_pos >> 16;
                        if (next_idx >= horn_loop_end) {
                            horn_sample_pos = horn_loop_begin << 16;
                        }
                    }
                    // Clamp to 16-bit range
                    if (mix > 32767) mix = 32767;
                    if (mix < -32768) mix = -32768;
                    buffer[i * 2] = (int16_t)mix;
                    buffer[i * 2 + 1] = (int16_t)mix;
                }

                // Use ~23ms timeout to avoid blocking audio task
                esp_err_t ret = i2s_channel_write(tx_handle, buffer,
                                                  ENGINE_BUFFER_SIZE * sizeof(int16_t) * 2,
                                                  &bytes_written, pdMS_TO_TICKS(25));
                if (ret != ESP_OK) {
                    perf_metrics_record_underrun();
                    vTaskDelay(pdMS_TO_TICKS(5));
                }
            } else {
                // No horn, just wait
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        }
    }

    free(buffer);
    ESP_LOGI(TAG, "Engine sound task stopped");
    vTaskDelete(NULL);
}

// ============================================================================
// Config Defaults and Migration
// ============================================================================

/**
 * @brief Set default sound config values
 */
static void sound_config_get_defaults(engine_sound_config_t *cfg)
{
    if (!cfg) return;

    cfg->magic = SOUND_CONFIG_MAGIC;
    cfg->version = SOUND_CONFIG_VERSION;
    cfg->profile = SOUND_PROFILE_CAT_3408;
    cfg->master_volume_level1 = 100;    // Normal volume
    cfg->master_volume_level2 = 50;     // Quiet mode
    cfg->active_volume_level = 0;       // Start on level 1
    cfg->volume_preset_low = 20;        // Menu preset: Low
    cfg->volume_preset_medium = 100;    // Menu preset: Medium
    cfg->volume_preset_high = 170;      // Menu preset: High
    cfg->idle_volume = 100;
    cfg->rev_volume = 80;
    cfg->knock_volume = 80;
    cfg->start_volume = 90;
    cfg->max_rpm_percentage = 300;
    cfg->acceleration = 2;
    cfg->deceleration = 1;
    cfg->rev_switch_point = 120;
    cfg->idle_end_point = 450;
    cfg->knock_start_point = 150;
    cfg->knock_interval = 8;
    cfg->jake_brake_enabled = true;
    cfg->v8_mode = true;
    cfg->air_brake_enabled = true;
    cfg->air_brake_volume = 70;
    cfg->reverse_beep_enabled = true;
    cfg->reverse_beep_volume = 70;
    cfg->gear_shift_enabled = true;
    cfg->gear_shift_volume = 70;
    cfg->wastegate_enabled = true;
    cfg->wastegate_volume = 70;
    cfg->horn_enabled = true;
    cfg->horn_type = HORN_TYPE_TRUCK;
    cfg->horn_volume = 80;
    cfg->mode_switch_sound_enabled = true;
    cfg->mode_switch_volume = 80;
}

/**
 * @brief Migrate sound config from old version to new version
 * Preserves all compatible settings, only new fields get defaults
 *
 * Version history:
 *   v1: Original config with single master_volume field
 *   v2: Added dual volume levels (master_volume_level1/2, active_volume_level)
 *       - Struct layout changed: fields after master_volume shifted by 2 bytes
 *   v3: Added configurable volume presets (volume_preset_low/medium/high)
 *       - Struct layout changed: new fields added before idle_volume
 */
static void sound_config_migrate(engine_sound_config_t *old_config, uint32_t old_version)
{
    ESP_LOGI(TAG, "Migrating sound config from v%lu to v%d", (unsigned long)old_version, SOUND_CONFIG_VERSION);

    // Get fresh defaults for new version
    engine_sound_config_t new_config;
    sound_config_get_defaults(&new_config);

    // Preserve profile selection (same offset in all versions)
    new_config.profile = old_config->profile;

    if (old_version == 1) {
        // v1 → v2 migration:
        // - Old struct had single master_volume at offset of new master_volume_level1
        // - All fields after that have shifted by 2 bytes, so they can't be trusted
        // - We preserve the old master_volume as level1, set level2 to half that
        // - All other settings reset to defaults (struct layout incompatible)

        // The old master_volume is now at the position of master_volume_level1
        uint8_t old_master_volume = old_config->master_volume_level1;
        new_config.master_volume_level1 = old_master_volume;
        new_config.master_volume_level2 = old_master_volume / 2;  // Quiet mode = half
        new_config.active_volume_level = 0;  // Start on level 1

        ESP_LOGI(TAG, "v1->v2: old master_volume=%d -> level1=%d, level2=%d",
                 old_master_volume, new_config.master_volume_level1, new_config.master_volume_level2);
    }

    if (old_version >= 2) {
        // v2 → v3 migration:
        // - Preserve all v2 fields that are still at same offsets
        // - New volume_preset fields get defaults
        new_config.master_volume_level1 = old_config->master_volume_level1;
        new_config.master_volume_level2 = old_config->master_volume_level2;
        new_config.active_volume_level = old_config->active_volume_level;
        // Note: All fields after idle_volume have shifted by 3 bytes in v3
        // Since struct layout changed, we keep defaults for everything else
        // Volume presets get default values (20, 100, 170)

        ESP_LOGI(TAG, "v2->v3: preserving volume levels, adding preset defaults");
    }

    // Copy migrated config back
    memcpy(old_config, &new_config, sizeof(engine_sound_config_t));
    old_config->magic = SOUND_CONFIG_MAGIC;
    old_config->version = SOUND_CONFIG_VERSION;

    ESP_LOGI(TAG, "Sound config migration complete");
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

    // Create deferred NVS save timer
    const esp_timer_create_args_t timer_args = {
        .callback = nvs_save_timer_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "nvs_save"
    };
    esp_err_t timer_ret = esp_timer_create(&timer_args, &nvs_save_timer);
    if (timer_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create NVS save timer: %s", esp_err_to_name(timer_ret));
        vSemaphoreDelete(engine_mutex);
        return timer_ret;
    }

    // Try to load saved config from NVS with migration support
    engine_sound_config_t saved_config;
    size_t config_len = sizeof(engine_sound_config_t);
    esp_err_t load_ret = nvs_load_sound_config(&saved_config, &config_len);

    if (load_ret != ESP_OK || saved_config.magic != SOUND_CONFIG_MAGIC) {
        // No valid config at all - use defaults
        ESP_LOGW(TAG, "No valid sound config found, using defaults");
        sound_config_get_defaults(&config);
        nvs_save_sound_config(&config, sizeof(engine_sound_config_t));
    } else if (saved_config.version != SOUND_CONFIG_VERSION) {
        // Valid config but old version - migrate it
        sound_config_migrate(&saved_config, saved_config.version);
        memcpy(&config, &saved_config, sizeof(engine_sound_config_t));
        nvs_save_sound_config(&config, sizeof(engine_sound_config_t));
    } else {
        // Valid config with current version
        ESP_LOGI(TAG, "Loaded sound config from NVS (version %lu)", (unsigned long)saved_config.version);
        memcpy(&config, &saved_config, sizeof(engine_sound_config_t));
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

    // Cleanup deferred NVS timer and flush any pending writes
    if (nvs_save_timer) {
        esp_timer_stop(nvs_save_timer);
        // Flush any pending config changes before shutdown
        if (nvs_config_dirty) {
            nvs_config_dirty = false;
            nvs_save_sound_config(&config, sizeof(engine_sound_config_t));
            ESP_LOGI(TAG, "Flushed pending NVS config on deinit");
        }
        esp_timer_delete(nvs_save_timer);
        nvs_save_timer = NULL;
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

    // Determine if we're in reverse gear for reverse beep
    // Beep should play when:
    // 1. Actually moving backwards, OR
    // 2. Stopped but actively accelerating into reverse
    // Beep should NOT play when braking to a stop (even from reverse)
    int8_t direction = tuning_get_last_direction();
    bool stopped = (abs_speed < 50);
    bool accelerating_into_reverse = stopped && (direction == -1) && !is_braking && (throttle < -100);
    bool in_reverse = moving_reverse || accelerating_into_reverse;

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

    // Air brake: trigger when motor effectively stops from moving
    // Use motor cutoff threshold (ESC deadband) for accurate timing
    static int16_t peak_vehicle_speed = 0;  // Track highest speed reached
    static bool was_motor_stopped = true;   // Track motor state transitions

    // Get motor cutoff in vehicle_speed scale (0-500 instead of 0-1000)
    int16_t motor_cutoff_scaled = tuning_get_motor_cutoff() / 2;
    bool motor_stopped = tuning_is_motor_stopped();

    // Track peak speed while motor is running (not stopped)
    if (!motor_stopped && vehicle_speed > peak_vehicle_speed) {
        peak_vehicle_speed = vehicle_speed;
    }

    // Trigger air brake when motor crosses into "stopped" state from moving
    // This uses the ESC cutoff threshold for accurate timing
    bool motor_just_stopped = motor_stopped && !was_motor_stopped;
    if (motor_just_stopped && peak_vehicle_speed > 100 && !air_brake_trigger) {
        air_brake_trigger = true;
        air_brake_sample_pos = 0;
        ESP_LOGI(TAG, "Air brake triggered (motor stopped, peak: %d, cutoff: %d)",
                 peak_vehicle_speed, motor_cutoff_scaled);
        peak_vehicle_speed = 0;  // Reset after triggering
    }
    was_motor_stopped = motor_stopped;

    // Reset peak tracking when accelerating again
    if (!motor_stopped && effective_throttle > 50) {
        peak_vehicle_speed = vehicle_speed;  // Reset to current, not zero
    } else if (vehicle_speed < motor_cutoff_scaled && !air_brake_trigger) {
        // Reset after fully stopped
        peak_vehicle_speed = 0;
    }

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

    // Wastegate/blowoff: trigger after rapid throttle drop from high throttle
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

void engine_sound_play_mode_switch(void) {
    // Trigger the air shift sound for mode change feedback
    // Only if engine is running (otherwise use beep from sound.c)
    if (engine_state == ENGINE_RUNNING) {
        mode_switch_trigger = true;
        mode_switch_sample_pos = 0;
    }
}

void engine_sound_set_horn(bool active) {
    if (active && !horn_active) {
        // Starting horn - reset sample position
        horn_sample_pos = 0;
    }
    horn_active = active;
}

bool engine_sound_is_horn_active(void) {
    return horn_active;
}

uint8_t engine_sound_toggle_volume_level(void) {
    // Toggle between level 0 and 1
    config.active_volume_level = (config.active_volume_level == 0) ? 1 : 0;

    uint8_t new_volume = get_master_volume();
    ESP_LOGI(TAG, "Volume level toggled to %d (volume: %d%%)",
             config.active_volume_level, new_volume);

    // Play a short confirmation beep
    // Use the mode switch sound if engine is running, otherwise use a simple beep
    if (engine_state == ENGINE_RUNNING) {
        mode_switch_trigger = true;
        mode_switch_sample_pos = 0;
    }

    // Schedule deferred NVS save (non-blocking)
    schedule_nvs_save();

    return config.active_volume_level;
}

uint8_t engine_sound_get_master_volume(void) {
    return get_master_volume();
}

uint8_t engine_sound_get_volume_preset(uint8_t index) {
    switch (index) {
        case 0: return config.volume_preset_low;
        case 1: return config.volume_preset_medium;
        case 2: return config.volume_preset_high;
        default: return config.volume_preset_medium;
    }
}

void engine_sound_set_volume_preset(uint8_t index) {
    uint8_t volume = engine_sound_get_volume_preset(index);
    // Set both volume levels to the preset value
    config.master_volume_level1 = volume;
    config.master_volume_level2 = volume;
    // Schedule deferred NVS save (non-blocking)
    schedule_nvs_save();
    ESP_LOGI(TAG, "Volume set to preset %d (%d%%)", index, volume);
}

uint8_t engine_sound_get_current_volume_preset_index(void) {
    uint8_t current = get_master_volume();
    // Find closest matching preset
    int diff_low = abs((int)current - (int)config.volume_preset_low);
    int diff_med = abs((int)current - (int)config.volume_preset_medium);
    int diff_high = abs((int)current - (int)config.volume_preset_high);

    if (diff_low <= diff_med && diff_low <= diff_high) {
        return 0;  // Low
    } else if (diff_high <= diff_med) {
        return 2;  // High
    } else {
        return 1;  // Medium
    }
}
