/**
 * @file tuning.c
 * @brief Tuning configuration management implementation
 */

#include "tuning.h"
#include "nvs_storage.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "TUNING";

// Current tuning configuration
static tuning_config_t current_config;

// Realistic throttle state
static int16_t simulated_velocity = 0;  // Current simulated velocity (-1000 to +1000)
static int8_t last_direction = 0;       // Last movement direction: -1=reverse, 0=neutral, 1=forward
static bool throttle_released = true;   // Has throttle returned to neutral since stopping?
static throttle_mode_t current_throttle_mode = THROTTLE_MODE_DIRECT; // AUX4 throttle mode
static bool currently_braking = false;  // True when throttle opposes movement

/**
 * @brief Set default tuning values
 */
void tuning_get_defaults(tuning_config_t *config)
{
    if (!config) return;

    memset(config, 0, sizeof(tuning_config_t));

    config->magic = TUNING_MAGIC;
    config->version = TUNING_VERSION;

    // Servo defaults
    for (int i = 0; i < SERVO_COUNT; i++) {
        config->servos[i].min_us = TUNING_DEFAULT_SERVO_MIN;
        config->servos[i].max_us = TUNING_DEFAULT_SERVO_MAX;
        config->servos[i].subtrim = TUNING_DEFAULT_SUBTRIM;
        config->servos[i].trim = TUNING_DEFAULT_TRIM;
        config->servos[i].reversed = false;
    }

    // Steering geometry defaults
    config->steering.axle_ratio[0] = TUNING_DEFAULT_AXLE1_RATIO;
    config->steering.axle_ratio[1] = TUNING_DEFAULT_AXLE2_RATIO;
    config->steering.axle_ratio[2] = TUNING_DEFAULT_AXLE3_RATIO;
    config->steering.axle_ratio[3] = TUNING_DEFAULT_AXLE4_RATIO;
    config->steering.all_axle_rear_ratio = TUNING_DEFAULT_ALL_AXLE_REAR;
    config->steering.expo = TUNING_DEFAULT_EXPO;
    config->steering.speed_steering = TUNING_DEFAULT_SPEED_STEERING;
    // Realistic steering defaults
    config->steering.realistic_enabled = TUNING_DEFAULT_REALISTIC_STEER;
    config->steering.responsiveness = TUNING_DEFAULT_RESPONSIVENESS;
    config->steering.return_rate = TUNING_DEFAULT_RETURN_RATE;

    // ESC defaults
    config->esc.fwd_limit = TUNING_DEFAULT_FWD_LIMIT;
    config->esc.rev_limit = TUNING_DEFAULT_REV_LIMIT;
    config->esc.subtrim = TUNING_DEFAULT_SUBTRIM;
    config->esc.deadzone = TUNING_DEFAULT_ESC_DEADZONE;
    config->esc.reversed = false;
    config->esc.realistic_throttle = TUNING_DEFAULT_REALISTIC;
    config->esc.coast_rate = TUNING_DEFAULT_COAST_RATE;
    config->esc.brake_force = TUNING_DEFAULT_BRAKE_FORCE;
    config->esc.motor_cutoff = TUNING_DEFAULT_MOTOR_CUTOFF;
}

/**
 * @brief Migrate tuning config from old version to new version
 * Preserves all compatible settings, only new fields get defaults
 */
