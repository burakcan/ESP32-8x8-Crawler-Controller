/**
 * @file web_server.c
 * @brief Web server with SPIFFS file serving and WebSocket updates
 */

#include "web_server.h"
#include "ota_update.h"
#include "version.h"
#include "nvs_storage.h"
#include "tuning.h"
#include "calibration.h"
#include "pwm_output.h"
#include "engine_sound.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "mdns.h"
#include "lwip/ip4_addr.h"
#include "esp_system.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

static const char *TAG = "WEB_SERVER";

static httpd_handle_t server = NULL;
static int ws_fd = -1;
static web_status_t current_status = {0};
static char ap_ip_addr_str[16] = "192.168.4.1";
static char sta_ip_addr_str[16] = "";

// WiFi STA state
static crawler_wifi_config_t sta_config = {0};
static bool sta_connected = false;
static esp_netif_t *sta_netif = NULL;
static esp_netif_t *ap_netif = NULL;

// WiFi STA reconnect logic
static int sta_retry_count = 0;
static bool sta_give_up = false;
static uint8_t sta_disconnect_reason = 0;
static esp_timer_handle_t sta_connect_timer = NULL;
#define STA_MAX_RETRY 5
#define STA_RETRY_DELAY_MS 5000
#define STA_INITIAL_DELAY_MS 2000

// Timer callback for delayed WiFi connect (avoids blocking event loop)
static void sta_connect_timer_cb(void *arg) {
    ESP_LOGI(TAG, "WiFi STA: connecting now...");
    esp_wifi_connect();
}

// UI mode override
static bool ui_mode_override = false;
static uint8_t ui_mode_value = 0;

// Servo test mode - when active, servos are controlled directly from UI
static bool servo_test_active = false;
static uint32_t servo_test_timeout = 0;  // Auto-disable after timeout
#define SERVO_TEST_TIMEOUT_MS 30000  // 30 seconds

// WiFi power state
static bool wifi_enabled = false;
static bool wifi_initialized = false;

// MIME types for common files
static const struct {
    const char *ext;
    const char *mime;
} mime_types[] = {
    { ".html", "text/html" },
    { ".css",  "text/css" },
    { ".js",   "application/javascript" },
    { ".json", "application/json" },
    { ".png",  "image/png" },
    { ".jpg",  "image/jpeg" },
    { ".ico",  "image/x-icon" },
    { ".svg",  "image/svg+xml" },
};

/**
 * @brief Get MIME type for file extension
 */
static const char* get_mime_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (ext) {
        for (int i = 0; i < sizeof(mime_types) / sizeof(mime_types[0]); i++) {
            if (strcasecmp(ext, mime_types[i].ext) == 0) {
                return mime_types[i].mime;
            }
        }
    }
    return "text/plain";
}

/**
 * @brief Initialize SPIFFS filesystem
 */
static esp_err_t init_spiffs(void)
{
    ESP_LOGI(TAG, "Initializing SPIFFS...");
    
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/web",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = false
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount SPIFFS");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "SPIFFS partition not found");
        } else {
            ESP_LOGE(TAG, "SPIFFS init failed: %s", esp_err_to_name(ret));
        }
        return ret;
    }
    
    size_t total = 0, used = 0;
    ret = esp_spiffs_info("storage", &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS: %d KB total, %d KB used", total / 1024, used / 1024);
    }
    
    return ESP_OK;
}

/**
 * @brief Get human-readable WiFi disconnect reason
 * Reason codes from ESP-IDF esp_wifi_types.h (wifi_err_reason_t)
 * 1-24: IEEE 802.11 standard reasons
 * 200+: Espressif-specific reasons
 */
static const char* wifi_disconnect_reason_str(uint8_t reason)
{
    switch (reason) {
        // IEEE 802.11 standard reasons (1-24)
        case 1:  return "Unspecified";
        case 2:  return "Auth expired";
        case 3:  return "Deauth leaving";
        case 4:  return "Disassoc inactivity";
        case 5:  return "Disassoc too many";
        case 6:  return "Class 2 frame from non-auth";
        case 7:  return "Class 3 frame from non-assoc";
        case 8:  return "Disassoc leaving";
        case 9:  return "Not authenticated";
        case 10: return "Power cap bad";
        case 11: return "Channel bad";
        case 13: return "Invalid IE";
        case 14: return "MIC failure";
        case 15: return "4-way handshake timeout";
        case 16: return "Group key update timeout";
        case 17: return "Handshake element mismatch";
        case 18: return "Invalid group cipher";
        case 19: return "Invalid pairwise cipher";
        case 20: return "Invalid AKMP";
        case 21: return "Unsupported RSN IE version";
        case 22: return "Invalid RSN IE capabilities";
        case 23: return "802.1X auth failed";
        case 24: return "Cipher suite rejected";
        // Espressif-specific reasons (200+)
        case 200: return "Beacon timeout";
        case 201: return "No AP found";
        case 202: return "Auth failed";
        case 203: return "Assoc failed";
        case 204: return "Handshake timeout";
        case 205: return "Connection failed";
        case 206: return "AP TSF reset";
        case 207: return "Roaming";
        case 208: return "Assoc comeback time too long";
        case 209: return "SA query timeout";
        default: return "Unknown";
    }
}

/**
 * @brief WiFi event handler
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_AP_STACONNECTED:
                ESP_LOGI(TAG, "WiFi AP: client connected");
                break;
            case WIFI_EVENT_AP_STADISCONNECTED:
                ESP_LOGI(TAG, "WiFi AP: client disconnected");
                ws_fd = -1;
                break;
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi STA: started, connecting in %dms...", STA_INITIAL_DELAY_MS);
                sta_retry_count = 0;
                sta_give_up = false;
                // Use timer to delay first connection (don't block event loop!)
                if (sta_connect_timer == NULL) {
                    esp_timer_create_args_t timer_args = {
                        .callback = sta_connect_timer_cb,
                        .name = "sta_connect"
                    };
                    esp_timer_create(&timer_args, &sta_connect_timer);
                }
                esp_timer_start_once(sta_connect_timer, STA_INITIAL_DELAY_MS * 1000);
                break;
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
                sta_disconnect_reason = event->reason;
                ESP_LOGW(TAG, "WiFi STA: disconnected - %s (reason %d)",
                         wifi_disconnect_reason_str(event->reason), event->reason);
                sta_connected = false;
                sta_ip_addr_str[0] = '\0';

                if (sta_config.enabled && !sta_give_up) {
                    sta_retry_count++;
                    if (sta_retry_count <= STA_MAX_RETRY) {
                        ESP_LOGI(TAG, "WiFi STA: retry %d/%d in %dms...",
                                 sta_retry_count, STA_MAX_RETRY, STA_RETRY_DELAY_MS);
                        // Use timer for retry delay (don't block event loop!)
                        if (sta_connect_timer != NULL) {
                            esp_timer_start_once(sta_connect_timer, STA_RETRY_DELAY_MS * 1000);
                        }
                    } else {
                        ESP_LOGW(TAG, "WiFi STA: max retries reached, giving up. "
                                 "Will retry when credentials are updated.");
                        sta_give_up = true;
                    }
                }
                break;
            }
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            snprintf(sta_ip_addr_str, sizeof(sta_ip_addr_str), IPSTR, IP2STR(&event->ip_info.ip));
            sta_connected = true;
            sta_retry_count = 0;
            sta_give_up = false;
            sta_disconnect_reason = 0;  // Clear disconnect reason on success
            ESP_LOGI(TAG, "WiFi STA: connected, IP: %s", sta_ip_addr_str);
        }
    }
}

/**
 * @brief Start WiFi STA mode (connect to external network)
 */
