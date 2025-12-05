#ifndef UDP_LOG_H
#define UDP_LOG_H

#include "esp_err.h"

/**
 * @brief Initialize UDP logging
 * Broadcasts ESP_LOG messages over UDP port 5555
 * Use: nc -u -l 5555 (Linux/Mac) or similar UDP listener
 */
esp_err_t udp_log_init(void);

#endif // UDP_LOG_H
