#include "ota_update.h"
#include "version.h"

#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_spiffs.h"

static const char *TAG = "ota_update";

// OTA receive buffer size
#define OTA_BUFFER_SIZE 4096

// Current OTA progress (accessible for WebSocket status updates)
static ota_progress_t s_ota_progress = {
    .status = OTA_STATUS_IDLE,
    .progress_percent = 0,
    .bytes_received = 0,
    .total_size = 0,
    .error_msg = ""
};

// Timer for delayed reboot
static esp_timer_handle_t s_reboot_timer = NULL;

static void reboot_callback(void *arg)
{
    ESP_LOGI(TAG, "Rebooting...");
    esp_restart();
}

static void schedule_reboot(void)
{
    if (s_reboot_timer == NULL) {
        esp_timer_create_args_t timer_args = {
            .callback = reboot_callback,
            .arg = NULL,
            .name = "ota_reboot"
        };
        esp_timer_create(&timer_args, &s_reboot_timer);
    }
    // Reboot after 1 second to allow HTTP response to be sent
    esp_timer_start_once(s_reboot_timer, 1000000);
}

static void set_error(const char *msg)
{
    s_ota_progress.status = OTA_STATUS_FAILED;
    strncpy(s_ota_progress.error_msg, msg, sizeof(s_ota_progress.error_msg) - 1);
    s_ota_progress.error_msg[sizeof(s_ota_progress.error_msg) - 1] = '\0';
}

// HTTP POST handler for firmware upload
static esp_err_t ota_upload_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "OTA upload request received");

    // Reset progress
    s_ota_progress.status = OTA_STATUS_IN_PROGRESS;
    s_ota_progress.progress_percent = 0;
    s_ota_progress.bytes_received = 0;
    s_ota_progress.error_msg[0] = '\0';

    // Get content length
    int content_len = req->content_len;
    if (content_len <= 0) {
        set_error("No content");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No content");
        return ESP_FAIL;
    }

    s_ota_progress.total_size = content_len;
    ESP_LOGI(TAG, "Firmware size: %d bytes", content_len);

    // Get the next OTA partition
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(running);

    if (update_partition == NULL) {
        set_error("No update partition");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No update partition available");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Writing to partition: %s at 0x%lx", update_partition->label, update_partition->address);

    // Begin OTA update
    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_partition, content_len, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        set_error("OTA begin failed");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to start OTA");
        return err;
    }

    // Allocate receive buffer
    char *buffer = malloc(OTA_BUFFER_SIZE);
    if (buffer == NULL) {
        esp_ota_abort(ota_handle);
        set_error("Out of memory");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_ERR_NO_MEM;
    }

    // Receive and write firmware data
    int bytes_received = 0;
    while (bytes_received < content_len) {
        int to_read = content_len - bytes_received;
        if (to_read > OTA_BUFFER_SIZE) {
            to_read = OTA_BUFFER_SIZE;
        }

        int read_bytes = httpd_req_recv(req, buffer, to_read);
        if (read_bytes <= 0) {
            if (read_bytes == HTTPD_SOCK_ERR_TIMEOUT) {
                ESP_LOGW(TAG, "Timeout, retrying...");
                continue;
            }
            ESP_LOGE(TAG, "Connection closed after %d bytes", bytes_received);
            free(buffer);
            esp_ota_abort(ota_handle);
            set_error("Connection closed");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Connection closed");
            return ESP_FAIL;
        }

        // Write to OTA partition
        err = esp_ota_write(ota_handle, buffer, read_bytes);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            free(buffer);
            esp_ota_abort(ota_handle);
            set_error("Write failed");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
            return err;
        }

        bytes_received += read_bytes;
        s_ota_progress.bytes_received = bytes_received;
        s_ota_progress.progress_percent = (bytes_received * 100) / content_len;

        // Log progress every 10%
        static int last_logged = -1;
        int current_ten = s_ota_progress.progress_percent / 10;
        if (current_ten != last_logged) {
            ESP_LOGI(TAG, "Progress: %d%%", s_ota_progress.progress_percent);
            last_logged = current_ten;
        }
    }

    free(buffer);

    // Finalize OTA
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        set_error("Validation failed");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Image validation failed");
        return err;
    }

    // Set boot partition
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        set_error("Set boot partition failed");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set boot partition");
        return err;
    }

    s_ota_progress.status = OTA_STATUS_SUCCESS;
    s_ota_progress.progress_percent = 100;

    ESP_LOGI(TAG, "OTA update successful!");

    // Send success response
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"success\",\"message\":\"Update complete. Rebooting...\"}");

    // Schedule reboot
    schedule_reboot();

    return ESP_OK;
}

