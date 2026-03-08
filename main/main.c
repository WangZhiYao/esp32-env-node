#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "app_espnow.h"
#include "app_event.h"
#include "app_network.h"
#include "app_sensor.h"
#include "app_storage.h"
#include "app_protocol.h"

#define TAG "app_main"

/* Task settings */
#define SENSOR_TASK_STACK_SIZE 3072
#define SENSOR_TASK_PRIORITY 5

/* Sensor type identifiers */
#define SENSOR_TYPE_ENVIRONMENTAL APP_PROTOCOL_SENSOR_ENV

/* Sensor sampling period, converted from seconds (from Kconfig) to microseconds */
#define SENSOR_INTERVAL_US ((uint64_t)CONFIG_SENSOR_SAMPLE_INTERVAL_S * 1000 * 1000)

/* Atomically tracks the node's registration status with the gateway. */
static atomic_bool s_registered = false;
/* Handle for the sensor reading task, used for task notifications. */
static TaskHandle_t s_sensor_task;

/**
 * @brief Callback for the periodic sensor timer.
 *
 * This function is executed by the high-resolution timer driver. It notifies
 * the sensor_task to wake up and perform a sensor reading.
 * @param arg Not used.
 */
static void sensor_timer_cb(void *arg)
{
    // Notify the sensor task to start a new reading cycle.
    // Use xTaskNotifyGive for a lightweight, direct-to-task notification.
    xTaskNotifyGive(s_sensor_task);
}

/**
 * @brief Main task for handling sensor data acquisition.
 *
 * This task waits for a notification from the sensor_timer_cb, then reads
 * data from the sensors. If the node is registered with the gateway, it
 * formats the data as JSON and posts it as an event for sending.
 * @param arg Not used.
 */
static void sensor_task(void *arg)
{
    while (1)
    {
        // Wait indefinitely for a notification from the timer callback.
        // This keeps the task blocked and CPU-efficient until it's time to work.
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        sensor_data_t data;
        esp_err_t ret = app_sensor_read(&data);
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "sensor read failed: %s", esp_err_to_name(ret));
            continue; // Wait for the next notification
        }

        ESP_LOGI(TAG, "T=%.2f°C P=%.2fhPa H=%.2f%% L=%.2flux",
                 data.temperature, data.pressure, data.humidity, data.lux);

        // Only prepare and send data if the node is confirmed to be registered.
        if (atomic_load(&s_registered))
        {
            app_protocol_env_data_t payload = {
                .temperature = data.temperature,
                .pressure    = data.pressure,
                .humidity    = data.humidity,
                .lux         = data.lux,
            };
            app_event_sensor_data_t evt = {0};
            evt.sensor_type = APP_PROTOCOL_SENSOR_ENV;
            evt.data_len    = sizeof(payload);
            memcpy(evt.data, &payload, sizeof(payload));
            // Post the data to the event loop for handling by app_event_handler.
            app_event_post(APP_EVENT_SENSOR_DATA, &evt, sizeof(evt));
        }
    }
}

/**
 * @brief Global event handler for application-specific events.
 *
 * This function acts as a central hub for events from different components,
 * such as ESP-NOW and sensors, allowing for decoupled communication.
 */
static void app_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    /* Only process APP_EVENT_BASE events, filter others */
    if (event_base != APP_EVENT_BASE)
    {
        return;
    }

    // Route events based on their specific ID.
    switch ((app_event_id_t)event_id)
    {
    case APP_EVENT_ESPNOW_REGISTERED:
    {
        // The ESP-NOW component has successfully registered with the gateway.
        app_event_espnow_registered_t *evt = (app_event_espnow_registered_t *)event_data;
        atomic_store(&s_registered, true); // Update the global registration flag.
        ESP_LOGI(TAG, "Registered: node_id=%d gateway=" MACSTR, evt->node_id, MAC2STR(evt->gateway_mac));
        break;
    }
    case APP_EVENT_ESPNOW_UNREGISTERED:
    {
        // The connection to the gateway has been lost.
        atomic_store(&s_registered, false); // Clear the registration flag.
        ESP_LOGW(TAG, "Lost gateway, will retry registration");
        break;
    }
    case APP_EVENT_SENSOR_DATA:
    {
        // Received sensor data event, ready to be sent via ESP-NOW.
        app_event_sensor_data_t *sensor_evt = (app_event_sensor_data_t *)event_data;
        esp_err_t err = app_espnow_send_data(sensor_evt->sensor_type, sensor_evt->data, sensor_evt->data_len);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "sensor_data send failed (type=%d): %s", sensor_evt->sensor_type, esp_err_to_name(err));
        }
        break;
    }
    default:
        break;
    }
}

/**
 * @brief Main entry point of the application.
 */
void app_main(void)
{
    esp_err_t err;

    // Initialize all application components in order.
    // This is critical as some components depend on others (e.g., ESP-NOW needs networking).
    err = app_storage_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "storage: %s", esp_err_to_name(err));
        return; // Halt on critical init failure.
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

    // Register the global handler to listen for all application events.
    err = app_event_handler_register(ESP_EVENT_ANY_ID, app_event_handler, NULL);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register event handler: %s", esp_err_to_name(err));
        return;
    }

    /* If the device was already registered (e.g., restored from NVS),
     * app_espnow_init() will know. We sync the main 's_registered' flag here
     * to reflect that state immediately. */
    atomic_store(&s_registered, app_espnow_is_registered());

    ESP_LOGI(TAG, "Node started");

    // Create the sensor task.
    xTaskCreate(sensor_task, "sensor", SENSOR_TASK_STACK_SIZE, NULL, SENSOR_TASK_PRIORITY, &s_sensor_task);
    // Trigger an initial sensor reading immediately on startup.
    xTaskNotifyGive(s_sensor_task);

    // Set up a periodic timer to trigger sensor readings.
    const esp_timer_create_args_t timer_args = {
        .callback = sensor_timer_cb,
        .name = "sensor",
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
