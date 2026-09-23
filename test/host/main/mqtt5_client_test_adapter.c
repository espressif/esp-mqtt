/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>
#include <stdlib.h>

#include "mqtt_client_priv.h"
#include "mqtt5_msg.h"

static bool fail_next_calloc;

void *mqtt_test_calloc(size_t count, size_t size)
{
    if (fail_next_calloc) {
        fail_next_calloc = false;
        return NULL;
    }

    return calloc(count, size);
}

size_t test_mqtt5_shared_subscription_alloc_failure(bool unsubscribe)
{
    uint8_t buffer[256] = {0};
    mqtt_connection_t connection = {.buffer = buffer, .buffer_length = sizeof(buffer)};
    uint16_t message_id = 0;
    esp_mqtt5_subscribe_property_config_t subscribe_property = {
        .is_share_subscribe = true, .share_name = "group"
    };
    esp_mqtt5_unsubscribe_property_config_t unsubscribe_property = {
        .is_share_subscribe = true, .share_name = "group"
    };
    esp_mqtt_topic_t topic = {.filter = "sensors/+", .qos = 1};
    fail_next_calloc = true;
    mqtt_message_t *message = unsubscribe ?
                              mqtt5_msg_unsubscribe(&connection, topic.filter, &message_id, &unsubscribe_property) :
                              mqtt5_msg_subscribe(&connection, &topic, 1, &message_id, &subscribe_property);
    return message->length;
}

esp_err_t test_mqtt5_check_inflight_maximum(uint16_t send_count, uint16_t receive_maximum)
{
    struct esp_mqtt_client client = {0};
    mqtt5_config_storage_t mqtt5_config = {0};
    client.mqtt5_config = &mqtt5_config;
    client.mqtt5_config->server_resp_property_info.receive_maximum = receive_maximum;
    client.send_publish_packet_count = send_count;
    return esp_mqtt5_client_check_inflight_maximum(&client);
}

int test_mqtt5_increment_packet_counter_with_dup(void)
{
    struct esp_mqtt_client client = {0};
    uint8_t publish_header[] = {0x3a}; // PUBLISH, DUP=1, QoS=1
    client.mqtt_state.connection.outbound_message.data = publish_header;
    esp_mqtt5_increment_packet_counter(&client);
    return client.send_publish_packet_count;
}

esp_err_t test_mqtt5_set_connect_null_property(void)
{
    struct esp_mqtt_client client = {0};
    return esp_mqtt5_client_set_connect_property(&client, NULL);
}

esp_err_t test_mqtt5_set_publish_null_property(void)
{
    struct esp_mqtt_client client = {0};
    return esp_mqtt5_client_set_publish_property(&client, NULL);
}

esp_err_t test_mqtt5_set_subscribe_null_property(void)
{
    struct esp_mqtt_client client = {0};
    return esp_mqtt5_client_set_subscribe_property(&client, NULL);
}

esp_err_t test_mqtt5_set_unsubscribe_null_property(void)
{
    struct esp_mqtt_client client = {0};
    return esp_mqtt5_client_set_unsubscribe_property(&client, NULL);
}

esp_err_t test_mqtt5_set_disconnect_null_property(void)
{
    struct esp_mqtt_client client = {0};
    return esp_mqtt5_client_set_disconnect_property(&client, NULL);
}
