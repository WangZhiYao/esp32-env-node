#pragma once

#include "esp_err.h"
#include "app_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize ESP-NOW and WiFi
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t app_espnow_init(void);

/**
 * @brief Send a Register Request to the Gateway
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t app_espnow_send_register_req(void);

/**
 * @brief Send a Heartbeat to the Gateway
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t app_espnow_send_heartbeat(void);

/**
 * @brief Send Data Report to the Gateway
 * 
 * @param data Pointer to the data buffer
 * @param len Length of the data
 * @return esp_err_t ESP_OK on success
 */
esp_err_t app_espnow_send_data(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
