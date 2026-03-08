#include "app_sensor.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "bh1750.h"
#include "bme280.h"

#define TAG "app_sensor"

// I2C bus configuration
#define I2C_PORT I2C_NUM_0
#define PIN_SDA GPIO_NUM_21
#define PIN_SCL GPIO_NUM_22

/* --- Static handles for I2C bus and devices --- */
// Handle for the shared I2C master bus.
static i2c_master_bus_handle_t s_bus;
// Handle for the BME280 sensor device on the bus.
static i2c_master_dev_handle_t s_bme280;
// Handle for the BH1750 sensor device on the bus.
static i2c_master_dev_handle_t s_bh1750;
// Storage for the BME280's factory calibration data.
static bme280_calib_t s_calib;

esp_err_t app_sensor_init(void)
{
    // Configure and initialize the I2C master bus.
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = PIN_SDA,
        .scl_io_num = PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "bus init failed");

    // Initialize the BME280 sensor and read its calibration data.
    // The calibration data is required to compensate the raw sensor readings.
    ESP_RETURN_ON_ERROR(bme280_init(s_bus, &s_bme280), TAG, "bme280 init failed");
    ESP_RETURN_ON_ERROR(bme280_read_calib(s_bme280, &s_calib), TAG, "bme280 calib failed");

    // Initialize the BH1750 light sensor.
    ESP_RETURN_ON_ERROR(bh1750_init(s_bus, &s_bh1750), TAG, "bh1750 init failed");

    ESP_LOGI(TAG, "sensors ready");
    return ESP_OK;
}

esp_err_t app_sensor_read(sensor_data_t *out)
{
    // Read raw data and apply compensation for the BME280 sensor.
    bme280_data_t bme = {0};
    ESP_RETURN_ON_ERROR(bme280_read(s_bme280, &s_calib, &bme), TAG, "bme280 read failed");

    // Read data from the BH1750 sensor.
    bh1750_data_t bh = {0};
    ESP_RETURN_ON_ERROR(bh1750_read(s_bh1750, &bh), TAG, "bh1750 read failed");

    // Aggregate data from all sensors into the output structure.
    out->temperature = bme.temperature;
    out->pressure = bme.pressure;
    out->humidity = bme.humidity;
    out->lux = bh.lux;

    return ESP_OK;
}

esp_err_t app_sensor_deinit(void)
{
    // De-initialize devices before tearing down the bus.
    ESP_RETURN_ON_ERROR(i2c_master_bus_rm_device(s_bh1750), TAG, "rm bh1750 failed");
    s_bh1750 = NULL;

    ESP_RETURN_ON_ERROR(i2c_master_bus_rm_device(s_bme280), TAG, "rm bme280 failed");
    s_bme280 = NULL;

    // Delete the I2C master bus.
    ESP_RETURN_ON_ERROR(i2c_del_master_bus(s_bus), TAG, "del bus failed");
    s_bus = NULL;

    ESP_LOGI(TAG, "sensors deinitialized");
    return ESP_OK;
}