// HTTP GET handler for OTA status
static esp_err_t ota_status_handler(httpd_req_t *req)
{
    char response[256];
    const char *status_str;

    switch (s_ota_progress.status) {
        case OTA_STATUS_IDLE:
            status_str = "idle";
            break;
        case OTA_STATUS_IN_PROGRESS:
            status_str = "in_progress";
            break;
        case OTA_STATUS_SUCCESS:
            status_str = "success";
            break;
        case OTA_STATUS_FAILED:
            status_str = "failed";
            break;
        default:
            status_str = "unknown";
    }

    snprintf(response, sizeof(response),
             "{\"status\":\"%s\",\"progress\":%d,\"received\":%u,\"total\":%u,\"error\":\"%s\"}",
             status_str,
             s_ota_progress.progress_percent,
             (unsigned int)s_ota_progress.bytes_received,
             (unsigned int)s_ota_progress.total_size,
             s_ota_progress.error_msg);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

// HTTP POST handler for SPIFFS file upload
// Expects multipart form data with filename in URL: /api/spiffs?file=filename.ext
static esp_err_t spiffs_upload_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "SPIFFS upload request received");

    // Get filename from query string
    char query[64] = {0};
    char filename[48] = {0};

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing filename parameter");
        return ESP_FAIL;
    }

    if (httpd_query_key_value(query, "file", filename, sizeof(filename)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'file' parameter");
        return ESP_FAIL;
    }

    // Build full path
    char filepath[64];
    snprintf(filepath, sizeof(filepath), "/web/%s", filename);

    ESP_LOGI(TAG, "Uploading file: %s (%d bytes)", filepath, req->content_len);

    // Get content length
    int content_len = req->content_len;
    if (content_len <= 0 || content_len > 256 * 1024) {  // Max 256KB per file
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content length");
        return ESP_FAIL;
    }

    // Open file for writing
    FILE *f = fopen(filepath, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", filepath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
        return ESP_FAIL;
    }

    // Allocate receive buffer
    char *buffer = malloc(1024);
    if (buffer == NULL) {
        fclose(f);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_ERR_NO_MEM;
    }

    // Receive and write file data
    int bytes_received = 0;
    while (bytes_received < content_len) {
        int to_read = content_len - bytes_received;
        if (to_read > 1024) {
            to_read = 1024;
        }

        int read_bytes = httpd_req_recv(req, buffer, to_read);
        if (read_bytes <= 0) {
            if (read_bytes == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGE(TAG, "Connection closed");
            free(buffer);
            fclose(f);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Connection closed");
            return ESP_FAIL;
        }

        size_t written = fwrite(buffer, 1, read_bytes, f);
        if (written != read_bytes) {
            ESP_LOGE(TAG, "Write failed");
            free(buffer);
            fclose(f);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
            return ESP_FAIL;
        }

        bytes_received += read_bytes;
    }

    free(buffer);
    fclose(f);

    ESP_LOGI(TAG, "File uploaded successfully: %s (%d bytes)", filename, bytes_received);

    // Send success response
    httpd_resp_set_type(req, "application/json");
    char response[128];
    snprintf(response, sizeof(response), "{\"status\":\"success\",\"file\":\"%s\",\"size\":%d}", filename, bytes_received);
    httpd_resp_sendstr(req, response);

    return ESP_OK;
}

// HTTP DELETE handler for SPIFFS file deletion
static esp_err_t spiffs_delete_handler(httpd_req_t *req)
{
    // Get filename from query string
    char query[64] = {0};
    char filename[48] = {0};

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "file", filename, sizeof(filename)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'file' parameter");
        return ESP_FAIL;
    }

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "/web/%s", filename);

    if (remove(filepath) != 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "File deleted: %s", filename);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"success\"}");
    return ESP_OK;
}

