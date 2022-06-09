/*
 * Copyright (c) 2022, Lorenzo Consolaro. tiko Energy Solutions
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
#ifndef _MQTT_PROPERTIES_H_
#define _MQTT_PROPERTIES_H_

#include "mqtt_config.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef CONFIG_MQTT_PROTOCOL_50

    typedef enum mqtt_property_names_
    {
        PAYLOAD_FORMAT_INDICATOR = 1,
        MESSAGE_EXPIRY_INTERVAL = 2,
        CONTENT_TYPE = 3,
        RESPONSE_TOPIC = 8,
        CORRELATION_DATA = 9,
        SUBSCRIPTION_IDENTIFIER = 11,
        SESSION_EXPIRY_INTERVAL = 17,
        ASSIGNED_CLIENT_IDENTIFER = 18,
        SERVER_KEEP_ALIVE = 19,
        AUTHENTICATION_METHOD = 21,
        AUTHENTICATION_DATA = 22,
        REQUEST_PROBLEM_INFORMATION = 23,
        WILL_DELAY_INTERVAL = 24,
        REQUEST_RESPONSE_INFORMATION = 25,
        RESPONSE_INFORMATION = 26,
        SERVER_REFERENCE = 28,
        REASON_STRING = 31,
        RECEIVE_MAXIMUM = 33,
        TOPIC_ALIAS_MAXIMUM = 34,
        TOPIC_ALIAS = 35,
        MAXIMUM_QOS = 36,
        RETAIN_AVAILABLE = 37,
        USER_PROPERTY = 38,
        MAXIMUM_PACKET_SIZE = 39,
        WILDCARD_SUBSCRIPTION_AVAILABLE = 40,
        SUBSCRIPTION_IDENTIFIER_AVAILABLE = 41,
        SHARED_SUBSCRIPTION_AVAILABLE = 42
    } mqtt_property_names_t;

    typedef enum mqtt_property_types_
    {
        BYTE,
        TWO_BYTE_INTEGER,
        FOUR_BYTE_INTEGER,
        VARIABLE_BYTE_INTEGER,
        BINARY_DATA,
        UTF_8_ENCODED_STRING,
        UTF_8_STRING_PAIR
    } mqtt_property_types_t;

    typedef struct mqtt_property_
    {
        int identifier; /* mbi */
        union
        {
            char byte;
            int integer2;
            int integer4;
            struct
            {
                int len;
                char *data;
            } data; // Either string or binary data
            struct
            {
                int len;
                char *data;
            } key; // Used in key-value pair
        } value;
    } mqtt_property_t;

    typedef struct mqtt_properties_
    {
        int count;  /* number of property entries */
        int length; /* mbi: byte length of all properties */
        mqtt_property_t array[CONFIG_MQTT_PROPERTIES_MAX];
    } mqtt_properties_t;

    int mqtt_property_get_type(int identifier);
    int mqtt_msg_write_properties(uint8_t *buf, mqtt_properties_t *props);
    int mqtt_msg_read_properties(uint8_t *buf, int rem_len, mqtt_properties_t *out_properties);

    int mqtt_property_get_number(mqtt_properties_t* props, mqtt_property_names_t name, int* out_count);
    void mqtt_property_get_buffer(mqtt_properties_t* props, mqtt_property_names_t name, uint8_t** out_data, int* out_data_len, int* out_count);
    void mqtt_property_get_pair(mqtt_properties_t* props, mqtt_property_names_t name,  char** out_key, int* out_key_len, char** out_data, int* out_data_len, int* out_count);
    int mqtt_property_put_number(mqtt_properties_t* props, mqtt_property_names_t name, int value);
    int mqtt_property_put_buffer(mqtt_properties_t* props, mqtt_property_names_t name, uint8_t* in_data, int in_data_len);
    int mqtt_property_put_pair(mqtt_properties_t* props, mqtt_property_names_t name,  const char* key, const char* data);

#ifdef CONFIG_MQTT_KEEP_TO_STRING
    typedef struct mqtt_property_name_string_
    {
        mqtt_property_names_t code;
        const char *str;
    } mqtt_property_name_string_t;
    static mqtt_property_name_string_t _mqtt_property_name_mapping[] = {
        {1, "PAYLOAD_FORMAT_INDICATOR"},
        {2, "MESSAGE_EXPIRY_INTERVAL"},
        {3, "CONTENT_TYPE"},
        {8, "RESPONSE_TOPIC"},
        {9, "CORRELATION_DATA"},
        {11, "SUBSCRIPTION_IDENTIFIER"},
        {17, "SESSION_EXPIRY_INTERVAL"},
        {18, "ASSIGNED_CLIENT_IDENTIFER"},
        {19, "SERVER_KEEP_ALIVE"},
        {21, "AUTHENTICATION_METHOD"},
        {22, "AUTHENTICATION_DATA"},
        {23, "REQUEST_PROBLEM_INFORMATION"},
        {24, "WILL_DELAY_INTERVAL"},
        {25, "REQUEST_RESPONSE_INFORMATION"},
        {26, "RESPONSE_INFORMATION"},
        {28, "SERVER_REFERENCE"},
        {31, "REASON_STRING"},
        {33, "RECEIVE_MAXIMUM"},
        {34, "TOPIC_ALIAS_MAXIMUM"},
        {35, "TOPIC_ALIAS"},
        {36, "MAXIMUM_QOS"},
        {37, "RETAIN_AVAILABLE"},
        {38, "USER_PROPERTY"},
        {39, "MAXIMUM_PACKET_SIZE"},
        {40, "WILDCARD_SUBSCRIPTION_AVAILABLE"},
        {41, "SUBSCRIPTION_IDENTIFIER_AVAILABLE"},
        {42, "SHARED_SUBSCRIPTION_AVAILABLE"}};

    static inline const char *
    mqtt_property_name_to_string(mqtt_property_names_t r)
    {
        int size = sizeof(_mqtt_property_name_mapping) / sizeof(mqtt_property_name_string_t);
        for (int i = 0; i < size; ++i)
        {
            if (_mqtt_property_name_mapping[i].code == r)
            {
                return _mqtt_property_name_mapping[i].str;
            }
        }
        return "";
    }
#endif

#endif

#ifdef __cplusplus
}
#endif //__cplusplus

#endif
