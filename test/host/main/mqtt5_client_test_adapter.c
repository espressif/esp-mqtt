/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>
#include <string.h>

#include "mqtt_client_priv.h"

static struct esp_mqtt_client s_client;
static mqtt5_config_storage_t s_mqtt5_config;

esp_mqtt_client_handle_t test_mqtt5_property_client_reset(void)
{
    memset(&s_client, 0, sizeof(s_client));
    memset(&s_mqtt5_config, 0, sizeof(s_mqtt5_config));
    s_client.mqtt5_config = &s_mqtt5_config;
    s_client.mqtt_state.connection.information.protocol_ver = MQTT_PROTOCOL_V_5;
    return &s_client;
}

mqtt5_staged_property_t *test_mqtt5_publish_property_slot(void)
{
    return &s_mqtt5_config.publish_property;
}

mqtt5_staged_property_t *test_mqtt5_subscribe_property_slot(void)
{
    return &s_mqtt5_config.subscribe_property;
}

mqtt5_staged_property_t *test_mqtt5_unsubscribe_property_slot(void)
{
    return &s_mqtt5_config.unsubscribe_property;
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