static void tuning_migrate(tuning_config_t *old_config, uint32_t old_version)
{
    ESP_LOGI(TAG, "Migrating tuning from v%lu to v%d", (unsigned long)old_version, TUNING_VERSION);

    // Get fresh defaults for new version
    tuning_config_t new_config;
    tuning_get_defaults(&new_config);

    // Copy all servo settings (these have been stable since v1)
    for (int i = 0; i < SERVO_COUNT; i++) {
        new_config.servos[i] = old_config->servos[i];
    }

    // Copy steering settings (stable since v1)
    new_config.steering.axle_ratio[0] = old_config->steering.axle_ratio[0];
    new_config.steering.axle_ratio[1] = old_config->steering.axle_ratio[1];
    new_config.steering.axle_ratio[2] = old_config->steering.axle_ratio[2];
    new_config.steering.axle_ratio[3] = old_config->steering.axle_ratio[3];
    new_config.steering.all_axle_rear_ratio = old_config->steering.all_axle_rear_ratio;
    new_config.steering.expo = old_config->steering.expo;
    new_config.steering.speed_steering = old_config->steering.speed_steering;

    // Copy ESC settings that exist in all versions
    new_config.esc.fwd_limit = old_config->esc.fwd_limit;
    new_config.esc.rev_limit = old_config->esc.rev_limit;
    new_config.esc.subtrim = old_config->esc.subtrim;
    new_config.esc.deadzone = old_config->esc.deadzone;
    new_config.esc.reversed = old_config->esc.reversed;
    new_config.esc.realistic_throttle = old_config->esc.realistic_throttle;
    new_config.esc.coast_rate = old_config->esc.coast_rate;

    // Version-specific migrations
    if (old_version >= 7) {
        // v7+ has brake_force
        new_config.esc.brake_force = old_config->esc.brake_force;
    }
    if (old_version >= 8) {
        // v8+ has motor_cutoff
        new_config.esc.motor_cutoff = old_config->esc.motor_cutoff;
    }
    if (old_version >= 9) {
        // v9+ has realistic steering
        new_config.steering.realistic_enabled = old_config->steering.realistic_enabled;
        new_config.steering.responsiveness = old_config->steering.responsiveness;
        new_config.steering.return_rate = old_config->steering.return_rate;
    }
    // New fields in future versions will get defaults automatically

    // Copy migrated config back
    memcpy(old_config, &new_config, sizeof(tuning_config_t));
    old_config->magic = TUNING_MAGIC;
    old_config->version = TUNING_VERSION;

    ESP_LOGI(TAG, "Migration complete");
}

esp_err_t tuning_init(tuning_config_t *config)
{
    ESP_LOGI(TAG, "Initializing tuning system...");

    // Try to load from NVS
    esp_err_t ret = nvs_load_tuning(&current_config);

    if (ret != ESP_OK || current_config.magic != TUNING_MAGIC) {
        // No valid config at all - use defaults
        ESP_LOGW(TAG, "No valid tuning found, using defaults");
        tuning_get_defaults(&current_config);
        nvs_save_tuning(&current_config);
    } else if (current_config.version != TUNING_VERSION) {
        // Valid config but old version - migrate it
        tuning_migrate(&current_config, current_config.version);
        nvs_save_tuning(&current_config);
    } else {
        ESP_LOGI(TAG, "Loaded tuning from NVS (version %lu)", (unsigned long)current_config.version);
    }

    // Log summary
    ESP_LOGI(TAG, "Servo endpoints: [%d-%d] [%d-%d] [%d-%d] [%d-%d]",
             current_config.servos[0].min_us, current_config.servos[0].max_us,
             current_config.servos[1].min_us, current_config.servos[1].max_us,
             current_config.servos[2].min_us, current_config.servos[2].max_us,
             current_config.servos[3].min_us, current_config.servos[3].max_us);
    ESP_LOGI(TAG, "Axle ratios: %d%% %d%% %d%% %d%%, all-axle rear: %d%%",
             current_config.steering.axle_ratio[0],
             current_config.steering.axle_ratio[1],
             current_config.steering.axle_ratio[2],
             current_config.steering.axle_ratio[3],
             current_config.steering.all_axle_rear_ratio);
    ESP_LOGI(TAG, "ESC limits: fwd=%d%% rev=%d%%, deadzone=%d",
             current_config.esc.fwd_limit,
             current_config.esc.rev_limit,
             current_config.esc.deadzone);

    if (config) {
        memcpy(config, &current_config, sizeof(tuning_config_t));
    }

    return ESP_OK;
}

const tuning_config_t* tuning_get_config(void)
{
    return &current_config;
}

esp_err_t tuning_set_config(const tuning_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;

    memcpy(&current_config, config, sizeof(tuning_config_t));
    current_config.magic = TUNING_MAGIC;
    current_config.version = TUNING_VERSION;

    ESP_LOGI(TAG, "Config set: coast=%d, brake=%d, realistic=%d",
             current_config.esc.coast_rate, current_config.esc.brake_force,
             current_config.esc.realistic_throttle);

    return ESP_OK;
}

esp_err_t tuning_save(void)
{
    ESP_LOGI(TAG, "Saving tuning to NVS...");
    return nvs_save_tuning(&current_config);
}

esp_err_t tuning_reset_defaults(bool save_to_nvs)
{
    ESP_LOGI(TAG, "Resetting tuning to defaults");
    tuning_get_defaults(&current_config);

    if (save_to_nvs) {
        return tuning_save();
    }
    return ESP_OK;
}

// ============================================================================
// Output Calculation Functions
// ============================================================================

// Debug logging for servo pulse calculation (uncomment to enable)
// #define DEBUG_SERVO_PULSE

