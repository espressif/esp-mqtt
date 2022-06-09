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
#ifndef _MQTT_REASON_CODES_H_
#define _MQTT_REASON_CODES_H_

#include "mqtt_config.h"

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef CONFIG_MQTT_PROTOCOL_50
    typedef enum mqtt_reason_code_
    {
        SUCCESS = 0,
        NORMAL_DISCONNECTION = 0,
        GRANTED_QOS_0 = 0,
        GRANTED_QOS_1 = 1,
        GRANTED_QOS_2 = 2,
        DISCONNECT_WITH_WILL_MESSAGE = 4,
        NO_MATCHING_SUBSCRIBERS = 16,
        NO_SUBSCRIPTION_FOUND = 17,
        CONTINUE_AUTHENTICATION = 24,
        RE_AUTHENTICATE = 25,
        UNSPECIFIED_ERROR = 0x80,
        MALFORMED_PACKET = 0x81,
        PROTOCOL_ERROR = 0x82,
        IMPLEMENTATION_SPECIFIC_ERROR = 0x83,
        UNSUPPORTED_PROTOCOL_VERSION = 0x84,
        CLIENT_IDENTIFIER_NOT_VALID = 0x85,
        BAD_USER_NAME_OR_PASSWORD = 0x86,
        NOT_AUTHORIZED = 0x87,
        SERVER_UNAVAILABLE = 0x88,
        SERVER_BUSY = 0x89,
        BANNED = 0x8A,
        SERVER_SHUTTING_DOWN = 0x8B,
        BAD_AUTHENTICATION_METHOD = 0x8C,
        KEEP_ALIVE_TIMEOUT = 141,
        SESSION_TAKEN_OVER = 142,
        TOPIC_FILTER_INVALID = 143,
        TOPIC_NAME_INVALID = 0x90,
        PACKET_IDENTIFIER_IN_USE = 145,
        PACKET_IDENTIFIER_NOT_FOUND = 146,
        RECEIVE_MAXIMUM_EXCEEDED = 147,
        TOPIC_ALIAS_INVALID = 148,
        PACKET_TOO_LARGE = 0x95,
        MESSAGE_RATE_TOO_HIGH = 150,
        QUOTA_EXCEEDED = 0x97,
        ADMINISTRATIVE_ACTION = 152,
        PAYLOAD_FORMAT_INVALID = 0x99,
        RETAIN_NOT_SUPPORTED = 0x9A,
        QOS_NOT_SUPPORTED = 0x9B,
        USE_ANOTHER_SERVER = 0x9C,
        SERVER_MOVED = 0x9D,
        SHARED_SUBSCRIPTION_NOT_SUPPORTED = 158,
        CONNECTION_RATE_EXCEEDED = 0x9F,
        MAXIMUM_CONNECT_TIME = 160,
        SUBSCRIPTION_IDENTIFIERS_NOT_SUPPORTED = 161,
        WILDCARD_SUBSCRIPTION_NOT_SUPPORTED = 162
    } mqtt_reason_code_t;

#ifdef CONFIG_MQTT_KEEP_TO_STRING
    typedef struct mqtt_reason_code_string_
    {
        mqtt_reason_code_t code;
        const char *str;
    }mqtt_reason_code_string_t;
    static mqtt_reason_code_string_t _mqtt_reason_code_mapping[] = {
        {0, "SUCCESS/NORMAL_DISCONNECTION/GRANTED_QOS_0"},
        {1, "GRANTED_QOS_1"},
        {2, "GRANTED_QOS_2"},
        {4, "DISCONNECT_WITH_WILL_MESSAGE"},
        {16, "NO_MATCHING_SUBSCRIBERS"},
        {17, "NO_SUBSCRIPTION_FOUND"},
        {24, "CONTINUE_AUTHENTICATION"},
        {25, "RE_AUTHENTICATE"},
        {0x80, "UNSPECIFIED_ERROR"},
        {0x81, "MALFORMED_PACKET"},
        {0x82, "PROTOCOL_ERROR"},
        {0x83, "IMPLEMENTATION_SPECIFIC_ERROR"},
        {0x84, "UNSUPPORTED_PROTOCOL_VERSION"},
        {0x85, "CLIENT_IDENTIFIER_NOT_VALID"},
        {0x86, "BAD_USER_NAME_OR_PASSWORD"},
        {0x87, "NOT_AUTHORIZED"},
        {0x88, "SERVER_UNAVAILABLE"},
        {0x89, "SERVER_BUSY"},
        {0x8A, "BANNED"},
        {0x8B, "SERVER_SHUTTING_DOWN"},
        {0x8C, "BAD_AUTHENTICATION_METHOD"},
        {141, "KEEP_ALIVE_TIMEOUT"},
        {142, "SESSION_TAKEN_OVER"},
        {143, "TOPIC_FILTER_INVALID"},
        {0x90, "TOPIC_NAME_INVALID"},
        {145, "PACKET_IDENTIFIER_IN_USE"},
        {146, "PACKET_IDENTIFIER_NOT_FOUND"},
        {147, "RECEIVE_MAXIMUM_EXCEEDED"},
        {148, "TOPIC_ALIAS_INVALID"},
        {0x95, "PACKET_TOO_LARGE"},
        {150, "MESSAGE_RATE_TOO_HIGH"},
        {0x97, "QUOTA_EXCEEDED"},
        {152, "ADMINISTRATIVE_ACTION"},
        {0x99, "PAYLOAD_FORMAT_INVALID"},
        {0x9A, "RETAIN_NOT_SUPPORTED"},
        {0x9B, "QOS_NOT_SUPPORTED"},
        {0x9C, "USE_ANOTHER_SERVER"},
        {0x9D, "SERVER_MOVED"},
        {158, "SHARED_SUBSCRIPTION_NOT_SUPPORTED"},
        {0x9F, "CONNECTION_RATE_EXCEEDED"},
        {160, "MAXIMUM_CONNECT_TIME"},
        {161, "SUBSCRIPTION_IDENTIFIERS_NOT_SUPPORTED"},
        {162, "WILDCARD_SUBSCRIPTION_NOT_SUPPORTED"}};

    static inline const char *
    mqtt_reason_code_to_string(mqtt_reason_code_t r)
    {
        int size = sizeof(_mqtt_reason_code_mapping) / sizeof(struct mqtt_reason_code_string_);
        for(int i = 0; i < size; ++i) {
            if(_mqtt_reason_code_mapping[i].code == r) {
                return _mqtt_reason_code_mapping[i].str;
            }
        }
        return "";
    }
#endif

#else
/**
 * MQTT connection error codes propagated via ERROR event
 */
typedef enum mqtt_reason_code_t
{
    MQTT_CONNECTION_ACCEPTED = 0,                  /*!< Connection accepted  */
    MQTT_CONNECTION_REFUSE_PROTOCOL = 1,           /*!< MQTT connection refused reason: Wrong protocol */
    MQTT_CONNECTION_REFUSE_ID_REJECTED = 2,        /*!< MQTT connection refused reason: ID rejected */
    MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE = 3, /*!< MQTT connection refused reason: Server unavailable */
    MQTT_CONNECTION_REFUSE_BAD_USERNAME = 4,       /*!< MQTT connection refused reason: Wrong user */
    MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED = 5      /*!< MQTT connection refused reason: Wrong username or password */
} mqtt_reason_code_t;
#endif

#ifdef __cplusplus
}
#endif //__cplusplus

#endif
