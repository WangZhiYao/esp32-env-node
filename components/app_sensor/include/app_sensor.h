#pragma once

#include "esp_err.h"

typedef struct {
    float temperature; // °C
    float pressure;    // hPa
    float humidity;    // %RH
    float lux;
} sensor_data_t;

esp_err_t app_sensor_init(void);
esp_err_t app_sensor_read(sensor_data_t *out);
esp_err_t app_sensor_deinit(void);
