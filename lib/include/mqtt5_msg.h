#ifndef MQTT5_MSG_H
#define MQTT5_MSG_H
#include <stdint.h>
#include <stdbool.h>
#include "sys/queue.h"
#include "mqtt_config.h"
#include "mqtt_msg.h"
#include "mqtt_client.h"
#ifdef  __cplusplus
extern "C" {
#endif

enum mqtt_properties_type {
    MQTT5_PROPERTY_PAYLOAD_FORMAT_INDICATOR      = 0x01,
    MQTT5_PROPERTY_MESSAGE_EXPIRY_INTERVAL       = 0x02,
    MQTT5_PROPERTY_CONTENT_TYPE                  = 0x03,
    MQTT5_PROPERTY_RESPONSE_TOPIC                = 0x08,
    MQTT5_PROPERTY_CORRELATION_DATA              = 0x09,
    MQTT5_PROPERTY_SUBSCRIBE_IDENTIFIER          = 0x0B,
    MQTT5_PROPERTY_SESSION_EXPIRY_INTERVAL       = 0x11,
    MQTT5_PROPERTY_ASSIGNED_CLIENT_IDENTIFIER    = 0x12,
    MQTT5_PROPERTY_SERVER_KEEP_ALIVE             = 0x13,
    MQTT5_PROPERTY_AUTHENTICATION_METHOD         = 0x15,
    MQTT5_PROPERTY_AUTHENTICATION_DATA           = 0x16,
    MQTT5_PROPERTY_REQUEST_PROBLEM_INFO          = 0x17,
    MQTT5_PROPERTY_WILL_DELAY_INTERVAL           = 0x18,
    MQTT5_PROPERTY_REQUEST_RESP_INFO             = 0x19,
    MQTT5_PROPERTY_RESP_INFO                     = 0x1A,
    MQTT5_PROPERTY_SERVER_REFERENCE              = 0x1C,
    MQTT5_PROPERTY_REASON_STRING                 = 0x1F,
    MQTT5_PROPERTY_RECEIVE_MAXIMUM               = 0x21,
    MQTT5_PROPERTY_TOPIC_ALIAS_MAXIMIM           = 0x22,
    MQTT5_PROPERTY_TOPIC_ALIAS                   = 0x23,
    MQTT5_PROPERTY_MAXIMUM_QOS                   = 0x24,
    MQTT5_PROPERTY_RETAIN_AVAILABLE              = 0x25,
    MQTT5_PROPERTY_USER_PROPERTY                 = 0x26,
    MQTT5_PROPERTY_MAXIMUM_PACKET_SIZE           = 0x27,
    MQTT5_PROPERTY_WILDCARD_SUBSCR_AVAILABLE     = 0x28,
    MQTT5_PROPERTY_SUBSCR_IDENTIFIER_AVAILABLE   = 0x29,
    MQTT5_PROPERTY_SHARED_SUBSCR_AVAILABLE       = 0x2A,
};

typedef struct mqtt5_user_property {
    char *key;
    char *value;
    STAILQ_ENTRY(mqtt5_user_property) next;
} mqtt5_user_property_t;
STAILQ_HEAD(mqtt5_user_property_list_t, mqtt5_user_property);
typedef struct mqtt5_user_property *mqtt5_user_property_item_t;

typedef struct {
    uint32_t maximum_packet_size;
    uint16_t receive_maximum;
    uint16_t topic_alias_maximum;
    uint8_t max_qos;
    bool retain_available;
    bool wildcard_subscribe_available;
    bool subscribe_identifiers_available;
    bool shared_subscribe_available;
    char *response_info;
} esp_mqtt5_connection_server_resp_property_t;

typedef struct {
    bool payload_format_indicator;
    uint32_t message_expiry_interval;
    uint16_t topic_alias;
    char *response_topic;
    int response_topic_len;
    char *correlation_data;
    uint16_t correlation_data_len;
    char *content_type;
    int content_type_len;
    uint16_t subscribe_id;
} esp_mqtt5_publish_resp_property_t;

typedef struct {
    uint32_t session_expiry_interval;
    uint32_t maximum_packet_size;
    uint16_t receive_maximum;
    uint16_t topic_alias_maximum;
    bool request_resp_info;
    bool request_problem_info;
    mqtt5_user_property_handle_t user_property;
} esp_mqtt5_connection_property_storage_t;

typedef struct {
    uint32_t will_delay_interval;
    uint32_t message_expiry_interval;
    bool payload_format_indicator;
    char *content_type;
    char *response_topic;
    char *correlation_data;
    uint16_t correlation_data_len;
    mqtt5_user_property_handle_t user_property;
} esp_mqtt5_connection_will_property_storage_t;

#define mqtt5_get_type mqtt_get_type

#define mqtt5_get_dup mqtt_get_dup

#define mqtt5_set_dup mqtt_set_dup

#define mqtt5_get_qos mqtt_get_qos

#define mqtt5_get_retain mqtt_get_retain

#define mqtt5_msg_init mqtt_msg_init

#define mqtt5_get_total_length mqtt_get_total_length

#define mqtt5_has_valid_msg_hdr mqtt_has_valid_msg_hdr

#define mqtt5_msg_pingreq mqtt_msg_pingreq

#define mqtt5_msg_pingresp mqtt_msg_pingresp

#define mqtt5_get_unsuback_data mqtt5_get_suback_data

#define mqtt5_get_pubcomp_data mqtt5_get_puback_data

uint16_t mqtt5_get_id(uint8_t *buffer, size_t length);
char *mqtt5_get_publish_property_payload(uint8_t *buffer, size_t buffer_length, char **msg_topic, size_t *msg_topic_len, esp_mqtt5_publish_resp_property_t *resp_property, uint16_t *property_len, size_t *payload_len, mqtt5_user_property_handle_t *user_property);
char *mqtt5_get_suback_data(uint8_t *buffer, size_t *length, mqtt5_user_property_handle_t *user_property);
char *mqtt5_get_puback_data(uint8_t *buffer, size_t *length, mqtt5_user_property_handle_t *user_property);
mqtt_message_t *mqtt5_msg_connect(mqtt_connection_t *connection, mqtt_connect_info_t *info, esp_mqtt5_connection_property_storage_t *property, esp_mqtt5_connection_will_property_storage_t *will_property);
mqtt_message_t *mqtt5_msg_publish(mqtt_connection_t *connection, const char *topic, const char *data, int data_length, int qos, int retain, uint16_t *message_id, const esp_mqtt5_publish_property_config_t *property, const char *resp_info);
esp_err_t mqtt5_msg_parse_connack_property(uint8_t *buffer, size_t buffer_len, mqtt_connect_info_t *connection_info, esp_mqtt5_connection_property_storage_t *connection_property, esp_mqtt5_connection_server_resp_property_t *resp_property, int *reason_code, uint8_t *ack_flag, mqtt5_user_property_handle_t *user_property);
int mqtt5_msg_get_reason_code(uint8_t *buffer, size_t length);
mqtt_message_t *mqtt5_msg_subscribe(mqtt_connection_t *connection, const esp_mqtt_topic_t *topic, int size, uint16_t *message_id, const esp_mqtt5_subscribe_property_config_t *property);
mqtt_message_t *mqtt5_msg_unsubscribe(mqtt_connection_t *connection, const char *topic, uint16_t *message_id, const esp_mqtt5_unsubscribe_property_config_t *property);
mqtt_message_t *mqtt5_msg_disconnect(mqtt_connection_t *connection, esp_mqtt5_disconnect_property_config_t *disconnect_property_info);
mqtt_message_t *mqtt5_msg_pubcomp(mqtt_connection_t *connection, uint16_t message_id);
mqtt_message_t *mqtt5_msg_pubrel(mqtt_connection_t *connection, uint16_t message_id);
mqtt_message_t *mqtt5_msg_pubrec(mqtt_connection_t *connection, uint16_t message_id);
mqtt_message_t *mqtt5_msg_puback(mqtt_connection_t *connection, uint16_t message_id);

#ifdef  __cplusplus
}
#endif

#endif  /* MQTT5_MSG_H */

