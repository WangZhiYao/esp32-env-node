#include <stdio.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "app_storage.h"
#include "app_event.h"
#include "app_network.h"
#include "app_espnow.h"
#include "app_sensor.h"

#define TAG "app_main"

#define SENSOR_INTERVAL_US  ((uint64_t)CONFIG_SENSOR_SAMPLE_INTERVAL_S * 1000 * 1000)

static atomic_bool s_registered = false;
static TaskHandle_t s_sensor_task;

static void sensor_timer_cb(void *arg)
{
    xTaskNotifyGive(s_sensor_task);
}

static void sensor_task(void *arg)
{
    while (1)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        sensor_data_t data;
        esp_err_t ret = app_sensor_read(&data);
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "sensor read failed: %s", esp_err_to_name(ret));
            continue;
        }

        ESP_LOGI(TAG, "T=%.2f°C P=%.2fhPa H=%.2f%% L=%.2flux",
                 data.temperature, data.pressure, data.humidity, data.lux);

        if (atomic_load(&s_registered))
        {
            app_event_sensor_data_t evt = {0};
            evt.sensor_type = 0x01;
            evt.data_len = (uint16_t)snprintf((char *)evt.data, sizeof(evt.data),
                "{\"t\":%.2f,\"p\":%.2f,\"h\":%.2f,\"l\":%.2f}",
                data.temperature, data.pressure, data.humidity, data.lux);
            app_event_post(APP_EVENT_SENSOR_DATA, &evt, sizeof(evt));
        }
    }
}

static void app_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    /* Only process APP_EVENT_BASE events, filter others */
    if (event_base != APP_EVENT_BASE)
    {
        return;
    }

    switch ((app_event_id_t)event_id)
    {

    case APP_EVENT_ESPNOW_REGISTERED:
        app_event_espnow_registered_t *evt = (app_event_espnow_registered_t *)event_data;
        atomic_store(&s_registered, true);
        ESP_LOGI(TAG, "Registered: node_id=%d gateway=" MACSTR, evt->node_id, MAC2STR(evt->gateway_mac));
        break;

    case APP_EVENT_ESPNOW_UNREGISTERED:
        atomic_store(&s_registered, false);
        ESP_LOGW(TAG, "Lost gateway, will retry registration");
        break;

    case APP_EVENT_SENSOR_DATA:
        app_event_sensor_data_t *sensor_evt = (app_event_sensor_data_t *)event_data;
        esp_err_t err = app_espnow_send_data(sensor_evt->data, sensor_evt->data_len);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "sensor_data send failed (type=%d): %s", sensor_evt->sensor_type, esp_err_to_name(err));
        }
        break;

    default:
        break;
    }
}

void app_main(void)
{
    esp_err_t err;

    err = app_storage_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "storage: %s", esp_err_to_name(err));
        return;
    }

    err = app_event_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "event: %s", esp_err_to_name(err));
        return;
    }

    err = app_network_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "network: %s", esp_err_to_name(err));
        return;
    }

    err = app_espnow_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "espnow: %s", esp_err_to_name(err));
        return;
    }

    err = app_sensor_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "sensor: %s", esp_err_to_name(err));
        return;
    }

    err = app_event_handler_register(ESP_EVENT_ANY_ID, app_event_handler, NULL);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register event handler: %s", esp_err_to_name(err));
        return;
    }

    /* If restored from NVS, app_espnow_init already set registered state.
     * Sync local flag before entering the loop. */
    atomic_store(&s_registered, app_espnow_is_registered());

    ESP_LOGI(TAG, "Node started");

    xTaskCreate(sensor_task, "sensor", 3072, NULL, 5, &s_sensor_task);
    xTaskNotifyGive(s_sensor_task);  // trigger first read immediately

    const esp_timer_create_args_t timer_args = {
        .callback = sensor_timer_cb,
        .name     = "sensor",
    };
    esp_timer_handle_t sensor_timer;
    err = esp_timer_create(&timer_args, &sensor_timer);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "timer create: %s", esp_err_to_name(err));
        return;
    }
    esp_timer_start_periodic(sensor_timer, SENSOR_INTERVAL_US);
}