uint16_t tuning_calc_servo_pulse(uint8_t servo_idx, int16_t position)
{
    if (servo_idx >= SERVO_COUNT) {
        return SERVO_CENTER_US;
    }

    const servo_tuning_t *servo = &current_config.servos[servo_idx];

#ifdef DEBUG_SERVO_PULSE
    int16_t orig_position = position;
#endif

    // Apply reverse
    if (servo->reversed) {
        position = -position;
    }

    // Calculate center with subtrim and trim
    // Subtrim shifts everything (center + endpoints)
    // Trim shifts only center (endpoints stay fixed)
    int16_t center = SERVO_CENTER_US + servo->subtrim + servo->trim;
    int16_t min_us = servo->min_us + servo->subtrim;
    int16_t max_us = servo->max_us + servo->subtrim;

    // Clamp endpoints to valid servo range
    if (min_us < SERVO_MIN_US) min_us = SERVO_MIN_US;
    if (max_us > SERVO_MAX_US) max_us = SERVO_MAX_US;

    // Map position (-1000 to +1000) to pulse width
    uint16_t pulse;
    if (position < 0) {
        // Negative: map -1000..0 to min..center
        pulse = center + (position * (center - min_us)) / 1000;
    } else {
        // Positive: map 0..1000 to center..max
        pulse = center + (position * (max_us - center)) / 1000;
    }

    // Final clamp
    if (pulse < min_us) pulse = min_us;
    if (pulse > max_us) pulse = max_us;

#ifdef DEBUG_SERVO_PULSE
    static uint32_t last_log[SERVO_COUNT] = {0};
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (now - last_log[servo_idx] > 500) {  // Log every 500ms
        ESP_LOGI(TAG, "Servo %d: pos_in=%d, pos_rev=%d, subtrim=%d, trim=%d, center=%d, pulse=%u",
                 servo_idx, orig_position, position, servo->subtrim, servo->trim, center, pulse);
        last_log[servo_idx] = now;
    }
#endif

    return pulse;
}

uint16_t tuning_calc_esc_pulse(int16_t throttle)
{
    const esc_tuning_t *esc = &current_config.esc;

    // Apply reverse
    if (esc->reversed) {
        throttle = -throttle;
    }

    // Apply deadzone
    if (throttle > -esc->deadzone && throttle < esc->deadzone) {
        throttle = 0;
    }

    // Apply speed limits
    if (throttle > 0) {
        // Forward: limit to fwd_limit percent
        throttle = (throttle * esc->fwd_limit) / 100;
    } else if (throttle < 0) {
        // Reverse: limit to rev_limit percent
        throttle = (throttle * esc->rev_limit) / 100;
    }

    // Apply realistic throttle behavior if AUX4 switch is in high position
    if (current_throttle_mode == THROTTLE_MODE_REALISTIC) {
        throttle = tuning_apply_realistic_throttle(throttle);
    }

    // Calculate pulse with subtrim
    int16_t center = SERVO_CENTER_US + esc->subtrim;

    uint16_t pulse;
    if (throttle < 0) {
        pulse = center + (throttle * (center - RC_DEFAULT_MIN_US)) / 1000;
    } else {
        pulse = center + (throttle * (RC_DEFAULT_MAX_US - center)) / 1000;
    }

    // Clamp to valid range
    if (pulse < RC_DEFAULT_MIN_US) pulse = RC_DEFAULT_MIN_US;
    if (pulse > RC_DEFAULT_MAX_US) pulse = RC_DEFAULT_MAX_US;

    return pulse;
}

