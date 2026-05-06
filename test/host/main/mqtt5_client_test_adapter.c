/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>

#include "mqtt_client_priv.h"

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
