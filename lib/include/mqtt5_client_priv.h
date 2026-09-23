/*
 * SPDX-FileCopyrightText: 2022-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _MQTT5_CLIENT_PRIV_H_
#define _MQTT5_CLIENT_PRIV_H_

#include "mqtt5_client.h"
#include "mqtt5_msg.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mqtt5_topic_alias {
    char *topic;
    uint16_t topic_len;
    uint16_t topic_alias;
    STAILQ_ENTRY(mqtt5_topic_alias) next;
} mqtt5_topic_alias_t;
STAILQ_HEAD(mqtt5_topic_alias_list_t, mqtt5_topic_alias);
typedef struct mqtt5_topic_alias_list_t *mqtt5_topic_alias_handle_t;
typedef struct mqtt5_topic_alias *mqtt5_topic_alias_item_t;

typedef struct {
    const void *property;
    TaskHandle_t owner;
} mqtt5_staged_property_t;

typedef struct {
    esp_mqtt5_connection_property_storage_t connect_property_info;
    esp_mqtt5_connection_will_property_storage_t will_property_info;
    esp_mqtt5_connection_server_resp_property_t server_resp_property_info;
    esp_mqtt5_disconnect_property_config_t disconnect_property_info;
    mqtt5_staged_property_t publish_property;
    mqtt5_staged_property_t subscribe_property;
    mqtt5_staged_property_t unsubscribe_property;
    mqtt5_topic_alias_handle_t peer_topic_alias;
} mqtt5_config_storage_t;

void esp_mqtt5_increment_packet_counter(esp_mqtt5_client_handle_t client);
void esp_mqtt5_decrement_packet_counter(esp_mqtt5_client_handle_t client);
void esp_mqtt5_parse_pubcomp(esp_mqtt5_client_handle_t client);
void esp_mqtt5_parse_puback(esp_mqtt5_client_handle_t client);
void esp_mqtt5_parse_unsuback(esp_mqtt5_client_handle_t client);
void esp_mqtt5_parse_suback(esp_mqtt5_client_handle_t client);
void esp_mqtt5_parse_disconnect(esp_mqtt5_client_handle_t client, int *disconnect_rsp_code);
esp_err_t esp_mqtt5_parse_connack(esp_mqtt5_client_handle_t client, int *connect_rsp_code);
void esp_mqtt5_client_destory(esp_mqtt5_client_handle_t client);
esp_err_t esp_mqtt5_client_check_inflight_maximum(esp_mqtt5_client_handle_t client);
esp_err_t esp_mqtt5_client_publish_check(esp_mqtt5_client_handle_t client, int qos, int retain);
esp_err_t esp_mqtt5_client_subscribe_check(esp_mqtt5_client_handle_t client, int qos);
esp_err_t esp_mqtt5_staged_property_set(mqtt5_staged_property_t *slot, const void *property);
const void *esp_mqtt5_staged_property_get(const mqtt5_staged_property_t *slot);
void esp_mqtt5_staged_property_clear(mqtt5_staged_property_t *slot);
esp_err_t esp_mqtt5_create_default_config(esp_mqtt5_client_handle_t client);
esp_err_t esp_mqtt5_get_publish_data(esp_mqtt5_client_handle_t client, uint8_t *msg_buf, size_t msg_read_len,
                                     char **msg_topic, size_t *msg_topic_len, char **msg_data, size_t *msg_data_len);
#ifdef __cplusplus
}
#endif //__cplusplus

#endif
