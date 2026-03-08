#include "bh1750.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "bh1750"

// One-time H-resolution mode: 1 lx resolution, 120ms measurement
#define CMD_POWER_ON        0x01
#define CMD_RESET           0x07
#define CMD_CONT_H_RES      0x10

esp_err_t bh1750_init(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t *out_handle)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = BH1750_I2C_ADDR,
        .scl_speed_hz    = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, out_handle), TAG, "add device failed");

    uint8_t cmd = CMD_POWER_ON;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(*out_handle, &cmd, 1, 100), TAG, "power on failed");

    cmd = CMD_RESET;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(*out_handle, &cmd, 1, 100), TAG, "reset failed");

    cmd = CMD_CONT_H_RES;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(*out_handle, &cmd, 1, 100), TAG, "set mode failed");

    vTaskDelay(pdMS_TO_TICKS(180));

    return ESP_OK;
}

esp_err_t bh1750_read(i2c_master_dev_handle_t handle, bh1750_data_t *out)
{
    uint8_t buf[2];
    ESP_RETURN_ON_ERROR(i2c_master_receive(handle, buf, 2, 100), TAG, "read failed");

    uint16_t raw = (buf[0] << 8) | buf[1];
    out->lux = raw / 1.2f;

    return ESP_OK;
}
