/**
 * @file nvs_storage.h
 * @brief Non-Volatile Storage interface for persistent configuration
 */

#ifndef NVS_STORAGE_H
#define NVS_STORAGE_H

#include "config.h"
#include "esp_err.h"

/**
 * @brief Initialize NVS storage
 * @return ESP_OK on success
 */
esp_err_t nvs_storage_init(void);

/**
 * @brief Save calibration data to NVS
 * @param data Pointer to calibration data to save
 * @return ESP_OK on success
 */
esp_err_t nvs_save_calibration(const calibration_data_t *data);

/**
 * @brief Load calibration data from NVS
 * @param data Pointer to calibration data structure to fill
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no calibration saved
 */
esp_err_t nvs_load_calibration(calibration_data_t *data);

/**
 * @brief Clear calibration data from NVS
 * @return ESP_OK on success
 */
esp_err_t nvs_clear_calibration(void);

/**
 * @brief Check if valid calibration exists in NVS
 * @return true if valid calibration exists
 */
bool nvs_has_calibration(void);

/**
 * @brief Get default calibration values
 * @param data Pointer to calibration data structure to fill with defaults
 */
void nvs_get_default_calibration(calibration_data_t *data);

/**
 * @brief Save WiFi STA configuration to NVS
 * @param config Pointer to WiFi STA config to save
 * @return ESP_OK on success
 */
esp_err_t nvs_save_wifi_config(const crawler_wifi_config_t *config);

/**
 * @brief Load WiFi STA configuration from NVS
 * @param config Pointer to WiFi STA config structure to fill
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no config saved
 */
esp_err_t nvs_load_wifi_config(crawler_wifi_config_t *config);

/**
 * @brief Get default WiFi STA configuration (disabled)
 * @param config Pointer to WiFi STA config structure to fill with defaults
 */
void nvs_get_default_wifi_config(crawler_wifi_config_t *config);

#endif // NVS_STORAGE_H
