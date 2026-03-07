#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "app_storage.h"
#include "app_event.h"
#include "app_network.h"
#include "app_espnow.h"

#define TAG "app_main"

static bool s_registered = false;

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
        s_registered = true;
        ESP_LOGI(TAG, "Registered: node_id=%d gateway=" MACSTR, evt->node_id, MAC2STR(evt->gateway_mac));
        break;

    case APP_EVENT_ESPNOW_UNREGISTERED:
        s_registered = false;
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

    err = app_event_handler_register(ESP_EVENT_ANY_ID, app_event_handler, NULL);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register event handler: %s", esp_err_to_name(err));
        return;
    }

    /* If restored from NVS, app_espnow_init already set registered state.
     * Sync local flag before entering the loop. */
    s_registered = app_espnow_is_registered();

    ESP_LOGI(TAG, "Node started");

    int sample_count = 0;

    while (1)
    {
        if (s_registered)
        {
            /* TODO: replace with real sensor task posting APP_EVENT_SENSOR_DATA */
            app_event_sensor_data_t evt = {0};
            evt.sensor_type = 0x01;
            evt.data_len = (uint8_t)snprintf((char *)evt.data, sizeof(evt.data),
                                             "SensorData_%d", sample_count++);
            app_event_post(APP_EVENT_SENSOR_DATA, &evt, sizeof(evt));
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
