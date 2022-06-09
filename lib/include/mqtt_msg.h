/*
* Copyright (c) 2014, Stephen Robinson
* Contributors 2022, Lorenzo Consolaro. tiko Energy Solutions
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
*
* 1. Redistributions of source code must retain the above copyright
* notice, this list of conditions and the following disclaimer.
* 2. Redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in the
* documentation and/or other materials provided with the distribution.
* 3. Neither the name of the copyright holder nor the names of its
* contributors may be used to endorse or promote products derived
* from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*
*/
#ifndef MQTT_MSG_H
#define MQTT_MSG_H
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "mqtt_config.h"
#include "mqtt_properties.h"
#include "platform.h"

#ifdef  __cplusplus
extern "C" {
#endif

/* 7      6     5     4     3     2     1     0 */
/*|      --- Message Type----     |  DUP Flag |    QoS Level    | Retain  | */
/*                    Remaining Length                 */


enum mqtt_message_type {
    MQTT_MSG_TYPE_CONNECT = 1,
    MQTT_MSG_TYPE_CONNACK = 2,
    MQTT_MSG_TYPE_PUBLISH = 3,
    MQTT_MSG_TYPE_PUBACK = 4,
    MQTT_MSG_TYPE_PUBREC = 5,
    MQTT_MSG_TYPE_PUBREL = 6,
    MQTT_MSG_TYPE_PUBCOMP = 7,
    MQTT_MSG_TYPE_SUBSCRIBE = 8,
    MQTT_MSG_TYPE_SUBACK = 9,
    MQTT_MSG_TYPE_UNSUBSCRIBE = 10,
    MQTT_MSG_TYPE_UNSUBACK = 11,
    MQTT_MSG_TYPE_PINGREQ = 12,
    MQTT_MSG_TYPE_PINGRESP = 13,
    MQTT_MSG_TYPE_DISCONNECT = 14,
#ifdef CONFIG_MQTT_PROTOCOL_50
    MQTT_MSG_TYPE_AUTH = 15
#endif
};

typedef struct mqtt_message {
    uint8_t *data;
    size_t length;
    size_t fragmented_msg_total_length;       /*!< Used for publishing long messages. total len of fragmented messages (zero for all other messages) */
    size_t fragmented_msg_data_offset;        /*!< Used for publishing long messages. data offset of fragmented messages (zero for all other messages) */
} mqtt_message_t;

typedef struct mqtt_connection {
    mqtt_message_t message;
#if MQTT_MSG_ID_INCREMENTAL
    uint16_t last_message_id;   /*!< last used id if incremental message id configured */
#endif
    uint8_t *buffer;       // Fixed size buffer. If the packet is bigger than this and needs to be stored, the exceeding data will be malloc
    size_t buffer_length;

} mqtt_connection_t;

typedef struct mqtt_connect_info_ {
    char *client_id;
    int64_t keepalive;          /*!< keepalive=0 -> keepalive is disabled */
    int clean_session;
#ifdef CONFIG_MQTT_PROTOCOL_50
    int session_expiry_interval;    // Session expiry interval in seconds (0 or absent, end on connection. 0xFFFFFFFF, do not expire)
    int client_receive_maximum;     // Maximum amount of QOS 1 2 messages to handle (if absent = 65535)
    int client_maximum_packet_size; // Maximum accepted packet size (absent, no limit)
    int client_topic_alias_maximum; // Highest accepted value for topic alias (0 or absent, no topic alias)
    bool request_response_info;     // Request response info in connack. Server MAY reply.
    bool request_problem_info;      // Request Reason String/User String on failure. (0, reason only on PUB CONNACK DISC)
    struct {
        char* key;
        char* value;
    } user_props[CONFIG_MQTT_PROPERTIES_MAX];
    int user_props_count;
    struct {
        char* method;          // Name of auth method used for extended auth. (absent, auth not performed)
        uint8_t* data;
        int data_len;
    } auth;
#endif
    // Will information
    struct {
        char *topic;
        char *message;
        int message_length;
        int qos;
        int retain;
#ifdef CONFIG_MQTT_PROTOCOL_50
        int delay_interval;         // interval in seconds before server will send will packet after disconnection. (0 or absent, do not wait)
        bool payload_format_indicator;// Ask the server to validate the message being a UTF8 string.
        int message_expiry_interval;// Lifetime of the Will message in seconds. Sent as publication expiry interval with will. (absent, no expiry)
        char* content_type;         // Application defined string describing content of the will message
        char* response_topic;       // string which is used as topic name for a response message -> makes the will message a <Request>
        uint8_t* correlation_data;  // Binary data used by the sender to match <Response Messages> with the relative <Request> (absent, no data required)
        int correlation_data_len;
        struct {
            char *key;
            char *value;
        } user_props[CONFIG_MQTT_PROPERTIES_MAX];
        int user_props_count;
#endif
    } will;
    // 
    char *username;
    char *password;

} mqtt_connect_info_t;

#define CONNECT_INFO_INIT {\
    .request_proplem_info = true;\
}

