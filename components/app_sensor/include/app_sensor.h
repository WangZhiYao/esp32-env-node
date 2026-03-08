#pragma once

#include "esp_err.h"

/**
 * @brief A unified structure to hold data from all sensors.
 */
typedef struct
{
    float temperature; // Temperature in degrees Celsius (°C)
    float pressure;    // Pressure in hectopascals (hPa)
    float humidity;    // Relative Humidity in percent (%RH)
    float lux;         // Ambient light in lux
} sensor_data_t;

/**
 * @brief Initializes the I2C bus and all connected sensors.
 */
esp_err_t app_sensor_init(void);

/**
 * @brief Reads the latest data from all initialized sensors.
 *
 * @param[out] out Pointer to a sensor_data_t struct to fill with sensor readings.
 * @return ESP_OK on success.
 */
esp_err_t app_sensor_read(sensor_data_t *out);

/**
 * @brief De-initializes sensors and the I2C bus.
 */
esp_err_t app_sensor_deinit(void);
