/**
 * @file web_server.h
 * @brief Web server for real-time status dashboard
 * 
 * Creates a WiFi access point and serves a web dashboard
 * accessible from any device (phone, tablet, PC).
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "config.h"
#include "rc_input.h"
#include "esp_err.h"

// WiFi AP configuration
#define WIFI_AP_SSID        "8x8-Crawler"
#define WIFI_AP_PASS        "crawler88"  // Min 8 chars
#define WIFI_AP_CHANNEL     1
#define WIFI_AP_MAX_CONN    4

// mDNS hostname (accessible as 8x8-crawler.local)
#define WIFI_MDNS_HOSTNAME  "8x8-crawler"

/**
 * @brief Status data structure sent to web clients
 */
typedef struct {
    // RC channel values (-1000 to +1000)
    int16_t rc_throttle;
    int16_t rc_steering;
    int16_t rc_aux1;
    int16_t rc_aux2;
    int16_t rc_aux3;
    int16_t rc_aux4;

    // Raw pulse widths (for debugging - all 6 channels)
    uint16_t rc_raw[6];

    // Output values
    uint16_t esc_pulse;
    uint16_t servo_a1;   // Axle 1 (front)
    uint16_t servo_a2;   // Axle 2
    uint16_t servo_a3;   // Axle 3
    uint16_t servo_a4;   // Axle 4 (rear)

    // System status
    uint8_t steering_mode;
    bool signal_lost;
    bool calibrated;
    bool calibrating;
    uint8_t cal_progress;

    // Uptime (milliseconds)
    uint32_t uptime_ms;

    // System stats (for monitor page)
    uint32_t heap_free;
    uint32_t heap_min;
    int8_t wifi_rssi;
} web_status_t;

/**
 * @brief Initialize WiFi AP and web server
 * @return ESP_OK on success
 */
esp_err_t web_server_init(void);

/**
 * @brief Update status data (call from main loop)
 * @param status Current status to broadcast to clients
 */
void web_server_update_status(const web_status_t *status);

/**
 * @brief Get the IP address of the AP
 * @return IP address string
 */
const char* web_server_get_ip(void);

/**
 * @brief Check if UI has overridden the steering mode
 * @param mode Pointer to store the mode value
 * @return true if UI override is active, false to use AUX channels
 */
bool web_server_get_mode_override(uint8_t *mode);

/**
 * @brief Clear the UI mode override (revert to AUX control)
 */
void web_server_clear_mode_override(void);

/**
 * @brief Get WiFi STA connection status
 * @return true if connected to external WiFi
 */
bool web_server_is_sta_connected(void);

/**
 * @brief Get WiFi STA IP address (when connected)
 * @return IP address string or empty string if not connected
 */
const char* web_server_get_sta_ip(void);

/**
 * @brief Enable/disable WiFi STA mode and optionally set credentials
 * @param enabled Whether to enable STA mode
 * @param ssid SSID to connect to (NULL to keep existing)
 * @param password Password (NULL to keep existing)
 * @return ESP_OK on success
 */
esp_err_t web_server_set_sta_config(bool enabled, const char *ssid, const char *password);

/**
 * @brief Get current WiFi STA configuration
 * @param config Pointer to config structure to fill
 */
void web_server_get_sta_config(crawler_wifi_config_t *config);

/**
 * @brief Check if servo test mode is active
 * @return true if servo test mode is active (servos controlled from UI)
 */
bool web_server_is_servo_test_active(void);

/**
 * @brief Update servo test timeout (call from main loop)
 * Automatically disables test mode after timeout
 */
void web_server_update_servo_test(void);

/**
 * @brief Enable WiFi (AP + optional STA)
 * Called when AUX3 button is held for 5 seconds
 */
void web_server_wifi_enable(void);

/**
 * @brief Disable WiFi completely (saves power)
 * Called when AUX3 button is held for 5 seconds while WiFi is on
 */
void web_server_wifi_disable(void);

/**
 * @brief Check if WiFi is currently enabled
 * @return true if WiFi is running
 */
bool web_server_wifi_is_enabled(void);

/**
 * @brief Initialize web server without starting WiFi
 * WiFi will be started later via web_server_wifi_enable()
 * @return ESP_OK on success
 */
esp_err_t web_server_init_no_wifi(void);

#endif // WEB_SERVER_H
