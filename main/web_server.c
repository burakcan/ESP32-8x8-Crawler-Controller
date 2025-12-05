/**
 * @file web_server.c
 * @brief Web server with SPIFFS file serving and WebSocket updates
 */

#include "web_server.h"
#include "ota_update.h"
#include "version.h"
#include "nvs_storage.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "lwip/ip4_addr.h"
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

// UI mode override
static bool ui_mode_override = false;
static uint8_t ui_mode_value = 0;

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
                ESP_LOGI(TAG, "WiFi STA: started, connecting...");
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGW(TAG, "WiFi STA: disconnected");
                sta_connected = false;
                sta_ip_addr_str[0] = '\0';
                if (sta_config.enabled) {
                    ESP_LOGI(TAG, "WiFi STA: reconnecting...");
                    esp_wifi_connect();
                }
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            snprintf(sta_ip_addr_str, sizeof(sta_ip_addr_str), IPSTR, IP2STR(&event->ip_info.ip));
            sta_connected = true;
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
    
    // Open file
    FILE *f = fopen(filepath, "r");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open: %s", filepath);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    // Set content type
    httpd_resp_set_type(req, get_mime_type(filepath));
    
    // Send file in chunks
    char buf[512];
    size_t read_bytes;
    while ((read_bytes = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, read_bytes) != ESP_OK) {
            fclose(f);
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }
    }
    fclose(f);
    
    // End response
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
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
 * @brief Start HTTP server
 */
static esp_err_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;  // Default is 8, need more for OTA + SPIFFS handlers
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;

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
    
    if (ws_fd < 0 || server == NULL) return;
    
    // Build JSON status
    // Keys: t=throttle, s=steering, x1/x2=aux, e=esc, a1-a4=axle servos, m=mode, ui=ui_override, sl=signal_lost, cd=calibrated, cg=calibrating, cp=cal_progress, u=uptime, v=version, b=build
    // WiFi: wse=wifi_sta_enabled, wsc=wifi_sta_connected, wss=wifi_sta_ssid, wsi=wifi_sta_ip
    char json[512];
    int len = snprintf(json, sizeof(json),
        "{\"t\":%d,\"s\":%d,\"x1\":%d,\"x2\":%d,\"e\":%u,"
        "\"a1\":%u,\"a2\":%u,\"a3\":%u,\"a4\":%u,"
        "\"m\":%u,\"ui\":%s,\"sl\":%s,\"cd\":%s,\"cg\":%s,\"cp\":%u,\"u\":%lu,"
        "\"v\":\"%s\",\"b\":%d,"
        "\"wse\":%s,\"wsc\":%s,\"wss\":\"%s\",\"wsi\":\"%s\"}",
        status->rc_throttle,
        status->rc_steering,
        status->rc_aux1,
        status->rc_aux2,
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
        status->uptime_sec,
        FW_VERSION,
        FW_BUILD_NUMBER,
        sta_config.enabled ? "true" : "false",
        sta_connected ? "true" : "false",
        sta_config.ssid,
        sta_ip_addr_str
    );
    
    httpd_ws_frame_t ws_pkt = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t*)json,
        .len = len
    };
    
    httpd_ws_send_frame_async(server, ws_fd, &ws_pkt);
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