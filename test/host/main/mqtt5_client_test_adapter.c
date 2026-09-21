/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdbool.h>
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

/* --- property validation -------------------------------------------------
 *
 * The per-message esp_mqtt_client_*5() APIs do not go through
 * esp_mqtt5_client_set_*_property(), so the validators below are the only thing
 * standing between a caller-supplied property and mqtt5_msg_*(). Two of these
 * cases used to reach the encoder: a shared subscription with no share name
 * dereferences NULL in mqtt5_msg_subscribe()/mqtt5_msg_unsubscribe(), and a
 * topic alias above the server maximum is a protocol error the broker answers
 * with DISCONNECT.
 */

/* Broker that supports everything, so a rejection is attributable to the
   property under test rather than to a server limit. */
static void test_mqtt5_permissive_client(struct esp_mqtt_client *client, mqtt5_config_storage_t *config,
                                         esp_mqtt_protocol_ver_t protocol_ver)
{
    client->mqtt5_config = config;
    client->mqtt_state.connection.information.protocol_ver = protocol_ver;
    config->server_resp_property_info.shared_subscribe_available = true;
    config->server_resp_property_info.topic_alias_maximum = 10;
}

esp_err_t test_mqtt5_validate_subscribe_property(const esp_mqtt5_subscribe_property_config_t *property,
                                                 bool shared_available, esp_mqtt_protocol_ver_t protocol_ver)
{
    struct esp_mqtt_client client = {0};
    mqtt5_config_storage_t mqtt5_config = {0};
    test_mqtt5_permissive_client(&client, &mqtt5_config, protocol_ver);
    mqtt5_config.server_resp_property_info.shared_subscribe_available = shared_available;
    return esp_mqtt5_client_validate_subscribe_property(&client, property);
}

esp_err_t test_mqtt5_validate_unsubscribe_property(const esp_mqtt5_unsubscribe_property_config_t *property,
                                                   bool shared_available, esp_mqtt_protocol_ver_t protocol_ver)
{
    struct esp_mqtt_client client = {0};
    mqtt5_config_storage_t mqtt5_config = {0};
    test_mqtt5_permissive_client(&client, &mqtt5_config, protocol_ver);
    mqtt5_config.server_resp_property_info.shared_subscribe_available = shared_available;
    return esp_mqtt5_client_validate_unsubscribe_property(&client, property);
}

esp_err_t test_mqtt5_validate_publish_property(const esp_mqtt5_publish_property_config_t *property,
                                               uint16_t topic_alias_maximum, esp_mqtt_protocol_ver_t protocol_ver)
{
    struct esp_mqtt_client client = {0};
    mqtt5_config_storage_t mqtt5_config = {0};
    test_mqtt5_permissive_client(&client, &mqtt5_config, protocol_ver);
    mqtt5_config.server_resp_property_info.topic_alias_maximum = topic_alias_maximum;
    return esp_mqtt5_client_validate_publish_property(&client, property);
}

/* esp_mqtt5_client_set_publish_property() stages a one-shot property that the
   next publish consumes. Reading it back is how the test tells "replaced" from
   "consumed"; the field lives in the private mqtt5 config. */
const void *test_mqtt5_staged_publish_property(esp_mqtt_client_handle_t client)
{
    return client->mqtt5_config->publish_property_info;
}