typedef struct mqtt_publish_info_
{
    char* topic;
    char* data;
    int data_len;
    int qos;
    int retain;
#ifdef CONFIG_MQTT_PROTOCOL_50
    bool payload_format_indicator;  // 0 Undefined, 1 String. Optional
    int message_expiry_interval;    // Validity before the packet is dropped by the broker.
    uint16_t topic_alias;           // Use with topic to set an alias. Send with no topic to use alias.
    char *response_topic;
    uint8_t* correlation_data;      // Identifier for response messages
    int correlation_data_len;
    struct {
        char *key;
        char *value;
    } user_props[CONFIG_MQTT_PROPERTIES_MAX];
    int user_props_count;
    // int subscription_id; //Should be populated by broker
    char* content_type;
#endif
} mqtt_publish_info_t;

typedef struct mqtt_subscribe_topic_
{
    const char *name;
    int qos;
#ifdef CONFIG_MQTT_PROTOCOL_50
    bool no_local; // If true, msgs will not be forwarded to connection with same client id of the publisher
    bool retain_as_published;
    int retain_handling; // 0: Send retain at time of subscribe. 1: Send at subscribe if subscription not exists. 2: Do not send on subscribe
#endif
} mqtt_subscribe_topic_t;

typedef struct mqtt_subscribe_info_ {
#ifdef CONFIG_MQTT_PROTOCOL_50
    int subscription_id;     // Subscription identifier
    struct {
        char *key;
        char *value;
    } user_props[CONFIG_MQTT_PROPERTIES_MAX];
    int user_props_count;
#endif
    mqtt_subscribe_topic_t topics[CONFIG_MQTT_SIMUL_TOPICS_MAX];
    int topics_count;
} mqtt_subscribe_info_t;

typedef struct mqtt_unsubscribe_info_ {
#ifdef CONFIG_MQTT_PROTOCOL_50
    struct {
        char *key;
        char *value;
    } user_props[CONFIG_MQTT_PROPERTIES_MAX];
    int user_props_count;
#endif
    const char* topics[CONFIG_MQTT_SIMUL_TOPICS_MAX];
    int topics_count;
} mqtt_unsubscribe_info_t;

static inline int mqtt_get_type(const uint8_t *buffer)
{
    return (buffer[0] & 0xf0) >> 4;
}
static inline int mqtt_get_dup(const uint8_t *buffer)
{
    return (buffer[0] & 0x08) >> 3;
}
static inline void mqtt_set_dup(uint8_t *buffer)
{
    buffer[0] |= 0x08;
}
static inline int mqtt_get_qos(const uint8_t *buffer)
{
    return (buffer[0] & 0x06) >> 1;
}
static inline int mqtt_get_retain(const uint8_t *buffer)
{
    return (buffer[0] & 0x01);
}

void mqtt_msg_init(mqtt_connection_t *connection, uint8_t *buffer, size_t buffer_length);
size_t mqtt_get_total_length(const uint8_t *buffer, size_t length, int *fixed_size_len);
int mqtt_has_valid_msg_hdr(uint8_t *buffer, size_t length);