int16_t tuning_apply_realistic_throttle(int16_t throttle_input)
{
    const esc_tuning_t *esc = &current_config.esc;

    // Coast rate: 0 = fast deceleration, 100 = slow deceleration (long coast)
    int16_t coast_decel = 50 - (esc->coast_rate * 45) / 100;  // 50 down to 5
    if (coast_decel < 5) coast_decel = 5;

    // Brake force: 0 = weak brake, 100 = instant stop
    int16_t brake_strength = 5 + (esc->brake_force * 195) / 100;

    // Base acceleration rate
    int16_t accel_rate = 20 + (100 - esc->coast_rate) / 5;  // 20-40 per tick

    // Determine direction
    bool throttle_forward = (throttle_input > 0);
    bool throttle_reverse = (throttle_input < 0);
    bool throttle_neutral = (throttle_input == 0);

    bool moving_forward = (simulated_velocity > 0);
    bool moving_reverse = (simulated_velocity < 0);
    bool stopped = (simulated_velocity == 0);

    // Reset braking flag - will be set if braking detected
    currently_braking = false;

    // Track throttle release for direction change lockout
    if (throttle_neutral) {
        throttle_released = true;
    }

    // Case 1: No throttle - coast (natural deceleration only)
    if (throttle_neutral) {
        if (moving_forward) {
            simulated_velocity -= coast_decel;
            if (simulated_velocity < 0) simulated_velocity = 0;
        } else if (moving_reverse) {
            simulated_velocity += coast_decel;
            if (simulated_velocity > 0) simulated_velocity = 0;
        }
    }
    // Case 2: Throttle opposite to movement - active braking (does NOT reverse)
    // Also consider braking when stopped but holding throttle opposite to last direction
    else if ((throttle_forward && moving_reverse) || (throttle_reverse && moving_forward) ||
             (stopped && throttle_reverse && last_direction == 1) ||
             (stopped && throttle_forward && last_direction == -1)) {
        currently_braking = true;  // Set braking flag for engine sound
        static int log_cnt = 0;
        if (++log_cnt >= 20) {  // Log every 200ms
            ESP_LOGI(TAG, "BRAKE: vel=%d str=%d force=%d%%", simulated_velocity, brake_strength, esc->brake_force);
            log_cnt = 0;
        }
        if (moving_forward) {
            simulated_velocity -= brake_strength;
            if (simulated_velocity < 0) simulated_velocity = 0;
        } else if (moving_reverse) {
            simulated_velocity += brake_strength;
            if (simulated_velocity > 0) simulated_velocity = 0;
        }
        // When stopped, velocity stays 0 (already handled above)
        // Braking does not allow direction change - must release throttle first
        throttle_released = false;
    }
    // Case 3: Throttle in same direction as movement (or from stop)
    else {
        if (throttle_forward) {
            // Check if we can go forward: either already moving forward,
            // or stopped AND throttle was released since last movement
            bool can_go_forward = moving_forward || (stopped && (throttle_released || last_direction != -1));

            if (can_go_forward) {
                if (simulated_velocity < throttle_input) {
                    // Accelerating
                    simulated_velocity += accel_rate;
                    if (simulated_velocity > throttle_input) {
                        simulated_velocity = throttle_input;
                    }
                    last_direction = 1;
                    throttle_released = false;
                } else if (simulated_velocity > throttle_input) {
                    // Coasting down to target
                    simulated_velocity -= coast_decel;
                    if (simulated_velocity < throttle_input) {
                        simulated_velocity = throttle_input;
                    }
                }
            }
            // else: trying to go forward after braking from reverse - blocked until throttle released
        } else if (throttle_reverse) {
            // Check if we can go reverse: either already moving reverse,
            // or stopped AND throttle was released since last movement
            bool can_go_reverse = moving_reverse || (stopped && (throttle_released || last_direction != 1));

            if (can_go_reverse) {
                if (simulated_velocity > throttle_input) {
                    simulated_velocity -= accel_rate;
                    if (simulated_velocity < throttle_input) {
                        simulated_velocity = throttle_input;
                    }
                    last_direction = -1;
                    throttle_released = false;
                } else if (simulated_velocity < throttle_input) {
                    simulated_velocity += coast_decel;
                    if (simulated_velocity > throttle_input) {
                        simulated_velocity = throttle_input;
                    }
                }
            }
            // else: trying to go reverse after braking from forward - blocked until throttle released
        }
    }

    // Reset direction tracking when fully stopped and throttle released
    if (stopped && throttle_released) {
        last_direction = 0;
    }

    return simulated_velocity;
}

void tuning_reset_realistic_throttle(void)
{
    simulated_velocity = 0;
    last_direction = 0;
    throttle_released = true;
}

int16_t tuning_get_simulated_velocity(void)
{
    return simulated_velocity;
}

// ============================================================================
// Realistic Steering
// ============================================================================

// Current smoothed steering input (single value - like a mechanical linkage)
// All axles follow this proportionally based on their ratios
static int16_t current_steering_input = 0;

