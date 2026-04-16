/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_log.h"
#include "mqtt_client.h"
#if CONFIG_MQTT_PROTOCOL_5
#include "mqtt5_client.h"
#endif
#include "mqtt_conformance.h"

static const char *TAG = "mqtt_conformance";

#define CLIENT_ID_SIZE 20

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        break;

    case MQTT_EVENT_DISCONNECTED:
#if CONFIG_MQTT_PROTOCOL_5
        if (event->error_handle) {
            ESP_LOGW(TAG, "DISCONNECT_REASON=%d", event->error_handle->disconnect_return_code);
        }

#endif
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);

        if (event->data_len > 0 && event->data) {
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED data_len=%d return_code=0x%02x",
                     event->data_len, (unsigned int)(uint8_t)event->data[0]);
        }

        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);

        if (event->data_len > 0 && event->data) {
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED data_len=%d reason_code=0x%02x",
                     event->data_len, (unsigned int)(uint8_t)event->data[0]);
        }

        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA topic=%.*s qos=%d len=%d offset=%d total=%d", event->topic_len, event->topic,
                 event->qos, event->data_len, event->current_data_offset, event->total_data_len);
        ESP_LOGI(TAG, "MQTT_EVENT_DATA_PAYLOAD %.*s", event->data_len, event->data ? event->data : "");

        if (event->current_data_offset + event->data_len == event->total_data_len) {
            ESP_LOGI(TAG, "MQTT_EVENT_DATA_COMPLETE msg_id=%d total=%d", event->msg_id, event->total_data_len);
        }

        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT_EVENT_ERROR");

        if (event->error_handle) {
            ESP_LOGE(TAG, "error_type=%" PRId32 " connect_return_code=%" PRId32,
                     (int32_t)event->error_handle->error_type,
                     (int32_t)event->error_handle->connect_return_code);
        }

        if (event->data_len > 0 && event->data) {
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR data_len=%d data=%.*s",
                     event->data_len, event->data_len, event->data);
        }

        break;

    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

void conformance_register_event_handlers(command_context_t *ctx)
{
    esp_mqtt_client_register_event(ctx->mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
}

void conformance_unregister_event_handlers(command_context_t *ctx)
{
    esp_mqtt_client_unregister_event(ctx->mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler);
}

void conformance_set_broker_uri(command_context_t *ctx, const char *uri)
{
    esp_mqtt_client_config_t config = {0};
    config.broker.address.uri = uri;
    esp_mqtt_set_config(ctx->mqtt_client, &config);
}

void conformance_configure_client(command_context_t *ctx)
{
    static char client_id[CLIENT_ID_SIZE];
    snprintf(client_id, sizeof(client_id), "esp-%08" PRIx32, esp_random());
    esp_mqtt_client_config_t config = {0};
    config.credentials.client_id = client_id;
    ESP_LOGI(TAG, "Client configured, client_id=%s (broker URI set via set_uri command)", client_id);
    esp_mqtt_set_config(ctx->mqtt_client, &config);
}