// HTTP GET handler for SPIFFS file listing
static esp_err_t spiffs_list_handler(httpd_req_t *req)
{
    DIR *dir = opendir("/web");
    if (dir == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open directory");
        return ESP_FAIL;
    }

    // Get SPIFFS info
    size_t total = 0, used = 0;
    esp_spiffs_info("storage", &total, &used);

    httpd_resp_set_type(req, "application/json");

    // Build JSON response
    char response[512];
    int offset = snprintf(response, sizeof(response), "{\"total\":%u,\"used\":%u,\"files\":[",
                          (unsigned)total, (unsigned)used);

    struct dirent *entry;
    bool first = true;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {  // Regular file
            char filepath[280];  // /web/ + NAME_MAX (255)
            struct stat st;
            snprintf(filepath, sizeof(filepath), "/web/%s", entry->d_name);
            stat(filepath, &st);

            offset += snprintf(response + offset, sizeof(response) - offset,
                             "%s{\"name\":\"%s\",\"size\":%ld}",
                             first ? "" : ",", entry->d_name, st.st_size);
            first = false;
        }
    }
    closedir(dir);

    snprintf(response + offset, sizeof(response) - offset, "]}");
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

esp_err_t ota_update_init(void)
{
    ESP_LOGI(TAG, "OTA update module initialized");
    ESP_LOGI(TAG, "Firmware version: %s (build %d)", FW_VERSION, FW_BUILD_NUMBER);
    return ESP_OK;
}

esp_err_t ota_register_handlers(httpd_handle_t server)
{
    esp_err_t err;

    // Register POST handler for firmware upload
    httpd_uri_t ota_upload = {
        .uri = "/api/ota",
        .method = HTTP_POST,
        .handler = ota_upload_handler,
        .user_ctx = NULL
    };
    err = httpd_register_uri_handler(server, &ota_upload);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register OTA upload handler");
        return err;
    }

    // Register GET handler for status
    httpd_uri_t ota_status = {
        .uri = "/api/ota/status",
        .method = HTTP_GET,
        .handler = ota_status_handler,
        .user_ctx = NULL
    };
    err = httpd_register_uri_handler(server, &ota_status);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register OTA status handler");
        return err;
    }

    // Register SPIFFS file upload handler (POST /api/spiffs?file=filename)
    httpd_uri_t spiffs_upload = {
        .uri = "/api/spiffs",
        .method = HTTP_POST,
        .handler = spiffs_upload_handler,
        .user_ctx = NULL
    };
    err = httpd_register_uri_handler(server, &spiffs_upload);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register SPIFFS upload handler");
        return err;
    }

    // Register SPIFFS file list handler (GET /api/spiffs)
    httpd_uri_t spiffs_list = {
        .uri = "/api/spiffs",
        .method = HTTP_GET,
        .handler = spiffs_list_handler,
        .user_ctx = NULL
    };
    err = httpd_register_uri_handler(server, &spiffs_list);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register SPIFFS list handler");
        return err;
    }

    // Register SPIFFS file delete handler (DELETE /api/spiffs?file=filename)
    httpd_uri_t spiffs_delete = {
        .uri = "/api/spiffs",
        .method = HTTP_DELETE,
        .handler = spiffs_delete_handler,
        .user_ctx = NULL
    };
    err = httpd_register_uri_handler(server, &spiffs_delete);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register SPIFFS delete handler");
        return err;
    }

    ESP_LOGI(TAG, "OTA and SPIFFS handlers registered");
    return ESP_OK;
}

ota_progress_t ota_get_progress(void)
{
    return s_ota_progress;
}

esp_err_t ota_mark_valid(void)
{
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mark app as valid: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "App marked as valid, rollback cancelled");
    }
    return err;
}
