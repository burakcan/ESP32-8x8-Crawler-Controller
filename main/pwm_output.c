/**
 * @file pwm_output.c
 * @brief PWM output implementation for ESC and servos
 */

#include "pwm_output.h"
#include "driver/mcpwm_prelude.h"
#include "esp_log.h"

static const char *TAG = "PWM_OUTPUT";

// ESC comparator handle
static mcpwm_cmpr_handle_t esc_comparator = NULL;
static uint16_t esc_current_pulse = FAILSAFE_THROTTLE_US;

// Servo comparator handles
static mcpwm_cmpr_handle_t servo_comparators[SERVO_COUNT] = {NULL};
static uint16_t servo_current_pulse[SERVO_COUNT] = {SERVO_CENTER_US, SERVO_CENTER_US, SERVO_CENTER_US, SERVO_CENTER_US};

// Servo GPIO pins (one per axle)
static const int servo_gpio_pins[SERVO_COUNT] = {
    PIN_SERVO_AXLE_1,
    PIN_SERVO_AXLE_2,
    PIN_SERVO_AXLE_3,
    PIN_SERVO_AXLE_4
};

// Servo names for logging
static const char *servo_names[SERVO_COUNT] = {
    "Axle-1",
    "Axle-2",
    "Axle-3",
    "Axle-4"
};

/**
 * @brief Initialize ESC PWM output
 */
static esp_err_t init_esc(void)
{
    ESP_LOGI(TAG, "Initializing ESC on GPIO %d", PIN_ESC);
    
    // Create timer for ESC (Group 0, same as RC input capture)
    mcpwm_timer_config_t timer_config = {
        .group_id = MCPWM_GROUP_RC_ESC,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = MCPWM_TIMER_RESOLUTION_HZ,
        .period_ticks = RC_PWM_PERIOD_US,  // 20000us = 50Hz
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
    };
    mcpwm_timer_handle_t timer = NULL;
    ESP_ERROR_CHECK(mcpwm_new_timer(&timer_config, &timer));
    
    // Create operator
    mcpwm_operator_config_t operator_config = {
        .group_id = MCPWM_GROUP_RC_ESC,
    };
    mcpwm_oper_handle_t oper = NULL;
    ESP_ERROR_CHECK(mcpwm_new_operator(&operator_config, &oper));
    ESP_ERROR_CHECK(mcpwm_operator_connect_timer(oper, timer));
    
    // Create comparator
    mcpwm_comparator_config_t comparator_config = {
        .flags.update_cmp_on_tez = true,
    };
    ESP_ERROR_CHECK(mcpwm_new_comparator(oper, &comparator_config, &esc_comparator));
    
    // Create generator
    mcpwm_generator_config_t generator_config = {
        .gen_gpio_num = PIN_ESC,
    };
    mcpwm_gen_handle_t generator = NULL;
    ESP_ERROR_CHECK(mcpwm_new_generator(oper, &generator_config, &generator));
    
    // Set PWM actions: HIGH on timer empty, LOW on compare match
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(generator,
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)));
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(generator,
        MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, esc_comparator, MCPWM_GEN_ACTION_LOW)));
    
    // Set initial pulse to neutral
    ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(esc_comparator, FAILSAFE_THROTTLE_US));
    esc_current_pulse = FAILSAFE_THROTTLE_US;
    
    // Enable and start timer
    ESP_ERROR_CHECK(mcpwm_timer_enable(timer));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP));
    
    ESP_LOGI(TAG, "ESC initialized at neutral (%d us)", FAILSAFE_THROTTLE_US);
    return ESP_OK;
}

/**
 * @brief Initialize servo PWM outputs
 */
static esp_err_t init_servos(void)
{
    ESP_LOGI(TAG, "Initializing %d servos...", SERVO_COUNT);
    
    // Create timer for servos (Group 1)
    mcpwm_timer_config_t timer_config = {
        .group_id = MCPWM_GROUP_SERVOS,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = MCPWM_TIMER_RESOLUTION_HZ,
        .period_ticks = RC_PWM_PERIOD_US,  // 20000us = 50Hz
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
    };
    mcpwm_timer_handle_t timer = NULL;
    ESP_ERROR_CHECK(mcpwm_new_timer(&timer_config, &timer));
    
    // Create operators for servos (need multiple operators for multiple outputs)
    // Each MCPWM group has 3 operators, each operator has 2 generators
    // We need 4 servos, so we'll use 2 operators
    
    mcpwm_oper_handle_t operators[2] = {NULL, NULL};
    
    for (int op = 0; op < 2; op++) {
        mcpwm_operator_config_t operator_config = {
            .group_id = MCPWM_GROUP_SERVOS,
        };
        ESP_ERROR_CHECK(mcpwm_new_operator(&operator_config, &operators[op]));
        ESP_ERROR_CHECK(mcpwm_operator_connect_timer(operators[op], timer));
    }
    
    // Create comparators and generators for each servo
    for (int i = 0; i < SERVO_COUNT; i++) {
        int op_idx = i / 2;  // Which operator to use
        
        // Create comparator
        mcpwm_comparator_config_t comparator_config = {
            .flags.update_cmp_on_tez = true,
        };
        ESP_ERROR_CHECK(mcpwm_new_comparator(operators[op_idx], &comparator_config, &servo_comparators[i]));
        
        // Create generator
        mcpwm_generator_config_t generator_config = {
            .gen_gpio_num = servo_gpio_pins[i],
        };
        mcpwm_gen_handle_t generator = NULL;
        ESP_ERROR_CHECK(mcpwm_new_generator(operators[op_idx], &generator_config, &generator));
        
        // Set PWM actions
        ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(generator,
            MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)));
        ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(generator,
            MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, servo_comparators[i], MCPWM_GEN_ACTION_LOW)));
        
        // Set initial pulse to center
        ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(servo_comparators[i], SERVO_CENTER_US));
        servo_current_pulse[i] = SERVO_CENTER_US;
        
        ESP_LOGI(TAG, "  Servo %d (%s) on GPIO %d", i, servo_names[i], servo_gpio_pins[i]);
    }
    
    // Enable and start timer
    ESP_ERROR_CHECK(mcpwm_timer_enable(timer));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP));
    
    ESP_LOGI(TAG, "Servos initialized at center (%d us)", SERVO_CENTER_US);
    return ESP_OK;
}

