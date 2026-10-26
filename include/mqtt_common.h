/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MQTT_OVER_TCP_SCHEME "mqtt"
#define MQTT_OVER_SSL_SCHEME "mqtts"
#define MQTT_OVER_WS_SCHEME  "ws"
#define MQTT_OVER_WSS_SCHEME "wss"

/**
 *  *MQTT* protocol version used for connection
 */
typedef enum esp_mqtt_protocol_ver_t {
    MQTT_PROTOCOL_UNDEFINED = 0,
    MQTT_PROTOCOL_V_3_1,
    MQTT_PROTOCOL_V_3_1_1,
    MQTT_PROTOCOL_V_5,
} esp_mqtt_protocol_ver_t;

/**
 * Topic definition struct
 */
typedef struct topic_t {
    const char *filter;  /*!< Topic filter to subscribe */
    int qos; /*!< Max QoS level of the subscription */
} esp_mqtt_topic_t;

/**
 *  MQTT5 user property handle
 */
typedef struct mqtt5_user_property_list_t *mqtt5_user_property_handle_t;

/**
 *  MQTT5 protocol for user property
 */
typedef struct {
    const char *key;                       /*!< Item key name */
    const char *value;                     /*!< Item value string */
} esp_mqtt5_user_property_item_t;

/**
 *  MQTT5 protocol publish properties configuration, more details refer to MQTT5 protocol document section 3.3.2.3
 */
typedef struct {
    bool payload_format_indicator;               /*!< This value is to indicator publish message payload format */
    uint32_t message_expiry_interval;            /*!< The time interval that message expiry */
    uint16_t topic_alias;                        /*!< An integer value to identify the topic instead of using topic name string */
    const char *response_topic;                  /*!< Topic name for a response message */
    const char *correlation_data;                /*!< Binary data for receiver to match the response message */
    uint16_t correlation_data_len;               /*!< The length of correlation data */
    const char *content_type;                    /*!< This value is to indicator publish message content type, use a MIME content type string */
    mqtt5_user_property_handle_t user_property;  /*!< The handle for user property, call function esp_mqtt5_client_set_user_property to set it */
} esp_mqtt5_publish_property_config_t;

/**
 *  MQTT5 protocol subscribe properties configuration, more details refer to MQTT5 protocol document section 3.8.2.1
 */
typedef struct {
    uint32_t subscribe_id;                       /*!< A variable byte represents the identifier of the subscription, 1 to 268435455 (0 for none) */
    bool no_local_flag;                          /*!< Subscription Option to allow that server publish message that client sent */
    bool retain_as_published_flag;               /*!< Subscription Option to keep the retain flag as published option */
    uint8_t retain_handle;                       /*!< Subscription Option to handle retain option */
    bool is_share_subscribe;                     /*!< Whether subscribe is a shared subscription */
    const char *share_name;                      /*!< The name of shared subscription which is a part of $share/{share_name}/{topic} */
    mqtt5_user_property_handle_t user_property;  /*!< The handle for user property, call function esp_mqtt5_client_set_user_property to set it */
} esp_mqtt5_subscribe_property_config_t;

/**
 *  MQTT5 protocol disconnect properties configuration, more details refer to MQTT5 protocol document section 3.14.2.2
 */
typedef struct {
    uint32_t session_expiry_interval;            /*!< The interval time of session expiry */
    uint8_t disconnect_reason;                   /*!< The reason that connection disconnect, refer to mqtt5_error_reason_code */
    mqtt5_user_property_handle_t user_property;  /*!< The handle for user property, call function esp_mqtt5_client_set_user_property to set it */
} esp_mqtt5_disconnect_property_config_t;

/**
 *  MQTT5 protocol unsubscribe properties configuration, more details refer to MQTT5 protocol document section 3.10.2.1
 */
typedef struct {
    bool is_share_subscribe;                     /*!< Whether subscribe is a shared subscription */
    const char *share_name;                      /*!< The name of shared subscription which is a part of $share/{share_name}/{topic} */
    mqtt5_user_property_handle_t user_property;  /*!< The handle for user property, call function esp_mqtt5_client_set_user_property to set it */
} esp_mqtt5_unsubscribe_property_config_t;

#ifdef __cplusplus
}
#endif
