#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

// I2C address when ADDR pin is pulled to GND
#define BH1750_I2C_ADDR 0x23

/**
 * @brief Output data from the BH1750.
 */
typedef struct {
    float lux; // Ambient light level in lux
} bh1750_data_t;

/**
 * @brief Adds the BH1750 to the I2C bus and starts continuous H-resolution mode.
 *
 * Sequence: Power On → Reset → Continuous H-Resolution Mode (1 lx, 120ms).
 * Waits 180ms after issuing the measurement command before returning, so the
 * first call to bh1750_read() will have valid data immediately.
 *
 * @param bus        Initialized I2C master bus handle.
 * @param out_handle Output device handle for subsequent operations.
 */
esp_err_t bh1750_init(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t *out_handle);

/**
 * @brief Reads the latest light measurement from the BH1750.
 *
 * The sensor continuously updates its internal register in H-resolution mode.
 * Raw value is converted to lux by dividing by 1.2 (per datasheet).
 *
 * @param handle Initialized BH1750 device handle.
 * @param out    Output struct to populate with the lux reading.
 */
esp_err_t bh1750_read(i2c_master_dev_handle_t handle, bh1750_data_t *out);
