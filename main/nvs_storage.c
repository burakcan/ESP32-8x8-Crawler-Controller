/**
 * @file nvs_storage.c
 * @brief Non-Volatile Storage implementation for persistent configuration
 */

#include "nvs_storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "NVS_STORAGE";

esp_err_t nvs_storage_init(void)
{
    esp_err_t ret = nvs_flash_init();
    
    // If NVS partition is corrupted or has wrong format, erase and reinitialize
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition issue, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "NVS storage initialized");
    } else {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

esp_err_t nvs_save_calibration(const calibration_data_t *data)
{
    nvs_handle_t handle;
    esp_err_t ret;
    
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Save calibration as blob
    ret = nvs_set_blob(handle, NVS_KEY_CALIBRATION, data, sizeof(calibration_data_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write calibration: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }
    
    // Commit changes
    ret = nvs_commit(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Calibration saved to NVS");
    }
    
    nvs_close(handle);
    return ret;
}

esp_err_t nvs_load_calibration(calibration_data_t *data)
{
    nvs_handle_t handle;
    esp_err_t ret;
    
    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Get blob size first
    size_t required_size = sizeof(calibration_data_t);
    ret = nvs_get_blob(handle, NVS_KEY_CALIBRATION, data, &required_size);
    
    nvs_close(handle);
    
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No calibration found in NVS");
        return ret;
    }
    
    // Verify magic number and version
    if (data->magic != CALIBRATION_MAGIC) {
        ESP_LOGW(TAG, "Invalid calibration magic number");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (data->version != CALIBRATION_VERSION) {
        ESP_LOGW(TAG, "Calibration version mismatch (stored: %lu, current: %d)", 
                 data->version, CALIBRATION_VERSION);
        // Could add migration logic here for future versions
        return ESP_ERR_INVALID_VERSION;
    }
    
    ESP_LOGI(TAG, "Calibration loaded from NVS");
    return ESP_OK;
}

esp_err_t nvs_clear_calibration(void)
{
    nvs_handle_t handle;
    esp_err_t ret;
    
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    
    ret = nvs_erase_key(handle, NVS_KEY_CALIBRATION);
    if (ret == ESP_OK) {
        nvs_commit(handle);
        ESP_LOGI(TAG, "Calibration cleared from NVS");
    }
    
    nvs_close(handle);
    return ret;
}

bool nvs_has_calibration(void)
{
    calibration_data_t data;
    return (nvs_load_calibration(&data) == ESP_OK && data.calibrated);
}

void nvs_get_default_calibration(calibration_data_t *data)
{
    memset(data, 0, sizeof(calibration_data_t));
    
    data->magic = CALIBRATION_MAGIC;
    data->version = CALIBRATION_VERSION;
    data->calibrated = false;
    
    // Set defaults for all channels
    for (int i = 0; i < RC_CHANNEL_COUNT; i++) {
        data->channels[i].min = RC_DEFAULT_MIN_US;
        data->channels[i].center = RC_DEFAULT_CENTER_US;
        data->channels[i].max = RC_DEFAULT_MAX_US;
        data->channels[i].deadzone = DEFAULT_DEADZONE_US;
        data->channels[i].reversed = false;
    }
    
    ESP_LOGI(TAG, "Default calibration values set");
}

esp_err_t nvs_save_wifi_config(const crawler_wifi_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(ret));
        return ret;
    }

    // Save WiFi config as blob
    ret = nvs_set_blob(handle, NVS_KEY_WIFI_STA, config, sizeof(crawler_wifi_config_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write WiFi config: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    ret = nvs_commit(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "WiFi config saved to NVS");
    }

    nvs_close(handle);
    return ret;
}

esp_err_t nvs_load_wifi_config(crawler_wifi_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t required_size = sizeof(crawler_wifi_config_t);
    ret = nvs_get_blob(handle, NVS_KEY_WIFI_STA, config, &required_size);

    nvs_close(handle);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No WiFi config found in NVS");
        return ret;
    }

    // Verify magic number
    if (config->magic != CRAWLER_WIFI_MAGIC) {
        ESP_LOGW(TAG, "Invalid WiFi config magic number");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "WiFi config loaded from NVS (enabled: %s)", config->enabled ? "yes" : "no");
    return ESP_OK;
}

void nvs_get_default_wifi_config(crawler_wifi_config_t *config)
{
    memset(config, 0, sizeof(crawler_wifi_config_t));
    config->magic = CRAWLER_WIFI_MAGIC;
    config->enabled = false;
    config->ssid[0] = '\0';
    config->password[0] = '\0';
    config->connected = false;
    ESP_LOGI(TAG, "Default WiFi config set (disabled)");
}