int16_t tuning_apply_realistic_steering(int16_t target_input)
{
    const steering_tuning_t *steer = &current_config.steering;

    // Convert responsiveness (0-100) to max movement rate per tick
    // 0 = very slow (max ~10), 100 = fast (max ~60)
    int16_t max_move_rate = 10 + (steer->responsiveness * 50) / 100;

    // Convert return_rate (0-100) to max center return speed
    int16_t max_return_rate = 10 + (steer->return_rate * 60) / 100;

    // Minimum rate to ensure we actually reach the target
    const int16_t min_rate = 3;

    // Threshold to consider "returning to center" (within ±50 of center)
    const int16_t center_threshold = 50;

    int16_t delta = target_input - current_steering_input;

    if (delta == 0) {
        return current_steering_input;
    }

    // Determine max rate based on whether we're steering or centering
    int16_t max_rate;
    if (target_input > -center_threshold && target_input < center_threshold) {
        max_rate = max_return_rate;
    } else {
        max_rate = max_move_rate;
    }

    // Proportional rate: faster when far, slower when close (ease-in/ease-out)
    // Divisor of 20 gives good feel: at delta=1000 rate=50, at delta=100 rate=5
    int16_t abs_delta = (delta > 0) ? delta : -delta;
    int16_t rate = abs_delta / 20;

    // Clamp rate between min and max
    if (rate > max_rate) rate = max_rate;
    if (rate < min_rate) rate = min_rate;

    // Move toward target
    if (delta > 0) {
        current_steering_input += rate;
        if (current_steering_input > target_input) {
            current_steering_input = target_input;
        }
    } else {
        current_steering_input -= rate;
        if (current_steering_input < target_input) {
            current_steering_input = target_input;
        }
    }

    return current_steering_input;
}

void tuning_reset_realistic_steering(void)
{
    current_steering_input = 0;
}

bool tuning_is_realistic_steering_enabled(void)
{
    return current_config.steering.realistic_enabled;
}

void tuning_set_throttle_mode(throttle_mode_t mode)
{
    current_throttle_mode = mode;
}

bool tuning_is_neutral_mode(void)
{
    return current_throttle_mode == THROTTLE_MODE_NEUTRAL;
}

int16_t tuning_apply_expo(int16_t input)
{
    uint8_t expo = current_config.steering.expo;

    if (expo == 0) {
        return input;  // Linear, no expo
    }

    // Expo formula: output = input * (1 - expo/100) + input^3 * (expo/100)
    // This provides softer response around center while maintaining full travel
    // Simplified integer math version:

    // Normalize to -1.0 to +1.0 range (using fixed point: /1000)
    int32_t x = input;
    int32_t x_cubed = (x * x * x) / (1000 * 1000);  // x^3 scaled

    // Linear portion weight: (100 - expo)
    // Cubic portion weight: expo
    int32_t linear_part = x * (100 - expo);
    int32_t cubic_part = x_cubed * expo;

    int32_t result = (linear_part + cubic_part) / 100;

    // Clamp result
    if (result > 1000) result = 1000;
    if (result < -1000) result = -1000;

    return (int16_t)result;
}

uint8_t tuning_get_axle_ratio(uint8_t axle_idx, steering_mode_t mode)
{
    if (axle_idx >= SERVO_COUNT) {
        return 0;
    }

    uint8_t ratio = current_config.steering.axle_ratio[axle_idx];

    // In all-axle mode, rear axles (2, 3) get additional reduction
    if (mode == STEER_MODE_ALL_AXLE && (axle_idx == 2 || axle_idx == 3)) {
        ratio = (ratio * current_config.steering.all_axle_rear_ratio) / 100;
    }

    return ratio;
}

int16_t tuning_apply_speed_steering(int16_t steering)
{
    uint8_t speed_steering = current_config.steering.speed_steering;

    if (speed_steering == 0) {
        return steering;  // Feature disabled
    }

    // Use simulated velocity (current output) not throttle input
    // This means steering stays reduced while coasting at speed
    int16_t abs_velocity = (simulated_velocity < 0) ? -simulated_velocity : simulated_velocity;

    // Calculate reduction factor based on current speed
    // At 0 velocity: no reduction (100%)
    // At full velocity: reduce by speed_steering percent
    // Formula: reduction = 100 - (abs_velocity * speed_steering / 1000)
    int32_t reduction = 100 - ((int32_t)abs_velocity * speed_steering) / 1000;
    if (reduction < 0) reduction = 0;

    // Apply reduction to steering
    int32_t result = ((int32_t)steering * reduction) / 100;

    // Clamp result
    if (result > 1000) result = 1000;
    if (result < -1000) result = -1000;

    return (int16_t)result;
}

bool tuning_is_braking(void)
{
    return currently_braking;
}

int8_t tuning_get_last_direction(void)
{
    return last_direction;
}

bool tuning_is_motor_stopped(void)
{
    // Motor is effectively stopped when simulated velocity is below ESC cutoff threshold
    int16_t abs_velocity = (simulated_velocity < 0) ? -simulated_velocity : simulated_velocity;
    return abs_velocity < current_config.esc.motor_cutoff;
}

int16_t tuning_get_motor_cutoff(void)
{
    return current_config.esc.motor_cutoff;
}
