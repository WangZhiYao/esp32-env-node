#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "bh1750.h"

#define TAG "bh1750"

// Opcodes (BH1750 datasheet §5)
#define CMD_POWER_ON    0x01  // Power on, no-op measurement
#define CMD_RESET       0x07  // Reset data register (only valid in power-on state)
#define CMD_CONT_H_RES  0x10  // Continuously measure at 1 lx resolution (~120ms/sample)

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

    // Reset clears the internal data register to avoid reading stale data
    cmd = CMD_RESET;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(*out_handle, &cmd, 1, 100), TAG, "reset failed");

    // Start continuous H-resolution mode; first result ready after ~120ms
    cmd = CMD_CONT_H_RES;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(*out_handle, &cmd, 1, 100), TAG, "set mode failed");

    // Wait for the first measurement to complete (120ms typical, 180ms max per datasheet)
    vTaskDelay(pdMS_TO_TICKS(180));

    return ESP_OK;
}

esp_err_t bh1750_read(i2c_master_dev_handle_t handle, bh1750_data_t *out)
{
    // BH1750 outputs a 16-bit big-endian value; no register address needed in continuous mode
    uint8_t buf[2];
    ESP_RETURN_ON_ERROR(i2c_master_receive(handle, buf, 2, 100), TAG, "read failed");

    uint16_t raw = (buf[0] << 8) | buf[1];
    // Convert raw count to lux: sensitivity is 1.2 counts/lx (datasheet §2.4)
    out->lux = raw / 1.2f;

    return ESP_OK;
}
