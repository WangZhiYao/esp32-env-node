#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_mac.h"

#include "app_espnow.h"
#include "app_storage.h"
#include "app_protocol.h"

#define TAG "app_espnow"

#define ESPNOW_WIFI_MODE WIFI_MODE_STA
#define ESPNOW_WIFI_IF ESP_IF_WIFI_STA

/* Default Broadcast MAC Address */
static const uint8_t s_broadcast_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/* Node State */
static uint8_t s_node_id = 0;
static uint16_t s_seq_num = 0;

/* Storage Keys */
#define NVS_NAMESPACE "node_cfg"
#define NVS_KEY_NODE_ID "node_id"

static void app_espnow_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    /* Handle send status if needed */
    if (status != ESP_NOW_SEND_SUCCESS)
    {
        // ESP_LOGW(TAG, "Send to " MACSTR " failed", MAC2STR(mac_addr));
    }
}

static void app_espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    if (len < sizeof(app_protocol_header_t))
    {
        ESP_LOGW(TAG, "Received invalid packet length: %d", len);
        return;
    }

    app_protocol_header_t *header = (app_protocol_header_t *)data;

    switch (header->type)
    {
    case APP_PROTOCOL_MSG_REGISTER_RESP:
    {
        if (len < sizeof(app_protocol_register_resp_t))
            break;
        app_protocol_register_resp_t *resp = (app_protocol_register_resp_t *)data;

        ESP_LOGI(TAG, "Received REGISTER_RESP. Assigned ID: %d", resp->assigned_id);
        if (resp->assigned_id != 0 && s_node_id != resp->assigned_id)
        {
            s_node_id = resp->assigned_id;
            app_storage_set_u8(NVS_NAMESPACE, NVS_KEY_NODE_ID, s_node_id);
            ESP_LOGI(TAG, "Node ID saved to NVS");
        }
        break;
    }
    case APP_PROTOCOL_MSG_HEARTBEAT_ACK:
    {
        ESP_LOGD(TAG, "Received HEARTBEAT_ACK from " MACSTR, MAC2STR(recv_info->src_addr));
        break;
    }
    default:
        // ESP_LOGD(TAG, "Received message type: 0x%02X", header->type);
        break;
    }
}

static esp_err_t app_espnow_send(const void *data, size_t len)
{
    /* Use Broadcast for now as we don't track Gateway MAC specifically yet */
    return esp_now_send(s_broadcast_mac, (const uint8_t *)data, len);
}

esp_err_t app_espnow_init(void)
{
    esp_err_t err;

    /* 1. Initialize WiFi */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(ESPNOW_WIFI_MODE));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));

    /* 2. Initialize ESP-NOW */
    err = esp_now_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_now_init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* 3. Register Callbacks */
    err = esp_now_register_recv_cb(app_espnow_recv_cb);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Register recv cb failed");
        return err;
    }

    err = esp_now_register_send_cb(app_espnow_send_cb);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Register send cb failed");
        return err;
    }

    /* 4. Add Broadcast Peer */
    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, s_broadcast_mac, ESP_NOW_ETH_ALEN);
    peer_info.channel = 1;
    peer_info.ifidx = ESPNOW_WIFI_IF;
    peer_info.encrypt = false;

    if (!esp_now_is_peer_exist(s_broadcast_mac))
    {
        err = esp_now_add_peer(&peer_info);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to add broadcast peer: %s", esp_err_to_name(err));
            return err;
        }
    }

    /* 5. Load Node ID from NVS */
    err = app_storage_get_u8(NVS_NAMESPACE, NVS_KEY_NODE_ID, &s_node_id);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Loaded Node ID: %d", s_node_id);
    }
    else
    {
        ESP_LOGW(TAG, "Node ID not found, starting as unregistered (ID: 0)");
        s_node_id = 0;
    }

    return ESP_OK;
}

esp_err_t app_espnow_send_register_req(void)
{
    app_protocol_register_req_t req = {0};
    req.header.type = APP_PROTOCOL_MSG_REGISTER_REQ;
    req.header.node_id = s_node_id; /* Should be 0 if not registered */
    req.header.seq = s_seq_num++;
    req.device_type = 0x01; /* Example Device Type */
    req.fw_version = 0x01;  /* Example FW Version */

    ESP_LOGI(TAG, "Sending REGISTER_REQ...");
    return app_espnow_send(&req, sizeof(req));
}

esp_err_t app_espnow_send_heartbeat(void)
{
    if (s_node_id == 0)
    {
        ESP_LOGW(TAG, "Cannot send Heartbeat: Node not registered");
        return ESP_ERR_INVALID_STATE;
    }

    app_protocol_heartbeat_t heartbeat = {0};
    heartbeat.header.type = APP_PROTOCOL_MSG_HEARTBEAT;
    heartbeat.header.node_id = s_node_id;
    heartbeat.header.seq = s_seq_num++;

    ESP_LOGI(TAG, "Sending HEARTBEAT...");
    return app_espnow_send(&heartbeat, sizeof(heartbeat));
}

esp_err_t app_espnow_send_data(const uint8_t *data, size_t len)
{
    if (s_node_id == 0)
    {
        ESP_LOGW(TAG, "Cannot send Data: Node not registered");
        return ESP_ERR_INVALID_STATE;
    }

    if (len > APP_PROTOCOL_USER_DATA_MAX_LEN)
    {
        ESP_LOGE(TAG, "Data too long: %d (Max: %d)", len, APP_PROTOCOL_USER_DATA_MAX_LEN);
        return ESP_ERR_INVALID_SIZE;
    }

    app_protocol_data_report_t report = {0};
    report.header.type = APP_PROTOCOL_MSG_DATA_REPORT;
    report.header.node_id = s_node_id;
    report.header.seq = s_seq_num++;
    report.data_len = len;
    memcpy(report.data, data, len);

    ESP_LOGI(TAG, "Sending DATA_REPORT...");
    return app_espnow_send(&report, sizeof(app_protocol_header_t) + sizeof(uint16_t) + len);
}
