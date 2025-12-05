/**
 * @file udp_log.c
 * @brief UDP broadcast logging for wireless debugging
 */

#include "udp_log.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string.h>
#include <stdarg.h>

static const char *TAG = "UDP_LOG";

#define UDP_LOG_PORT 5555
#define UDP_LOG_BROADCAST "255.255.255.255"

static int udp_socket = -1;
static struct sockaddr_in broadcast_addr;

// Custom vprintf function for ESP_LOG redirection
static int udp_log_vprintf(const char *fmt, va_list args)
{
    char buf[256];
    int len = vsnprintf(buf, sizeof(buf), fmt, args);

    // Always print to serial too
    vprintf(fmt, args);

    // Send over UDP if socket is ready
    if (udp_socket >= 0 && len > 0) {
        sendto(udp_socket, buf, len, 0,
               (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
    }

    return len;
}

esp_err_t udp_log_init(void)
{
    // Create UDP socket
    udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_socket < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        return ESP_FAIL;
    }

    // Enable broadcast
    int broadcast_enable = 1;
    if (setsockopt(udp_socket, SOL_SOCKET, SO_BROADCAST,
                   &broadcast_enable, sizeof(broadcast_enable)) < 0) {
        ESP_LOGE(TAG, "Failed to enable broadcast");
        close(udp_socket);
        udp_socket = -1;
        return ESP_FAIL;
    }

    // Setup broadcast address
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(UDP_LOG_PORT);
    broadcast_addr.sin_addr.s_addr = inet_addr(UDP_LOG_BROADCAST);

    // Redirect ESP_LOG output to our custom function
    esp_log_set_vprintf(udp_log_vprintf);

    ESP_LOGI(TAG, "UDP logging started on port %d", UDP_LOG_PORT);

    return ESP_OK;
}
