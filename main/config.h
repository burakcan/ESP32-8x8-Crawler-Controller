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
// PIN DEFINITIONS
// ============================================================================

// RC Receiver Input Pins (directly from receiver channels)
#define PIN_RC_THROTTLE     34  // Channel 1 - Throttle (GPIO 34 - input only)
#define PIN_RC_STEERING     35  // Channel 2 - Steering (GPIO 35 - input only)
#define PIN_RC_AUX1         32  // Channel 3 - Aux 1 / Mode select
#define PIN_RC_AUX2         33  // Channel 4 - Aux 2 / Extra

// ESC Output Pin
#define PIN_ESC             18  // Main drive motor ESC

// Servo Output Pins (4 servos for 8x8 steering - one per axle)
// Axle numbering: 1=front, 2, 3, 4=rear
// Axles 1-2 steer together, Axles 3-4 steer together
#define PIN_SERVO_AXLE_1    19  // Axle 1 (front) steering servo
#define PIN_SERVO_AXLE_2    21  // Axle 2 steering servo
#define PIN_SERVO_AXLE_3    22  // Axle 3 steering servo
#define PIN_SERVO_AXLE_4    23  // Axle 4 (rear) steering servo

// Status LED (optional - use built-in LED on DevKit)
#define PIN_STATUS_LED      2   // Built-in LED on most ESP32 DevKits

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

// Number of RC channels to calibrate
#define RC_CHANNEL_COUNT        4

// Channel indices
typedef enum {
    RC_CH_THROTTLE = 0,
    RC_CH_STEERING = 1,
    RC_CH_AUX1 = 2,
    RC_CH_AUX2 = 3
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
    STEER_MODE_SPIN,            // Maximum turn (spin in place)
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
#define MCPWM_CAPTURE_RESOLUTION_HZ 80000000 // 80MHz (fixed on ESP32)

#endif // CONFIG_H