esp_err_t pwm_output_init(void)
{
    ESP_LOGI(TAG, "Initializing PWM outputs...");
    
    esp_err_t ret = init_esc();
    if (ret != ESP_OK) {
        return ret;
    }
    
    ret = init_servos();
    if (ret != ESP_OK) {
        return ret;
    }
    
    ESP_LOGI(TAG, "All PWM outputs initialized");
    return ESP_OK;
}

// ============================================================================
// ESC Control
// ============================================================================

esp_err_t esc_set_pulse(uint16_t pulse_us)
{
    // Clamp to valid range
    if (pulse_us < RC_VALID_MIN_US) pulse_us = RC_VALID_MIN_US;
    if (pulse_us > RC_VALID_MAX_US) pulse_us = RC_VALID_MAX_US;
    
    esp_err_t ret = mcpwm_comparator_set_compare_value(esc_comparator, pulse_us);
    if (ret == ESP_OK) {
        esc_current_pulse = pulse_us;
    }
    return ret;
}

esp_err_t esc_set_throttle(int16_t throttle)
{
    uint16_t pulse = value_to_pulse(throttle, RC_DEFAULT_MIN_US, RC_DEFAULT_CENTER_US, RC_DEFAULT_MAX_US);
    return esc_set_pulse(pulse);
}

esp_err_t esc_set_neutral(void)
{
    return esc_set_pulse(FAILSAFE_THROTTLE_US);
}

uint16_t esc_get_pulse(void)
{
    return esc_current_pulse;
}

// ============================================================================
// Servo Control
// ============================================================================

esp_err_t servo_set_pulse(servo_id_t servo, uint16_t pulse_us)
{
    if (servo >= SERVO_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Clamp to valid servo range
    if (pulse_us < SERVO_MIN_US) pulse_us = SERVO_MIN_US;
    if (pulse_us > SERVO_MAX_US) pulse_us = SERVO_MAX_US;
    
    esp_err_t ret = mcpwm_comparator_set_compare_value(servo_comparators[servo], pulse_us);
    if (ret == ESP_OK) {
        servo_current_pulse[servo] = pulse_us;
    }
    return ret;
}

esp_err_t servo_set_position(servo_id_t servo, int16_t position)
{
    if (servo >= SERVO_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint16_t pulse = value_to_pulse(position, SERVO_MIN_US, SERVO_CENTER_US, SERVO_MAX_US);
    return servo_set_pulse(servo, pulse);
}

esp_err_t servo_center_all(void)
{
    esp_err_t ret = ESP_OK;
    for (int i = 0; i < SERVO_COUNT; i++) {
        esp_err_t r = servo_set_pulse((servo_id_t)i, SERVO_CENTER_US);
        if (r != ESP_OK) ret = r;
    }
    return ret;
}

esp_err_t servo_set_all(const int16_t positions[SERVO_COUNT])
{
    esp_err_t ret = ESP_OK;
    for (int i = 0; i < SERVO_COUNT; i++) {
        esp_err_t r = servo_set_position((servo_id_t)i, positions[i]);
        if (r != ESP_OK) ret = r;
    }
    return ret;
}

uint16_t servo_get_pulse(servo_id_t servo)
{
    if (servo >= SERVO_COUNT) {
        return SERVO_CENTER_US;
    }
    return servo_current_pulse[servo];
}

// ============================================================================
// Utility Functions
// ============================================================================

uint16_t value_to_pulse(int16_t value, uint16_t min_us, uint16_t center_us, uint16_t max_us)
{
    // Clamp input
    if (value < -1000) value = -1000;
    if (value > 1000) value = 1000;
    
    uint16_t pulse;
    if (value < 0) {
        // Negative: map -1000..0 to min..center
        pulse = center_us + (value * (int32_t)(center_us - min_us)) / 1000;
    } else {
        // Positive: map 0..1000 to center..max
        pulse = center_us + (value * (int32_t)(max_us - center_us)) / 1000;
    }
    
    return pulse;
}

int16_t pulse_to_value(uint16_t pulse_us, uint16_t min_us, uint16_t center_us, uint16_t max_us)
{
    int16_t value;
    
    if (pulse_us <= center_us) {
        // Negative side
        if (center_us > min_us) {
            value = ((int32_t)(pulse_us - center_us) * 1000) / (center_us - min_us);
        } else {
            value = 0;
        }
    } else {
        // Positive side
        if (max_us > center_us) {
            value = ((int32_t)(pulse_us - center_us) * 1000) / (max_us - center_us);
        } else {
            value = 0;
        }
    }
    
    // Clamp output
    if (value < -1000) value = -1000;
    if (value > 1000) value = 1000;
    
    return value;
}
