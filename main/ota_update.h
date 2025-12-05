#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include "esp_err.h"
#include "esp_http_server.h"

// OTA update status
typedef enum {
    OTA_STATUS_IDLE,
    OTA_STATUS_IN_PROGRESS,
    OTA_STATUS_SUCCESS,
    OTA_STATUS_FAILED
} ota_status_t;

// OTA progress info
typedef struct {
    ota_status_t status;
    int progress_percent;
    size_t bytes_received;
    size_t total_size;
    char error_msg[64];
} ota_progress_t;

/**
 * @brief Initialize OTA update module
 * @return ESP_OK on success
 */
esp_err_t ota_update_init(void);

/**
 * @brief Register OTA HTTP handlers with the server
 * @param server HTTP server handle
 * @return ESP_OK on success
 */
esp_err_t ota_register_handlers(httpd_handle_t server);

/**
 * @brief Get current OTA progress
 * @return Current progress info
 */
ota_progress_t ota_get_progress(void);

/**
 * @brief Mark current app as valid (call after successful boot)
 * @return ESP_OK on success
 */
esp_err_t ota_mark_valid(void);

#endif // OTA_UPDATE_H
