#pragma once

#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

// I2C address when SDO pin is pulled to GND
#define BME280_I2C_ADDR 0x76

/**
 * @brief Factory calibration (trim) parameters read from the sensor's NVM.
 *
 * These values are unique to each chip and must be used to compensate the
 * raw ADC readings for temperature, pressure, and humidity.
 * t_fine is an intermediate temperature value shared by all three compensation
 * formulas and is updated on every call to bme280_read().
 */
typedef struct
{
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
    int32_t  t_fine; // Intermediate temperature used by pressure/humidity compensation
} bme280_calib_t;

/**
 * @brief Compensated output data from the BME280.
 */
typedef struct
{
    float temperature; // °C
    float pressure;    // hPa
    float humidity;    // %RH
} bme280_data_t;

/**
 * @brief Adds the BME280 to the I2C bus and configures it for normal mode.
 *
 * Performs a soft reset, then writes CONFIG → CTRL_HUM → CTRL_MEAS in that
 * order, as required by the datasheet (CONFIG must be written in sleep mode).
 *
 * @param bus        Initialized I2C master bus handle.
 * @param out_handle Output device handle for subsequent operations.
 */
esp_err_t bme280_init(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t *out_handle);

/**
 * @brief Reads and parses the factory calibration data from the sensor's NVM.
 *
 * Must be called once after bme280_init() and before bme280_read().
 * Calibration data is split across two register banks (0x88 and 0xE1).
 *
 * @param handle Initialized BME280 device handle.
 * @param calib  Output struct to populate with calibration values.
 */
esp_err_t bme280_read_calib(i2c_master_dev_handle_t handle, bme280_calib_t *calib);

/**
 * @brief Reads raw ADC data and applies Bosch compensation formulas.
 *
 * Updates calib->t_fine as a side effect, which is required for pressure
 * and humidity compensation.
 *
 * @param handle Initialized BME280 device handle.
 * @param calib  Calibration data previously read by bme280_read_calib().
 * @param out    Output struct to populate with compensated readings.
 */
esp_err_t bme280_read(i2c_master_dev_handle_t handle, bme280_calib_t *calib, bme280_data_t *out);
