#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#define BH1750_I2C_ADDR     0x23

typedef struct {
    float lux;
} bh1750_data_t;

esp_err_t bh1750_init(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t *out_handle);
esp_err_t bh1750_read(i2c_master_dev_handle_t handle, bh1750_data_t *out);