static esp_err_t wifi_start_sta(void)
{
    if (!sta_config.enabled || sta_config.ssid[0] == '\0') {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "WiFi STA: connecting to '%s'", sta_config.ssid);

    wifi_config_t sta_wifi_config = {0};
    strncpy((char *)sta_wifi_config.sta.ssid, sta_config.ssid, sizeof(sta_wifi_config.sta.ssid) - 1);
    strncpy((char *)sta_wifi_config.sta.password, sta_config.password, sizeof(sta_wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_wifi_config));

    return ESP_OK;
}

/**
 * @brief Initialize WiFi in AP+STA dual mode
 */
static esp_err_t wifi_init_dual(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create network interfaces
    ap_netif = esp_netif_create_default_wifi_ap();
    sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, NULL));

    // Configure AP
    wifi_config_t ap_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .channel = WIFI_AP_CHANNEL,
            .password = WIFI_AP_PASS,
            .max_connection = WIFI_AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    // Set mode based on STA config
    if (sta_config.enabled && sta_config.ssid[0] != '\0') {
        ESP_LOGI(TAG, "WiFi mode: AP+STA (dual)");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    } else {
        ESP_LOGI(TAG, "WiFi mode: AP only");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    // Configure STA if enabled
    if (sta_config.enabled) {
        wifi_start_sta();
    }

    ESP_ERROR_CHECK(esp_wifi_start());

    // Initialize mDNS for hostname resolution
    esp_err_t mdns_ret = mdns_init();
    if (mdns_ret == ESP_OK) {
        mdns_hostname_set(WIFI_MDNS_HOSTNAME);
        mdns_instance_name_set("8x8 Crawler");
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
        ESP_LOGI(TAG, "mDNS: http://%s.local", WIFI_MDNS_HOSTNAME);
    } else {
        ESP_LOGW(TAG, "mDNS init failed: %s", esp_err_to_name(mdns_ret));
    }

    // Get AP IP address
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(ap_netif, &ip_info);
    snprintf(ap_ip_addr_str, sizeof(ap_ip_addr_str), IPSTR, IP2STR(&ip_info.ip));

    ESP_LOGI(TAG, "WiFi AP started. SSID: %s, Password: %s", WIFI_AP_SSID, WIFI_AP_PASS);
    ESP_LOGI(TAG, "AP IP: http://%s", ap_ip_addr_str);

    return ESP_OK;
}

/**
 * @brief Serve static file from SPIFFS
 */
static esp_err_t file_handler(httpd_req_t *req)
{
    char filepath[64];
    char uri_copy[48];
    const char *uri = req->uri;
    
    // Default to index.html
    if (strcmp(uri, "/") == 0) {
        uri = "/index.html";
    }
    
    // Copy URI with length limit
    strncpy(uri_copy, uri, sizeof(uri_copy) - 1);
    uri_copy[sizeof(uri_copy) - 1] = '\0';
    
    // Build filepath: /web + uri (max 4 + 47 = 51 chars, fits in 64)
    snprintf(filepath, sizeof(filepath), "/web%.47s", uri_copy);
    
    // Check if file exists
    struct stat st;
    if (stat(filepath, &st) != 0) {
        ESP_LOGW(TAG, "File not found: %s", filepath);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    
    // Open file in binary mode
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open: %s", filepath);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Set content type
    httpd_resp_set_type(req, get_mime_type(filepath));

    // Send file in chunks
    char buf[1024];
    size_t read_bytes;
    esp_err_t ret = ESP_OK;
    while ((read_bytes = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, read_bytes) != ESP_OK) {
            ESP_LOGE(TAG, "Failed sending %s", filepath);
            ret = ESP_FAIL;
            break;
        }
    }
    fclose(f);

    // End chunked response (only if we haven't failed)
    if (ret == ESP_OK) {
        httpd_resp_send_chunk(req, NULL, 0);
    }
    return ret;
}

/**
 * @brief Parse incoming WebSocket command
 * Format: {"cmd":"mode","v":0} or {"cmd":"aux"} to revert to AUX control
 */
static void parse_ws_command(const char *data, size_t len)
{
    // Simple parsing - look for "cmd":"mode" and "v":N
    if (strstr(data, "\"cmd\":\"mode\"") != NULL) {
        const char *v_pos = strstr(data, "\"v\":");
        if (v_pos) {
            int mode = atoi(v_pos + 4);
            if (mode >= 0 && mode <= 3) {
                ui_mode_override = true;
                ui_mode_value = (uint8_t)mode;
                ESP_LOGI(TAG, "UI mode override: %d", mode);
            }
        }
    } else if (strstr(data, "\"cmd\":\"aux\"") != NULL) {
        ui_mode_override = false;
        ESP_LOGI(TAG, "Mode control: AUX switches");
    }
}

/**
 * @brief WebSocket handler
 */
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WebSocket connected");
        ws_fd = httpd_req_to_sockfd(req);
        return ESP_OK;
    }
    
    // Handle incoming frames
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    
    // First call to get frame length
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Allocate buffer and receive payload
    if (ws_pkt.len > 0 && ws_pkt.len < 256) {
        uint8_t *buf = malloc(ws_pkt.len + 1);
        if (buf) {
            ws_pkt.payload = buf;
            ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
            if (ret == ESP_OK) {
                buf[ws_pkt.len] = '\0';
                parse_ws_command((const char *)buf, ws_pkt.len);
            }
            free(buf);
        }
    }
    
    return ESP_OK;
}

/**
 * @brief Restart device handler
 */
static esp_err_t restart_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Restart requested via API");
    httpd_resp_sendstr(req, "{\"status\":\"restarting\"}");

    // Give time for response to be sent
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();

    return ESP_OK; // Never reached
}

/**
 * @brief Enter bootloader (download) mode handler
 *
 * Uses chip_usb_set_persist_flags() to set the USB-Serial-JTAG to stay
 * in download mode after reset. This works on ESP32-S2/S3/C3/C6 with
 * native USB. For original ESP32, this just does a restart (user needs
 * to hold BOOT button or use esptool's reset sequence).
 */