#ifdef CONFIG_MQTT_PROTOCOL_50
mqtt_message_t *mqtt_msg_connect(mqtt_connection_t *connection, mqtt_connect_info_t *info);
mqtt_message_t *mqtt_msg_publish(mqtt_connection_t *connection, const char *topic, mqtt_properties_t* properties, const char *data, int data_length, int qos, int retain, uint16_t *message_id);
mqtt_message_t *mqtt_msg_puback(mqtt_connection_t *connection, uint16_t message_id, int reason_code, mqtt_properties_t* properties);
mqtt_message_t *mqtt_msg_pubrec(mqtt_connection_t *connection, uint16_t message_id, int reason_code, mqtt_properties_t* properties);
mqtt_message_t *mqtt_msg_pubrel(mqtt_connection_t *connection, uint8_t dup, uint16_t message_id, int reason_code, mqtt_properties_t* properties);
mqtt_message_t *mqtt_msg_pubcomp(mqtt_connection_t *connection, uint16_t message_id, int reason_code, mqtt_properties_t* properties);
mqtt_message_t* mqtt_msg_subscribe(mqtt_connection_t* connection, uint16_t* message_id, mqtt_properties_t* properties, int count, mqtt_subscribe_topic_t topics[]);
mqtt_message_t* mqtt_msg_unsubscribe(mqtt_connection_t* connection, uint16_t* message_id, mqtt_properties_t* properties, int count, const char* topic[]);
mqtt_message_t *mqtt_msg_pingreq(mqtt_connection_t *connection);
mqtt_message_t *mqtt_msg_pingresp(mqtt_connection_t *connection);
mqtt_message_t* mqtt_msg_disconnect(mqtt_connection_t* connection, int reason_code, mqtt_properties_t *properties);
esp_err_t mqtt_msg_parse_connack(uint8_t* buf, int len, bool* out_session_present, int* out_reason, mqtt_properties_t* out_props);
esp_err_t mqtt_msg_parse_publish(
    uint8_t* buf, size_t len, 
    bool* out_dup, int* out_qos, bool* out_retained, int* out_message_id,
    char** out_topic, int* out_topic_len,
    mqtt_properties_t* out_props,
    uint8_t** out_data, int* out_data_len);
esp_err_t mqtt_msg_parse_ack(uint8_t* buf, size_t len, int* out_type, bool* out_dup, int* out_message_id, int* out_reason, mqtt_properties_t* out_props);
esp_err_t mqtt_msg_parse_suback(uint8_t* buf, size_t len, int* out_message_id, mqtt_properties_t* out_props, int max_count, int* out_count, int* out_reason_codes);
esp_err_t mqtt_msg_parse_unsuback(uint8_t* buf, size_t len, int* out_message_id, mqtt_properties_t* out_props, int max_count, int* out_count, int* out_reason_codes);
esp_err_t mqtt_msg_parse_subunsuback(uint8_t* buf, size_t len, int type, int* out_message_id, mqtt_properties_t* out_props, int max_count, int* out_count, int* out_reason_codes);
#else
mqtt_message_t *mqtt_msg_connect(mqtt_connection_t *connection, mqtt_connect_info_t *info);
mqtt_message_t *mqtt_msg_publish(mqtt_connection_t *connection, const char *topic, const char *data, int data_length, int qos, int retain, uint16_t *message_id);
mqtt_message_t *mqtt_msg_puback(mqtt_connection_t *connection, uint16_t message_id);
mqtt_message_t *mqtt_msg_pubrec(mqtt_connection_t *connection, uint16_t message_id);
mqtt_message_t *mqtt_msg_pubrel(mqtt_connection_t *connection, uint8_t dup, uint16_t message_id);
mqtt_message_t *mqtt_msg_pubcomp(mqtt_connection_t *connection, uint16_t message_id);
mqtt_message_t* mqtt_msg_subscribe(mqtt_connection_t* connection, uint16_t* message_id, int count, mqtt_subscribe_topic_t topics[]);
mqtt_message_t* mqtt_msg_unsubscribe(mqtt_connection_t* connection, uint16_t* message_id, int count, const char* topic[]);
mqtt_message_t *mqtt_msg_pingreq(mqtt_connection_t *connection);
mqtt_message_t *mqtt_msg_pingresp(mqtt_connection_t *connection);
mqtt_message_t *mqtt_msg_disconnect(mqtt_connection_t *connection);
esp_err_t mqtt_msg_parse_connack(uint8_t* buf, int len, bool* out_session_present, int* out_reason);
esp_err_t mqtt_msg_parse_publish(
    uint8_t* buf, size_t len, 
    bool* out_dup, int* out_qos, bool* out_retained, int* out_message_id,
    char** out_topic, int* out_topic_len, uint8_t** out_data, int* out_data_len);
