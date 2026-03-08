#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#define BME280_I2C_ADDR     0x76

typedef struct {
    // Trim parameters
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2;
    int16_t  dig_P3;
    int16_t  dig_P4;
    int16_t  dig_P5;
    int16_t  dig_P6;
    int16_t  dig_P7;
    int16_t  dig_P8;
    int16_t  dig_P9;
    uint8_t  dig_H1;
    int16_t  dig_H2;
    uint8_t  dig_H3;
    int16_t  dig_H4;
    int16_t  dig_H5;
    int8_t   dig_H6;
    int32_t  t_fine;
} bme280_calib_t;

typedef struct {
    float temperature; // °C
    float pressure;    // hPa
    float humidity;    // %RH
} bme280_data_t;

esp_err_t bme280_init(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t *out_handle);
esp_err_t bme280_read(i2c_master_dev_handle_t handle, bme280_calib_t *calib, bme280_data_t *out);
esp_err_t bme280_read_calib(i2c_master_dev_handle_t handle, bme280_calib_t *calib);