static esp_err_t bootloader_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Bootloader mode requested via API");
    httpd_resp_sendstr(req, "{\"status\":\"entering_bootloader\"}");

    // Give time for response to be sent
    vTaskDelay(pdMS_TO_TICKS(500));

    // For chips with USB-Serial-JTAG (S2/S3/C3/C6), we could use:
    // chip_usb_set_persist_flags(USBDC_PERSIST_ENA);
    // But for plain ESP32, there's no software-only way to enter download mode.
    // The user needs to hold BOOT button during reset, or use esptool's
    // RTS/DTR reset sequence via USB-serial adapter.

    // Just restart - on native USB chips esptool can still flash during
    // the brief bootloader window, and for ESP32 with external USB-serial
    // the adapter's reset sequence handles it.
    esp_restart();

    return ESP_OK; // Never reached
}

/**
 * @brief WiFi settings GET handler - returns current config
 */
static esp_err_t wifi_get_handler(httpd_req_t *req)
{
    char response[256];
    snprintf(response, sizeof(response),
        "{\"enabled\":%s,\"connected\":%s,\"ssid\":\"%s\",\"ip\":\"%s\"}",
        sta_config.enabled ? "true" : "false",
        sta_connected ? "true" : "false",
        sta_config.ssid,
        sta_ip_addr_str);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

/**
 * @brief WiFi settings POST handler - updates config
 * Expects JSON: {"enabled":true/false,"ssid":"name","password":"pass"}
 */
static esp_err_t wifi_post_handler(httpd_req_t *req)
{
    char buf[256];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    ESP_LOGI(TAG, "WiFi config update: %s", buf);

    // Simple JSON parsing
    bool enabled = sta_config.enabled;
    char ssid[WIFI_STA_SSID_MAX_LEN + 1] = {0};
    char password[WIFI_STA_PASS_MAX_LEN + 1] = {0};
    bool has_ssid = false;
    bool has_password = false;

    // Parse enabled
    const char *enabled_pos = strstr(buf, "\"enabled\":");
    if (enabled_pos) {
        enabled = (strstr(enabled_pos, "true") != NULL);
    }

    // Parse ssid
    const char *ssid_pos = strstr(buf, "\"ssid\":\"");
    if (ssid_pos) {
        ssid_pos += 8;
        const char *end = strchr(ssid_pos, '"');
        if (end && (end - ssid_pos) < WIFI_STA_SSID_MAX_LEN) {
            strncpy(ssid, ssid_pos, end - ssid_pos);
            ssid[end - ssid_pos] = '\0';
            has_ssid = true;
        }
    }

    // Parse password
    const char *pass_pos = strstr(buf, "\"password\":\"");
    if (pass_pos) {
        pass_pos += 12;
        const char *end = strchr(pass_pos, '"');
        if (end && (end - pass_pos) < WIFI_STA_PASS_MAX_LEN) {
            strncpy(password, pass_pos, end - pass_pos);
            password[end - pass_pos] = '\0';
            has_password = true;
        }
    }

    // Apply config
    esp_err_t ret = web_server_set_sta_config(
        enabled,
        has_ssid ? ssid : NULL,
        has_password ? password : NULL
    );

    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save config");
        return ESP_FAIL;
    }

    // Return updated status
    char response[256];
    snprintf(response, sizeof(response),
        "{\"status\":\"ok\",\"enabled\":%s,\"ssid\":\"%s\"}",
        enabled ? "true" : "false",
        has_ssid ? ssid : sta_config.ssid);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

/**
 * @brief Tuning GET handler - returns current tuning config as JSON
 */
static esp_err_t tuning_get_handler(httpd_req_t *req)
{
    const tuning_config_t *cfg = tuning_get_config();

    // Build JSON response - servo settings (actual max ~450 bytes)
    char response[512];
    int len = snprintf(response, sizeof(response),
        "{"
        "\"servos\":["
        "{\"min\":%d,\"max\":%d,\"subtrim\":%d,\"trim\":%d,\"rev\":%s},"
        "{\"min\":%d,\"max\":%d,\"subtrim\":%d,\"trim\":%d,\"rev\":%s},"
        "{\"min\":%d,\"max\":%d,\"subtrim\":%d,\"trim\":%d,\"rev\":%s},"
        "{\"min\":%d,\"max\":%d,\"subtrim\":%d,\"trim\":%d,\"rev\":%s}"
        "],"
        "\"steering\":{"
        "\"ratio\":[%d,%d,%d,%d],"
        "\"allAxleRear\":%d,"
        "\"expo\":%d,"
        "\"speedSteering\":%d"
        "},"
        "\"esc\":{"
        "\"fwdLimit\":%d,"
        "\"revLimit\":%d,"
        "\"subtrim\":%d,"
        "\"deadzone\":%d,"
        "\"rev\":%s,"
        "\"realistic\":%s,"
        "\"coastRate\":%d,"
        "\"brakeForce\":%d,"
        "\"motorCutoff\":%d"
        "}"
        "}",
        cfg->servos[0].min_us, cfg->servos[0].max_us, cfg->servos[0].subtrim, cfg->servos[0].trim, cfg->servos[0].reversed ? "true" : "false",
        cfg->servos[1].min_us, cfg->servos[1].max_us, cfg->servos[1].subtrim, cfg->servos[1].trim, cfg->servos[1].reversed ? "true" : "false",
        cfg->servos[2].min_us, cfg->servos[2].max_us, cfg->servos[2].subtrim, cfg->servos[2].trim, cfg->servos[2].reversed ? "true" : "false",
        cfg->servos[3].min_us, cfg->servos[3].max_us, cfg->servos[3].subtrim, cfg->servos[3].trim, cfg->servos[3].reversed ? "true" : "false",
        cfg->steering.axle_ratio[0], cfg->steering.axle_ratio[1], cfg->steering.axle_ratio[2], cfg->steering.axle_ratio[3],
        cfg->steering.all_axle_rear_ratio,
        cfg->steering.expo,
        cfg->steering.speed_steering,
        cfg->esc.fwd_limit,
        cfg->esc.rev_limit,
        cfg->esc.subtrim,
        cfg->esc.deadzone,
        cfg->esc.reversed ? "true" : "false",
        cfg->esc.realistic_throttle ? "true" : "false",
        cfg->esc.coast_rate,
        cfg->esc.brake_force,
        cfg->esc.motor_cutoff
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, len);
    return ESP_OK;
}

/**
 * @brief Helper to parse integer from JSON
 */
static bool parse_json_int(const char *json, const char *key, int *value)
{
    char search[32];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *pos = strstr(json, search);
    if (pos) {
        pos += strlen(search);
        while (*pos == ' ') pos++;
        *value = atoi(pos);
        return true;
    }
    return false;
}

/**
 * @brief Helper to parse boolean from JSON
 */
static bool parse_json_bool(const char *json, const char *key, bool *value)
{
    char search[32];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *pos = strstr(json, search);
    if (pos) {
        pos += strlen(search);
        while (*pos == ' ') pos++;
        *value = (strncmp(pos, "true", 4) == 0);
        return true;
    }
    return false;
}

/**
 * @brief Tuning POST handler - updates tuning config
 */
static esp_err_t tuning_post_handler(httpd_req_t *req)
{
    char buf[1024];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    ESP_LOGI(TAG, "Tuning update: %s", buf);

    // Get current config and update it
    tuning_config_t cfg;
    memcpy(&cfg, tuning_get_config(), sizeof(tuning_config_t));

    // Parse servo settings (simplified - expects flat structure)
    int val;
    bool bval;

    // Servo 0
    if (parse_json_int(buf, "s0_min", &val)) cfg.servos[0].min_us = val;
    if (parse_json_int(buf, "s0_max", &val)) cfg.servos[0].max_us = val;
    if (parse_json_int(buf, "s0_subtrim", &val)) cfg.servos[0].subtrim = val;
    if (parse_json_int(buf, "s0_trim", &val)) cfg.servos[0].trim = val;
    if (parse_json_bool(buf, "s0_rev", &bval)) cfg.servos[0].reversed = bval;

    // Servo 1
    if (parse_json_int(buf, "s1_min", &val)) cfg.servos[1].min_us = val;
    if (parse_json_int(buf, "s1_max", &val)) cfg.servos[1].max_us = val;
    if (parse_json_int(buf, "s1_subtrim", &val)) cfg.servos[1].subtrim = val;
    if (parse_json_int(buf, "s1_trim", &val)) cfg.servos[1].trim = val;
    if (parse_json_bool(buf, "s1_rev", &bval)) cfg.servos[1].reversed = bval;

    // Servo 2
    if (parse_json_int(buf, "s2_min", &val)) cfg.servos[2].min_us = val;
    if (parse_json_int(buf, "s2_max", &val)) cfg.servos[2].max_us = val;
    if (parse_json_int(buf, "s2_subtrim", &val)) cfg.servos[2].subtrim = val;
    if (parse_json_int(buf, "s2_trim", &val)) cfg.servos[2].trim = val;
    if (parse_json_bool(buf, "s2_rev", &bval)) cfg.servos[2].reversed = bval;

    // Servo 3
    if (parse_json_int(buf, "s3_min", &val)) cfg.servos[3].min_us = val;
    if (parse_json_int(buf, "s3_max", &val)) cfg.servos[3].max_us = val;
    if (parse_json_int(buf, "s3_subtrim", &val)) cfg.servos[3].subtrim = val;
    if (parse_json_int(buf, "s3_trim", &val)) cfg.servos[3].trim = val;
    if (parse_json_bool(buf, "s3_rev", &bval)) cfg.servos[3].reversed = bval;

    // Steering geometry
    if (parse_json_int(buf, "ratio0", &val)) cfg.steering.axle_ratio[0] = val;
    if (parse_json_int(buf, "ratio1", &val)) cfg.steering.axle_ratio[1] = val;
    if (parse_json_int(buf, "ratio2", &val)) cfg.steering.axle_ratio[2] = val;
    if (parse_json_int(buf, "ratio3", &val)) cfg.steering.axle_ratio[3] = val;
    if (parse_json_int(buf, "allAxleRear", &val)) cfg.steering.all_axle_rear_ratio = val;
    if (parse_json_int(buf, "expo", &val)) cfg.steering.expo = val;
    if (parse_json_int(buf, "speedSteering", &val)) cfg.steering.speed_steering = val;

    // ESC settings
    if (parse_json_int(buf, "fwdLimit", &val)) cfg.esc.fwd_limit = val;
    if (parse_json_int(buf, "revLimit", &val)) cfg.esc.rev_limit = val;
    if (parse_json_int(buf, "escSubtrim", &val)) cfg.esc.subtrim = val;
    if (parse_json_int(buf, "deadzone", &val)) cfg.esc.deadzone = val;
    if (parse_json_bool(buf, "escRev", &bval)) cfg.esc.reversed = bval;
    if (parse_json_bool(buf, "realistic", &bval)) cfg.esc.realistic_throttle = bval;
    if (parse_json_int(buf, "coastRate", &val)) cfg.esc.coast_rate = val;
    if (parse_json_int(buf, "brakeForce", &val)) cfg.esc.brake_force = val;
    if (parse_json_int(buf, "motorCutoff", &val)) cfg.esc.motor_cutoff = val;

    // Apply and save
    tuning_set_config(&cfg);
    esp_err_t ret = tuning_save();

    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save tuning");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

/**
 * @brief Tuning reset handler - resets to defaults
 */
static esp_err_t tuning_reset_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Resetting tuning to defaults");

    esp_err_t ret = tuning_reset_defaults(true);

    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to reset tuning");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

// ============================================================================
// Sound Settings Handlers
// ============================================================================

/**
 * @brief Sound GET handler - returns current sound config as JSON
 */
static esp_err_t sound_get_handler(httpd_req_t *req)
{
    const engine_sound_config_t *cfg = engine_sound_get_config();
    sound_profile_t profile = engine_sound_get_profile();
    const char *profile_name = sound_profiles_get_name(profile);

    // Build JSON response (actual max ~650 bytes)
    char response[768];
    snprintf(response, sizeof(response),
        "{"
        "\"profile\":%d,"
        "\"profileName\":\"%s\","
        "\"masterVolumeLevel1\":%d,"
        "\"masterVolumeLevel2\":%d,"
        "\"activeVolumeLevel\":%d,"
        "\"volumePresetLow\":%d,"
        "\"volumePresetMedium\":%d,"
        "\"volumePresetHigh\":%d,"
        "\"currentVolumePreset\":%d,"
        "\"idleVolume\":%d,"
        "\"revVolume\":%d,"
        "\"knockVolume\":%d,"
        "\"startVolume\":%d,"
        "\"maxRpmPercent\":%d,"
        "\"acceleration\":%d,"
        "\"deceleration\":%d,"
        "\"revSwitchPoint\":%d,"
        "\"idleEndPoint\":%d,"
        "\"knockStartPoint\":%d,"
        "\"knockInterval\":%d,"
        "\"jakeBrakeEnabled\":%s,"
        "\"v8Mode\":%s,"
        "\"enabled\":%s,"
        "\"rpm\":%d,"
        "\"airBrakeEnabled\":%s,"
        "\"airBrakeVolume\":%d,"
        "\"reverseBeepEnabled\":%s,"
        "\"reverseBeepVolume\":%d,"
        "\"gearShiftEnabled\":%s,"
        "\"gearShiftVolume\":%d,"
        "\"wastegateEnabled\":%s,"
        "\"wastegateVolume\":%d,"
        "\"hornEnabled\":%s,"
        "\"hornVolume\":%d,"
        "\"hornType\":%d,"
        "\"modeSwitchEnabled\":%s,"
        "\"modeSwitchVolume\":%d"
        "}",
        profile,
        profile_name,
        cfg->master_volume_level1,
        cfg->master_volume_level2,
        cfg->active_volume_level,
        cfg->volume_preset_low,
        cfg->volume_preset_medium,
        cfg->volume_preset_high,
        engine_sound_get_current_volume_preset_index(),
        cfg->idle_volume,
        cfg->rev_volume,
        cfg->knock_volume,
        cfg->start_volume,
        cfg->max_rpm_percentage,
        cfg->acceleration,
        cfg->deceleration,
        cfg->rev_switch_point,
        cfg->idle_end_point,
        cfg->knock_start_point,
        cfg->knock_interval,
        cfg->jake_brake_enabled ? "true" : "false",
        cfg->v8_mode ? "true" : "false",
        engine_sound_is_enabled() ? "true" : "false",
        engine_sound_get_rpm(),
        cfg->air_brake_enabled ? "true" : "false",
        cfg->air_brake_volume,
        cfg->reverse_beep_enabled ? "true" : "false",
        cfg->reverse_beep_volume,
        cfg->gear_shift_enabled ? "true" : "false",
        cfg->gear_shift_volume,
        cfg->wastegate_enabled ? "true" : "false",
        cfg->wastegate_volume,
        cfg->horn_enabled ? "true" : "false",
        cfg->horn_volume,
        cfg->horn_type,
        cfg->mode_switch_sound_enabled ? "true" : "false",
        cfg->mode_switch_volume
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

/**
 * @brief Sound POST handler - updates sound config
 */
static esp_err_t sound_post_handler(httpd_req_t *req)
{
    char buf[1024];  // Increased for effect settings
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    ESP_LOGI(TAG, "Sound config update: %s", buf);

    // Get current config and update it
    engine_sound_config_t cfg;
    memcpy(&cfg, engine_sound_get_config(), sizeof(engine_sound_config_t));

    int val;
    bool bval;

    // Parse sound settings
    if (parse_json_int(buf, "profile", &val)) {
        // Profile change - apply it
        if (val >= 0 && val < SOUND_PROFILE_COUNT) {
            engine_sound_set_profile((sound_profile_t)val);
            cfg.profile = (sound_profile_t)val;
        }
    }
    if (parse_json_int(buf, "masterVolumeLevel1", &val)) cfg.master_volume_level1 = val;
    if (parse_json_int(buf, "masterVolumeLevel2", &val)) cfg.master_volume_level2 = val;
    if (parse_json_int(buf, "activeVolumeLevel", &val)) cfg.active_volume_level = val;
    if (parse_json_int(buf, "volumePresetLow", &val)) cfg.volume_preset_low = val;
    if (parse_json_int(buf, "volumePresetMedium", &val)) cfg.volume_preset_medium = val;
    if (parse_json_int(buf, "volumePresetHigh", &val)) cfg.volume_preset_high = val;
    if (parse_json_int(buf, "idleVolume", &val)) cfg.idle_volume = val;
    if (parse_json_int(buf, "revVolume", &val)) cfg.rev_volume = val;
    if (parse_json_int(buf, "knockVolume", &val)) cfg.knock_volume = val;
    if (parse_json_int(buf, "startVolume", &val)) cfg.start_volume = val;
    if (parse_json_int(buf, "maxRpmPercent", &val)) cfg.max_rpm_percentage = val;
    if (parse_json_int(buf, "acceleration", &val)) cfg.acceleration = val;
    if (parse_json_int(buf, "deceleration", &val)) cfg.deceleration = val;
    if (parse_json_int(buf, "revSwitchPoint", &val)) cfg.rev_switch_point = val;
    if (parse_json_int(buf, "idleEndPoint", &val)) cfg.idle_end_point = val;
    if (parse_json_int(buf, "knockStartPoint", &val)) cfg.knock_start_point = val;
    if (parse_json_int(buf, "knockInterval", &val)) cfg.knock_interval = val;
    if (parse_json_bool(buf, "jakeBrakeEnabled", &bval)) cfg.jake_brake_enabled = bval;
    if (parse_json_bool(buf, "v8Mode", &bval)) cfg.v8_mode = bval;
    if (parse_json_bool(buf, "enabled", &bval)) engine_sound_enable(bval);

    // Parse sound effect settings
    if (parse_json_bool(buf, "airBrakeEnabled", &bval)) cfg.air_brake_enabled = bval;
    if (parse_json_int(buf, "airBrakeVolume", &val)) cfg.air_brake_volume = val;
    if (parse_json_bool(buf, "reverseBeepEnabled", &bval)) cfg.reverse_beep_enabled = bval;
    if (parse_json_int(buf, "reverseBeepVolume", &val)) cfg.reverse_beep_volume = val;
    if (parse_json_bool(buf, "gearShiftEnabled", &bval)) cfg.gear_shift_enabled = bval;
    if (parse_json_int(buf, "gearShiftVolume", &val)) cfg.gear_shift_volume = val;
    if (parse_json_bool(buf, "wastegateEnabled", &bval)) cfg.wastegate_enabled = bval;
    if (parse_json_int(buf, "wastegateVolume", &val)) cfg.wastegate_volume = val;

    // Parse horn and mode switch settings
    if (parse_json_bool(buf, "hornEnabled", &bval)) cfg.horn_enabled = bval;
    if (parse_json_int(buf, "hornVolume", &val)) cfg.horn_volume = val;
    if (parse_json_int(buf, "hornType", &val)) cfg.horn_type = (horn_type_t)val;
    if (parse_json_bool(buf, "modeSwitchEnabled", &bval)) cfg.mode_switch_sound_enabled = bval;
    if (parse_json_int(buf, "modeSwitchVolume", &val)) cfg.mode_switch_volume = val;

    // Ensure magic and version are set correctly before saving
    cfg.magic = SOUND_CONFIG_MAGIC;
    cfg.version = SOUND_CONFIG_VERSION;

    // Apply configuration
    engine_sound_set_config(&cfg);

    // Save to NVS
    nvs_save_sound_config(&cfg, sizeof(engine_sound_config_t));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

/**
 * @brief Sound profiles GET handler - returns list of available profiles
 */
static esp_err_t sound_profiles_handler(httpd_req_t *req)
{
    char response[512];  // Sized for 3 profiles
    int len = 0;

    len += snprintf(response + len, sizeof(response) - len, "{\"profiles\":[");

    for (int i = 0; i < SOUND_PROFILE_COUNT; i++) {
        const sound_profile_def_t *profile = sound_profiles_get(i);
        if (i > 0) len += snprintf(response + len, sizeof(response) - len, ",");
        len += snprintf(response + len, sizeof(response) - len,
            "{\"id\":%d,\"name\":\"%s\",\"description\":\"%s\",\"cylinders\":%d,\"hasJakeBrake\":%s}",
            i, profile->name, profile->description, profile->cylinder_count,
            profile->has_jake_brake ? "true" : "false");
    }

    len += snprintf(response + len, sizeof(response) - len, "]}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

/**
 * @brief Build calibration JSON response
 */
static int build_calibration_response(char *buf, size_t bufsize, const char *result)
{
    calibration_status_t status;
    calibration_get_status(&status);
    const calibration_data_t *cal = calibration_get_data();

    return snprintf(buf, bufsize,
        "{%s\"step\":%d,\"channel\":%d,\"message\":\"%s\","
        "\"pulse\":%u,\"recCenter\":%u,\"recMin\":%u,\"recMax\":%u,"
        "\"valid\":%s,\"inProgress\":%s,"
        "\"channels\":["
        "{\"min\":%d,\"center\":%d,\"max\":%d,\"rev\":%s},"
        "{\"min\":%d,\"center\":%d,\"max\":%d,\"rev\":%s},"
        "{\"min\":%d,\"center\":%d,\"max\":%d,\"rev\":%s},"
        "{\"min\":%d,\"center\":%d,\"max\":%d,\"rev\":%s},"
        "{\"min\":%d,\"center\":%d,\"max\":%d,\"rev\":%s},"
        "{\"min\":%d,\"center\":%d,\"max\":%d,\"rev\":%s}]}",
        result ? result : "",
        status.step, status.channel, status.message,
        status.current_pulse, status.recorded_center, status.recorded_min, status.recorded_max,
        calibration_is_valid() ? "true" : "false",
        calibration_in_progress() ? "true" : "false",
        cal->channels[0].min, cal->channels[0].center, cal->channels[0].max, cal->channels[0].reversed ? "true" : "false",
        cal->channels[1].min, cal->channels[1].center, cal->channels[1].max, cal->channels[1].reversed ? "true" : "false",
        cal->channels[2].min, cal->channels[2].center, cal->channels[2].max, cal->channels[2].reversed ? "true" : "false",
        cal->channels[3].min, cal->channels[3].center, cal->channels[3].max, cal->channels[3].reversed ? "true" : "false",
        cal->channels[4].min, cal->channels[4].center, cal->channels[4].max, cal->channels[4].reversed ? "true" : "false",
        cal->channels[5].min, cal->channels[5].center, cal->channels[5].max, cal->channels[5].reversed ? "true" : "false");
}

/**
 * @brief Calibration GET handler - returns current calibration status
 */
static esp_err_t calibration_get_handler(httpd_req_t *req)
{
    char response[640];
    build_calibration_response(response, sizeof(response), NULL);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

/**
 * @brief Calibration POST handler - manual per-channel calibration
 * Actions:
 *   {"action":"start","channel":0-5}  - Start calibrating a channel
 *   {"action":"next"}                 - Confirm current step, move to next
 *   {"action":"cancel"}               - Cancel calibration
 *   {"action":"clear","channel":0-5}  - Clear channel calibration
 *   {"action":"clearAll"}             - Clear all calibration
 *   {"action":"reverse","channel":0-5,"value":true/false} - Set reversed
 */
static esp_err_t calibration_post_handler(httpd_req_t *req)
{
    char buf[128];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    ESP_LOGI(TAG, "Calibration action: %s", buf);

    const char *result_str = NULL;

    // Parse channel number if present
    int channel = -1;
    const char *ch_pos = strstr(buf, "\"channel\":");
    if (ch_pos) {
        channel = atoi(ch_pos + 10);
    }

    // Parse action
    if (strstr(buf, "\"start\"") != NULL && channel >= 0 && channel < RC_CHANNEL_COUNT) {
        esp_err_t ret = calibration_start_channel((rc_channel_t)channel);
        result_str = (ret == ESP_OK) ? "\"status\":\"started\"," : "\"status\":\"failed\",";
    } else if (strstr(buf, "\"next\"") != NULL) {
        esp_err_t ret = calibration_confirm_step();
        result_str = (ret == ESP_OK) ? "\"status\":\"ok\"," : "\"status\":\"failed\",";
    } else if (strstr(buf, "\"cancel\"") != NULL) {
        calibration_cancel();
        result_str = "\"status\":\"cancelled\",";
    } else if (strstr(buf, "\"clearAll\"") != NULL) {
        calibration_clear();
        result_str = "\"status\":\"cleared\",";
    } else if (strstr(buf, "\"clear\"") != NULL && channel >= 0 && channel < RC_CHANNEL_COUNT) {
        calibration_clear_channel((rc_channel_t)channel);
        result_str = "\"status\":\"cleared\",";
    } else if (strstr(buf, "\"reverse\"") != NULL && channel >= 0 && channel < RC_CHANNEL_COUNT) {
        bool reversed = (strstr(buf, "\"value\":true") != NULL);
        calibration_set_reversed((rc_channel_t)channel, reversed);
        result_str = "\"status\":\"ok\",";
    } else {
        result_str = "\"status\":\"unknown\",";
    }

    // Return full calibration state
    char response[640];
    build_calibration_response(response, sizeof(response), result_str);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

/**
 * @brief Servo test GET handler - returns test mode status
 */
static esp_err_t servo_test_get_handler(httpd_req_t *req)
{
    char response[128];
    snprintf(response, sizeof(response),
        "{\"active\":%s,\"pulses\":[%u,%u,%u,%u]}",
        servo_test_active ? "true" : "false",
        servo_get_pulse(SERVO_AXLE_1),
        servo_get_pulse(SERVO_AXLE_2),
        servo_get_pulse(SERVO_AXLE_3),
        servo_get_pulse(SERVO_AXLE_4));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

/**
 * @brief Servo test POST handler - enable/disable test mode and set servo positions
 * Expects JSON: {"active":true/false} or {"servo":0-3,"pulse":1000-2000}
 */
static esp_err_t servo_test_post_handler(httpd_req_t *req)
{
    char buf[128];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    ESP_LOGI(TAG, "Servo test: %s", buf);

    // Check for active toggle
    const char *active_pos = strstr(buf, "\"active\":");
    if (active_pos) {
        servo_test_active = (strstr(active_pos, "true") != NULL);
        if (servo_test_active) {
            servo_test_timeout = (uint32_t)(esp_timer_get_time() / 1000) + SERVO_TEST_TIMEOUT_MS;
            ESP_LOGI(TAG, "Servo test mode ENABLED (30s timeout)");
        } else {
            ESP_LOGI(TAG, "Servo test mode DISABLED");
        }
    }

    // Check for values array format: {"active":true,"values":[-500,0,500,0]}
    // Values are -1000 to +1000, need to convert to pulse using tuning_calc_servo_pulse
    const char *values_pos = strstr(buf, "\"values\":[");
    if (values_pos && servo_test_active) {
        values_pos += 10; // Skip past "\"values\":["
        int values[4];
        int count = 0;
        while (count < 4 && *values_pos) {
            // Skip whitespace
            while (*values_pos == ' ') values_pos++;
            if (*values_pos == '-' || (*values_pos >= '0' && *values_pos <= '9')) {
                values[count++] = atoi(values_pos);
                // Skip past this number
                if (*values_pos == '-') values_pos++;
                while (*values_pos >= '0' && *values_pos <= '9') values_pos++;
            }
            // Skip comma or break at ]
            while (*values_pos == ' ' || *values_pos == ',') values_pos++;
            if (*values_pos == ']') break;
        }

        if (count == 4) {
            for (int i = 0; i < 4; i++) {
                // Clamp to valid range
                if (values[i] < -1000) values[i] = -1000;
                if (values[i] > 1000) values[i] = 1000;
                // Calculate pulse using tuning
                uint16_t pulse = tuning_calc_servo_pulse(i, values[i]);
                servo_set_pulse((servo_id_t)i, pulse);
            }
            // Refresh timeout on activity
            servo_test_timeout = (uint32_t)(esp_timer_get_time() / 1000) + SERVO_TEST_TIMEOUT_MS;
            ESP_LOGI(TAG, "Servo test: [%d, %d, %d, %d]", values[0], values[1], values[2], values[3]);
        }
    }

    // Also support individual servo command: {"servo":0,"pulse":1500}
    int servo_idx = -1, pulse = -1;
    const char *servo_pos = strstr(buf, "\"servo\":");
    const char *pulse_pos = strstr(buf, "\"pulse\":");

    if (servo_pos && pulse_pos) {
        servo_idx = atoi(servo_pos + 8);
        pulse = atoi(pulse_pos + 8);

        if (servo_test_active && servo_idx >= 0 && servo_idx < SERVO_COUNT &&
            pulse >= SERVO_MIN_US && pulse <= SERVO_MAX_US) {
            servo_set_pulse((servo_id_t)servo_idx, pulse);
            // Refresh timeout on activity
            servo_test_timeout = (uint32_t)(esp_timer_get_time() / 1000) + SERVO_TEST_TIMEOUT_MS;
            ESP_LOGI(TAG, "Servo %d set to %d us", servo_idx, pulse);
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

/**
 * @brief Start HTTP server
 */
static esp_err_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;      // Increased from 4096 for file serving buffer
    config.max_uri_handlers = 24;  // Need extra for calibration + servo test APIs
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.recv_wait_timeout = 120;  // 2 minutes for OTA uploads (default is 5)
    config.send_wait_timeout = 120;

    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    // WebSocket handler (must be registered first for /ws)
    httpd_uri_t ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = NULL,
        .is_websocket = true
    };
    httpd_register_uri_handler(server, &ws);

    // Register OTA handlers (before wildcard file handler)
    ota_register_handlers(server);

    // WiFi settings API - GET
    httpd_uri_t wifi_get = {
        .uri = "/api/wifi",
        .method = HTTP_GET,
        .handler = wifi_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &wifi_get);

    // WiFi settings API - POST
    httpd_uri_t wifi_post = {
        .uri = "/api/wifi",
        .method = HTTP_POST,
        .handler = wifi_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &wifi_post);

    // System restart API
    httpd_uri_t restart = {
        .uri = "/api/restart",
        .method = HTTP_POST,
        .handler = restart_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &restart);

    // Bootloader mode API
    httpd_uri_t bootloader = {
        .uri = "/api/bootloader",
        .method = HTTP_POST,
        .handler = bootloader_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &bootloader);

    // Tuning API - GET
    httpd_uri_t tuning_get = {
        .uri = "/api/tuning",
        .method = HTTP_GET,
        .handler = tuning_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &tuning_get);

    // Tuning API - POST
    httpd_uri_t tuning_post = {
        .uri = "/api/tuning",
        .method = HTTP_POST,
        .handler = tuning_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &tuning_post);

    // Tuning reset API - POST
    httpd_uri_t tuning_reset = {
        .uri = "/api/tuning/reset",
        .method = HTTP_POST,
        .handler = tuning_reset_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &tuning_reset);

    // Calibration API - GET
    httpd_uri_t cal_get = {
        .uri = "/api/calibration",
        .method = HTTP_GET,
        .handler = calibration_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &cal_get);

    // Calibration API - POST
    httpd_uri_t cal_post = {
        .uri = "/api/calibration",
        .method = HTTP_POST,
        .handler = calibration_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &cal_post);

    // Servo test API - GET
    httpd_uri_t servo_get = {
        .uri = "/api/servo",
        .method = HTTP_GET,
        .handler = servo_test_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &servo_get);

    // Servo test API - POST
    httpd_uri_t servo_post = {
        .uri = "/api/servo",
        .method = HTTP_POST,
        .handler = servo_test_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &servo_post);

    // Sound API - GET
    httpd_uri_t sound_get = {
        .uri = "/api/sound",
        .method = HTTP_GET,
        .handler = sound_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &sound_get);

    // Sound API - POST
    httpd_uri_t sound_post = {
        .uri = "/api/sound",
        .method = HTTP_POST,
        .handler = sound_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &sound_post);

    // Sound profiles API - GET
    httpd_uri_t sound_profiles = {
        .uri = "/api/sound/profiles",
        .method = HTTP_GET,
        .handler = sound_profiles_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &sound_profiles);

    // Static file handler (wildcard for all other requests)
    httpd_uri_t file = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = file_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &file);

    return ESP_OK;
}

esp_err_t web_server_init(void)
{
    ESP_LOGI(TAG, "Initializing web server...");

    // Initialize SPIFFS for static files
    esp_err_t ret = init_spiffs();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS not available, web UI disabled");
        // Continue anyway - WiFi AP still works
    }

    // Load WiFi STA configuration from NVS
    ret = nvs_load_wifi_config(&sta_config);
    if (ret != ESP_OK) {
        nvs_get_default_wifi_config(&sta_config);
    }

    // Initialize WiFi (AP always, STA if enabled)
    ret = wifi_init_dual();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = start_webserver();
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "Web server ready!");
    return ESP_OK;
}

void web_server_update_status(const web_status_t *status)
{
    if (status == NULL) return;

    memcpy(&current_status, status, sizeof(web_status_t));

    // Capture ws_fd atomically to avoid TOCTOU race with disconnect handler
    int fd = ws_fd;
    if (fd < 0 || server == NULL) return;

    // Build JSON status
    // Main: t=throttle, s=steering, x1-x4=aux channels, e=esc, a1-a4=axle servos, m=mode
    // Status: ui=ui_override, sl=signal_lost, cd=calibrated, cg=calibrating, cp=cal_progress
    // Info: u=uptime_ms, v=version, b=build
    // Monitor: rc=raw_pulses[6], h=heap_free, hm=heap_min, rs=rssi
    // WiFi: wse=wifi_sta_enabled, wsc=wifi_sta_connected, wss=wifi_sta_ssid, wsi=wifi_sta_ip
    //       wsr=wifi_sta_reason (disconnect reason code), wsrs=wifi_sta_reason_str

    char json[896];
    int len = snprintf(json, sizeof(json),
        "{\"t\":%d,\"s\":%d,\"x1\":%d,\"x2\":%d,\"x3\":%d,\"x4\":%d,\"e\":%u,"
        "\"a1\":%u,\"a2\":%u,\"a3\":%u,\"a4\":%u,"
        "\"m\":%u,\"ui\":%s,\"sl\":%s,\"cd\":%s,\"cg\":%s,\"cp\":%u,"
        "\"u\":%lu,\"v\":\"%s\",\"b\":\"%s\","
        "\"rc\":[%u,%u,%u,%u,%u,%u],\"h\":%lu,\"hm\":%lu,\"rs\":%d,"
        "\"wse\":%s,\"wsc\":%s,\"wss\":\"%s\",\"wsi\":\"%s\",\"wsr\":%u,\"wsrs\":\"%s\"}",
        status->rc_throttle,
        status->rc_steering,
        status->rc_aux1,
        status->rc_aux2,
        status->rc_aux3,
        status->rc_aux4,
        status->esc_pulse,
        status->servo_a1,
        status->servo_a2,
        status->servo_a3,
        status->servo_a4,
        status->steering_mode,
        ui_mode_override ? "true" : "false",
        status->signal_lost ? "true" : "false",
        status->calibrated ? "true" : "false",
        status->calibrating ? "true" : "false",
        status->cal_progress,
        (unsigned long)status->uptime_ms,
        FW_VERSION,
        FW_BUILD_DATE,
        status->rc_raw[0], status->rc_raw[1], status->rc_raw[2], status->rc_raw[3], status->rc_raw[4], status->rc_raw[5],
        (unsigned long)status->heap_free,
        (unsigned long)status->heap_min,
        status->wifi_rssi,
        sta_config.enabled ? "true" : "false",
        sta_connected ? "true" : "false",
        sta_config.ssid,
        sta_ip_addr_str,
        sta_disconnect_reason,
        sta_disconnect_reason ? wifi_disconnect_reason_str(sta_disconnect_reason) : ""
    );

    httpd_ws_frame_t ws_pkt = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t*)json,
        .len = len
    };

    esp_err_t ret = httpd_ws_send_frame_async(server, fd, &ws_pkt);
    if (ret != ESP_OK) {
        // Client disconnected - clear the socket fd so we stop trying
        ws_fd = -1;
    }
}

const char* web_server_get_ip(void)
{
    return ap_ip_addr_str;
}

bool web_server_get_mode_override(uint8_t *mode)
{
    if (ui_mode_override && mode) {
        *mode = ui_mode_value;
    }
    return ui_mode_override;
}

void web_server_clear_mode_override(void)
{
    ui_mode_override = false;
}

bool web_server_is_sta_connected(void)
{
    return sta_connected;
}

const char* web_server_get_sta_ip(void)
{
    return sta_ip_addr_str;
}

esp_err_t web_server_set_sta_config(bool enabled, const char *ssid, const char *password)
{
    bool need_restart = false;

    // Update config
    if (ssid != NULL) {
        strncpy(sta_config.ssid, ssid, WIFI_STA_SSID_MAX_LEN);
        sta_config.ssid[WIFI_STA_SSID_MAX_LEN] = '\0';
    }
    if (password != NULL) {
        strncpy(sta_config.password, password, WIFI_STA_PASS_MAX_LEN);
        sta_config.password[WIFI_STA_PASS_MAX_LEN] = '\0';
    }

    // Check if mode change needed
    if (sta_config.enabled != enabled) {
        need_restart = true;
    }
    sta_config.enabled = enabled;
    sta_config.magic = CRAWLER_WIFI_MAGIC;

    // Save to NVS
    esp_err_t ret = nvs_save_wifi_config(&sta_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save WiFi STA config");
        return ret;
    }

    // Reset retry counter when credentials change
    sta_retry_count = 0;
    sta_give_up = false;

    // Apply changes
    if (need_restart) {
        if (enabled && sta_config.ssid[0] != '\0') {
            // Switch to AP+STA mode
            ESP_LOGI(TAG, "Enabling WiFi STA mode...");
            esp_wifi_set_mode(WIFI_MODE_APSTA);
            wifi_start_sta();
            esp_wifi_connect();
        } else {
            // Switch to AP only mode
            ESP_LOGI(TAG, "Disabling WiFi STA mode...");
            esp_wifi_disconnect();
            sta_connected = false;
            sta_ip_addr_str[0] = '\0';
            esp_wifi_set_mode(WIFI_MODE_AP);
        }
    } else if (enabled && ssid != NULL) {
        // Just update credentials and reconnect
        ESP_LOGI(TAG, "Updating WiFi STA credentials...");
        esp_wifi_disconnect();
        wifi_start_sta();
        esp_wifi_connect();
    }

    return ESP_OK;
}

void web_server_get_sta_config(crawler_wifi_config_t *config)
{
    if (config) {
        memcpy(config, &sta_config, sizeof(crawler_wifi_config_t));
        config->connected = sta_connected;
    }
}

bool web_server_is_servo_test_active(void)
{
    return servo_test_active;
}

void web_server_update_servo_test(void)
{
    if (servo_test_active) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if (now > servo_test_timeout) {
            servo_test_active = false;
            ESP_LOGI(TAG, "Servo test mode timed out - disabled");
        }
    }
}

bool web_server_wifi_is_enabled(void)
{
    return wifi_enabled;
}

void web_server_wifi_enable(void)
{
    if (wifi_enabled) {
        return;  // Already enabled
    }

    ESP_LOGI(TAG, "Enabling WiFi...");

    ESP_LOGI(TAG, "STA config: enabled=%d, ssid='%s'", sta_config.enabled, sta_config.ssid);
    if (!wifi_initialized) {
        // First time - full initialization
        wifi_init_dual();
        start_webserver();
        wifi_initialized = true;
    } else {
        // Re-enable WiFi
        esp_wifi_start();
    }

    wifi_enabled = true;
    ESP_LOGI(TAG, "WiFi enabled");
}

void web_server_wifi_disable(void)
{
    if (!wifi_enabled) {
        return;  // Already disabled
    }

    ESP_LOGI(TAG, "Disabling WiFi to save power...");

    // Cancel any pending STA connection timer
    if (sta_connect_timer != NULL) {
        esp_timer_stop(sta_connect_timer);
    }

    // Stop WiFi completely (turns off radio)
    esp_wifi_stop();

    wifi_enabled = false;
    ws_fd = -1;  // WebSocket disconnected
    sta_connected = false;

    ESP_LOGI(TAG, "WiFi disabled");
}

esp_err_t web_server_init_no_wifi(void)
{
    esp_err_t ret;

    // Initialize SPIFFS for serving web files
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/web",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = false
    };

    ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS (%s)", esp_err_to_name(ret));
        // Continue anyway - WiFi AP still works
    }

    // Load WiFi STA configuration from NVS
    ret = nvs_load_wifi_config(&sta_config);
    if (ret != ESP_OK) {
        nvs_get_default_wifi_config(&sta_config);
    }

    // WiFi stays OFF - will be enabled via web_server_wifi_enable()
    wifi_enabled = false;
    wifi_initialized = false;

    ESP_LOGI(TAG, "Web server initialized (WiFi OFF - hold AUX3 5sec to enable)");
    return ESP_OK;
}