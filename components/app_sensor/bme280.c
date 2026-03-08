#include "bme280.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define TAG "bme280"

#define REG_ID          0xD0
#define REG_RESET       0xE0
#define REG_CTRL_HUM    0xF2
#define REG_STATUS      0xF3
#define REG_CTRL_MEAS   0xF4
#define REG_CONFIG      0xF5
#define REG_PRESS_MSB   0xF7
#define REG_CALIB_00    0x88
#define REG_CALIB_26    0xE1

static esp_err_t bme280_write_reg(i2c_master_dev_handle_t handle, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(handle, buf, 2, 100);
}

static esp_err_t bme280_read_reg(i2c_master_dev_handle_t handle, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(handle, &reg, 1, data, len, 100);
}

esp_err_t bme280_init(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t *out_handle)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = BME280_I2C_ADDR,
        .scl_speed_hz    = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, out_handle), TAG, "add device failed");

    uint8_t chip_id = 0;
    ESP_RETURN_ON_ERROR(bme280_read_reg(*out_handle, REG_ID, &chip_id, 1), TAG, "read id failed");
    if (chip_id != 0x60) {
        ESP_LOGE(TAG, "unexpected chip id: 0x%02x", chip_id);
        return ESP_ERR_NOT_FOUND;
    }

    // soft reset
    ESP_RETURN_ON_ERROR(bme280_write_reg(*out_handle, REG_RESET, 0xB6), TAG, "reset failed");
    vTaskDelay(pdMS_TO_TICKS(10));

    // Must configure in sleep mode (after reset, chip is in sleep mode)
    // t_sb = 1000ms, filter = 4
    ESP_RETURN_ON_ERROR(bme280_write_reg(*out_handle, REG_CONFIG, 0xA0), TAG, "config failed");
    // osrs_h = x1 (takes effect after ctrl_meas write)
    ESP_RETURN_ON_ERROR(bme280_write_reg(*out_handle, REG_CTRL_HUM, 0x01), TAG, "ctrl_hum failed");
    // osrs_t = x2, osrs_p = x16, mode = normal (write last to activate)
    ESP_RETURN_ON_ERROR(bme280_write_reg(*out_handle, REG_CTRL_MEAS, 0x57), TAG, "ctrl_meas failed");

    return ESP_OK;
}

esp_err_t bme280_read_calib(i2c_master_dev_handle_t handle, bme280_calib_t *calib)
{
    uint8_t buf[26];
    ESP_RETURN_ON_ERROR(bme280_read_reg(handle, REG_CALIB_00, buf, 26), TAG, "read calib00 failed");

    calib->dig_T1 = (uint16_t)(buf[1] << 8 | buf[0]);
    calib->dig_T2 = (int16_t)(buf[3] << 8 | buf[2]);
    calib->dig_T3 = (int16_t)(buf[5] << 8 | buf[4]);
    calib->dig_P1 = (uint16_t)(buf[7] << 8 | buf[6]);
    calib->dig_P2 = (int16_t)(buf[9] << 8 | buf[8]);
    calib->dig_P3 = (int16_t)(buf[11] << 8 | buf[10]);
    calib->dig_P4 = (int16_t)(buf[13] << 8 | buf[12]);
    calib->dig_P5 = (int16_t)(buf[15] << 8 | buf[14]);
    calib->dig_P6 = (int16_t)(buf[17] << 8 | buf[16]);
    calib->dig_P7 = (int16_t)(buf[19] << 8 | buf[18]);
    calib->dig_P8 = (int16_t)(buf[21] << 8 | buf[20]);
    calib->dig_P9 = (int16_t)(buf[23] << 8 | buf[22]);
    calib->dig_H1 = buf[25];

    uint8_t hbuf[7];
    ESP_RETURN_ON_ERROR(bme280_read_reg(handle, REG_CALIB_26, hbuf, 7), TAG, "read calib26 failed");

    calib->dig_H2 = (int16_t)(hbuf[1] << 8 | hbuf[0]);
    calib->dig_H3 = hbuf[2];
    calib->dig_H4 = (int16_t)((int8_t)hbuf[3] << 4) | (hbuf[4] & 0x0F);
    calib->dig_H5 = (int16_t)((int8_t)hbuf[5] << 4) | (hbuf[4] >> 4);
    calib->dig_H6 = (int8_t)hbuf[6];

    return ESP_OK;
}

esp_err_t bme280_read(i2c_master_dev_handle_t handle, bme280_calib_t *calib, bme280_data_t *out)
{
    uint8_t raw[8];
    ESP_RETURN_ON_ERROR(bme280_read_reg(handle, REG_PRESS_MSB, raw, 8), TAG, "read data failed");

    int32_t adc_P = (int32_t)((raw[0] << 12) | (raw[1] << 4) | (raw[2] >> 4));
    int32_t adc_T = (int32_t)((raw[3] << 12) | (raw[4] << 4) | (raw[5] >> 4));
    int32_t adc_H = (int32_t)((raw[6] << 8)  |  raw[7]);

    // Temperature compensation
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)calib->dig_T1 << 1))) * calib->dig_T2) >> 11;
    int32_t var2 = (((((adc_T >> 4) - (int32_t)calib->dig_T1) *
                      ((adc_T >> 4) - (int32_t)calib->dig_T1)) >> 12) * calib->dig_T3) >> 14;
    calib->t_fine = var1 + var2;
    out->temperature = (float)((calib->t_fine * 5 + 128) >> 8) / 100.0f;

    // Pressure compensation
    int64_t p1 = (int64_t)calib->t_fine - 128000;
    int64_t p2 = p1 * p1 * (int64_t)calib->dig_P6;
    p2 += (p1 * (int64_t)calib->dig_P5) << 17;
    p2 += ((int64_t)calib->dig_P4) << 35;
    p1  = ((p1 * p1 * (int64_t)calib->dig_P3) >> 8) + ((p1 * (int64_t)calib->dig_P2) << 12);
    p1  = (((int64_t)1 << 47) + p1) * (int64_t)calib->dig_P1 >> 33;
    if (p1 == 0) {
        out->pressure = 0;
    } else {
        int64_t p = 1048576 - adc_P;
        p = (((p << 31) - p2) * 3125) / p1;
        p1 = ((int64_t)calib->dig_P9 * (p >> 13) * (p >> 13)) >> 25;
        p2 = ((int64_t)calib->dig_P8 * p) >> 19;
        p  = ((p + p1 + p2) >> 8) + ((int64_t)calib->dig_P7 << 4);
        out->pressure = (float)p / 25600.0f;
    }

    // Humidity compensation (Bosch datasheet §4.2.3)
    int32_t v_x1 = calib->t_fine - 76800;
    int32_t h_left = (((adc_H << 14) - ((int32_t)calib->dig_H4 << 20)
                       - ((int32_t)calib->dig_H5 * v_x1)) + 16384) >> 15;
    int32_t h_right = (((((v_x1 * (int32_t)calib->dig_H6) >> 10)
                        * (((v_x1 * (int32_t)calib->dig_H3) >> 11) + 32768)) >> 10)
                       + 2097152) * (int32_t)calib->dig_H2 + 8192;
    h_right >>= 14;
    int32_t h = h_left * h_right;
    h = h - (((((h >> 15) * (h >> 15)) >> 7) * (int32_t)calib->dig_H1) >> 4);
    h = h < 0 ? 0 : h;
    h = h > 419430400 ? 419430400 : h;
    out->humidity = (float)(h >> 12) / 1024.0f;

    return ESP_OK;
}
