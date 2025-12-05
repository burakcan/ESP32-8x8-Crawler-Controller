/**
 * @file config.h
 * @brief Central configuration for 8x8 Crawler Controller
 * 
 * All pin definitions, constants, and configuration values in one place.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// VERSION
// ============================================================================
#define FIRMWARE_VERSION    "1.0.0"
#define PROJECT_NAME        "8x8 Crawler Controller"

// ============================================================================
// PIN DEFINITIONS (ESP32-S3 Mini)
// ============================================================================

// RC Receiver Input Pins (directly from receiver channels)
// ESP32-S3 Mini - 6 channel receiver
#define PIN_RC_STEERING     6   // Channel 1 - Steering
#define PIN_RC_THROTTLE     5   // Channel 2 - Throttle
#define PIN_RC_AUX1         4   // Channel 3 - Aux 1 / Mode select
#define PIN_RC_AUX2         3   // Channel 4 - Aux 2 / Extra
#define PIN_RC_AUX3         2   // Channel 5 - Aux 3 (reserved)
#define PIN_RC_AUX4         1   // Channel 6 - Aux 4 (reserved)

// ESC Output Pin
#define PIN_ESC             12  // Main drive motor ESC

// Servo Output Pins (4 servos for 8x8 steering - one per axle)
// Axle numbering: 1=front, 2, 3, 4=rear
// Axles 1-2 steer together, Axles 3-4 steer together
#define PIN_SERVO_AXLE_1    8   // Axle 1 (front) steering servo
#define PIN_SERVO_AXLE_2    9   // Axle 2 steering servo
#define PIN_SERVO_AXLE_3    10  // Axle 3 steering servo
#define PIN_SERVO_AXLE_4    11  // Axle 4 (rear) steering servo

// Status LED (RGB WS2812 on ESP32-S3 Mini)
#define PIN_STATUS_LED      21  // RGB LED on ESP32-S3 Mini
#define STATUS_LED_IS_RGB   1   // Flag indicating RGB LED (WS2812)

// ============================================================================
// RC SIGNAL PARAMETERS
// ============================================================================

// Standard RC PWM timing
#define RC_PWM_FREQ_HZ          50      // 50Hz standard RC frequency
#define RC_PWM_PERIOD_US        20000   // 20ms period

// Default signal range (will be overridden by calibration)
#define RC_DEFAULT_MIN_US       1000    // Minimum pulse width
#define RC_DEFAULT_CENTER_US    1500    // Center/neutral pulse width
#define RC_DEFAULT_MAX_US       2000    // Maximum pulse width

// Valid signal detection range (outside = invalid/no signal)
#define RC_VALID_MIN_US         800     // Below this = invalid
#define RC_VALID_MAX_US         2200    // Above this = invalid

// Signal loss timeout
#define RC_SIGNAL_TIMEOUT_MS    250     // Time before declaring signal lost

// ============================================================================
// SERVO PARAMETERS
// ============================================================================

#define SERVO_MIN_US            500     // Servo minimum pulse (extended range)
#define SERVO_MAX_US            2500    // Servo maximum pulse (extended range)
#define SERVO_CENTER_US         1500    // Servo center position

// ============================================================================
// CALIBRATION PARAMETERS
// ============================================================================

// Number of RC channels
#define RC_CHANNEL_COUNT        6

// Channel indices
typedef enum {
    RC_CH_THROTTLE = 0,
    RC_CH_STEERING = 1,
    RC_CH_AUX1 = 2,
    RC_CH_AUX2 = 3,
    RC_CH_AUX3 = 4,
    RC_CH_AUX4 = 5
} rc_channel_t;

// Calibration data structure for a single channel
typedef struct {
    uint16_t min;       // Minimum pulse width (stick full one way)
    uint16_t center;    // Center/neutral pulse width
    uint16_t max;       // Maximum pulse width (stick full other way)
    uint16_t deadzone;  // Deadzone around center (in us)
    bool reversed;      // Whether channel is reversed
} channel_calibration_t;

// Full calibration data
typedef struct {
    uint32_t magic;                                     // Magic number to verify valid data
    uint32_t version;                                   // Calibration data version
    channel_calibration_t channels[RC_CHANNEL_COUNT];   // Per-channel calibration
    bool calibrated;                                    // Whether calibration has been done
} calibration_data_t;

#define CALIBRATION_MAGIC       0x88CA1001  // Magic number for calibration data
#define CALIBRATION_VERSION     1

// Default calibration values
#define DEFAULT_DEADZONE_US     20  // 20us deadzone around center

// ============================================================================
// STEERING MODES
// ============================================================================

typedef enum {
    STEER_MODE_FRONT = 0,       // Axles 1-2 steer, 3-4 fixed (like a car)
    STEER_MODE_REAR,            // Axles 3-4 steer, 1-2 fixed
    STEER_MODE_ALL_AXLE,        // All axles steer (1-2 opposite to 3-4)
    STEER_MODE_CRAB,            // All axles steer same direction (crab walk)
    STEER_MODE_COUNT            // Number of steering modes
} steering_mode_t;

// ============================================================================
// SYSTEM PARAMETERS
// ============================================================================

// Main loop timing
#define MAIN_LOOP_PERIOD_MS     10  // 100Hz main loop

// Failsafe values (used when signal is lost)
#define FAILSAFE_THROTTLE_US    1500    // Neutral throttle
#define FAILSAFE_STEERING_US    1500    // Centered steering

// NVS namespace for storing calibration
#define NVS_NAMESPACE           "crawler_cfg"
#define NVS_KEY_CALIBRATION     "calibration"
#define NVS_KEY_WIFI_STA        "wifi_sta"
#define NVS_KEY_TUNING          "tuning"

// ============================================================================
// TUNING CONFIGURATION
// ============================================================================

// Number of steering servos (one per axle)
#define SERVO_COUNT             4

// Per-servo tuning: endpoints, subtrim, trim, reverse
typedef struct {
    uint16_t min_us;        // Minimum pulse width (full left endpoint)
    uint16_t max_us;        // Maximum pulse width (full right endpoint)
    int16_t subtrim;        // Mechanical center offset, shifts endpoints too (-200 to +200 µs)
    int16_t trim;           // Operational center offset, endpoints stay fixed (-200 to +200 µs)
    bool reversed;          // Invert servo direction
} servo_tuning_t;

// Steering geometry settings
typedef struct {
    uint8_t axle_ratio[4];       // Steering ratio for each axle (0-100%)
    uint8_t all_axle_rear_ratio; // Rear axle ratio in all-axle mode (0-100%)
    uint8_t expo;                // Steering expo curve (0-100%, 0=linear)
    uint8_t speed_steering;      // Speed-dependent steering reduction (0-100%, 0=disabled)
} steering_tuning_t;

// ESC/Motor tuning settings
typedef struct {
    uint8_t fwd_limit;          // Forward speed limit (0-100%)
    uint8_t rev_limit;          // Reverse speed limit (0-100%)
    int8_t subtrim;             // Neutral offset (-100 to +100 µs)
    uint8_t deadzone;           // Deadzone around neutral (0-100)
    bool reversed;              // Invert throttle direction
    bool realistic_throttle;    // Enable realistic coasting/drag brake behavior
    uint8_t coast_rate;         // Coast deceleration rate (0-100, higher = slower coast)
    uint8_t brake_force;        // Active brake strength (0-100%, how hard braking stops you)
} esc_tuning_t;

// Complete tuning configuration
typedef struct {
    uint32_t magic;             // Magic number to verify valid data
    uint32_t version;           // Tuning data version
    servo_tuning_t servos[SERVO_COUNT];
    steering_tuning_t steering;
    esc_tuning_t esc;
} tuning_config_t;

#define TUNING_MAGIC            0x54554E45  // "TUNE" in hex
#define TUNING_VERSION          7           // Bumped: drag_brake -> brake_force

// Default tuning values
#define TUNING_DEFAULT_SERVO_MIN        1000
#define TUNING_DEFAULT_SERVO_MAX        2000
#define TUNING_DEFAULT_SUBTRIM          0
#define TUNING_DEFAULT_TRIM             0
#define TUNING_DEFAULT_AXLE1_RATIO      100
#define TUNING_DEFAULT_AXLE2_RATIO      70
#define TUNING_DEFAULT_AXLE3_RATIO      70
#define TUNING_DEFAULT_AXLE4_RATIO      100
#define TUNING_DEFAULT_ALL_AXLE_REAR    80
#define TUNING_DEFAULT_EXPO             0
#define TUNING_DEFAULT_SPEED_STEERING   0       // 0=disabled, 100=max reduction at full throttle
#define TUNING_DEFAULT_FWD_LIMIT        100
#define TUNING_DEFAULT_REV_LIMIT        100
#define TUNING_DEFAULT_ESC_DEADZONE     30
#define TUNING_DEFAULT_REALISTIC        false   // Default to instant response (current behavior)
#define TUNING_DEFAULT_COAST_RATE       50      // Medium coast speed (0=fast stop, 100=slow coast)
#define TUNING_DEFAULT_BRAKE_FORCE      50      // Medium brake strength (0=weak, 100=instant stop)

// ============================================================================
// WIFI STATION MODE CONFIGURATION
// ============================================================================

#define WIFI_STA_SSID_MAX_LEN   32
#define WIFI_STA_PASS_MAX_LEN   64

// WiFi STA configuration (for connecting to external network)
// Named crawler_wifi_config_t to avoid conflict with ESP-IDF's wifi_sta_config_t
typedef struct {
    uint32_t magic;                             // Magic number to verify valid data
    bool enabled;                               // Whether STA mode is enabled
    char ssid[WIFI_STA_SSID_MAX_LEN + 1];       // SSID to connect to
    char password[WIFI_STA_PASS_MAX_LEN + 1];   // Password
    bool connected;                             // Last known connection status (runtime only)
} crawler_wifi_config_t;

#define CRAWLER_WIFI_MAGIC      0x57494649  // "WIFI" in hex

// ============================================================================
// MCPWM CONFIGURATION
// ============================================================================

// MCPWM groups and timers allocation
// Group 0: RC input capture + ESC output
// Using separate timers within the group

#define MCPWM_GROUP_RC_ESC      0
#define MCPWM_GROUP_SERVOS      1   // Group 1 for servo outputs

// Timer resolution
#define MCPWM_TIMER_RESOLUTION_HZ   1000000  // 1MHz = 1us resolution
#define MCPWM_CAPTURE_RESOLUTION_HZ 80000000 // 80MHz APB clock (both ESP32 and ESP32-S3)

#endif // CONFIG_H