esp_err_t mqtt_msg_parse_ack(uint8_t* buf, size_t len, int* out_type, bool* out_dup, int* out_message_id);
esp_err_t mqtt_msg_parse_suback(uint8_t* buf, size_t len, int* out_message_id, int max_count, int* out_count, int out_qos[]);
esp_err_t mqtt_msg_parse_unsuback(uint8_t* buf, size_t len, int* out_message_id);
#endif

// Expose message handling methods (should rename them)
int init_message(mqtt_connection_t* connection);
mqtt_message_t* fail_message(mqtt_connection_t* connection);
mqtt_message_t* fini_message(mqtt_connection_t* connection, int type, int dup, int qos, int retain);
uint16_t append_message_id(mqtt_connection_t* connection, uint16_t message_id);
int append_string(mqtt_connection_t* connection, const char* string, int len);

int mqtt_msg_encode_int(uint8_t *buf, int length);
int mqtt_msg_decode_int(uint8_t *buf, int* out_length);

static inline size_t mqtt_msg_read_char(uint8_t* buf, char* out_val)
{
    *out_val = buf[0];
    return sizeof(uint8_t);
}

static inline int mqtt_msg_read_int16(uint8_t* buf, int* out_val)
{
    *out_val = ((int)buf[0]) << 8;
    *out_val |= buf[1];
    return sizeof(uint16_t);
}

// Return a reference to the index of the buffer where the string start
static inline size_t mqtt_msg_read_string(uint8_t *buf, char **out_string, int *out_len)
{
    mqtt_msg_read_int16(buf, out_len);
    *out_string = (char*)&buf[sizeof(uint16_t)];

    return sizeof(uint16_t) + *out_len;
}

static inline size_t mqtt_msg_write_char(uint8_t* buf, char val)
{
    *buf = val;
    return sizeof(uint8_t);
}

// Write a 2 byte integer to the buffer
static inline size_t mqtt_msg_write_int16(uint8_t* buf, int val)
{
    buf[0] = val >> 8;
    buf[1] = val & 0xFF;
    return sizeof(uint16_t);
}

static inline size_t mqtt_msg_write_string(uint8_t *buf, const char *string, int len)
{
    mqtt_msg_write_int16(buf, len);
    if(len) {
        memcpy(&buf[sizeof(uint16_t)], string, len);
    }
    return sizeof(uint16_t) + len;
}

#ifdef CONFIG_MQTT_PROTOCOL_50
static inline size_t mqtt_msg_write_int32(uint8_t* buf, int val)
{
    buf[0] = (val >> 24) & 0xFF;
    buf[1] = (val >> 16) & 0xFF;
    buf[2] = (val >> 8) & 0xFF;
    buf[3] = val & 0xFF;
    return sizeof(uint32_t);
}

static inline int mqtt_msg_read_int32(uint8_t* buf, int* outValue)
{
    *outValue = ((int)buf[0]) << 24;
    *outValue |= ((int)buf[1]) << 16;
    *outValue |= ((int)buf[2]) << 8;
    *outValue |= buf[3];
    return sizeof(uint32_t);
}
#endif

#ifdef  __cplusplus
}
#endif

#endif  /* MQTT_MSG_H */

