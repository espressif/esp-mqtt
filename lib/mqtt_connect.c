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
#include <string.h>
#include "mqtt_msg.h"
#include "mqtt_config.h"
#include "platform.h"
#include "esp_log.h"
#include "mqtt_properties.h"
#include "mqtt_reason_codes.h"

enum mqtt_connect_flag {
    MQTT_CONNECT_FLAG_USERNAME = 1 << 7,
    MQTT_CONNECT_FLAG_PASSWORD = 1 << 6,
    MQTT_CONNECT_FLAG_WILL_RETAIN = 1 << 5,
    MQTT_CONNECT_FLAG_WILL = 1 << 2,
    MQTT_CONNECT_FLAG_CLEAN_SESSION = 1 << 1
};

static const char* TAG = "mqtt_connect";

mqtt_message_t* mqtt_msg_connect(mqtt_connection_t* connection, mqtt_connect_info_t* info)
{

    init_message(connection);

    int header_len = MQTT_VARIABLE_HEADER_SIZE;

    if (connection->message.length + header_len > connection->buffer_length) {
        return fail_message(connection);
    }
    uint8_t* variable_header = &connection->buffer[connection->message.length];
    connection->message.length += header_len;

    int header_idx = 0;
    header_idx += mqtt_msg_write_string(variable_header, MQTT_PROTOCOL_NAME, strlen(MQTT_PROTOCOL_NAME));
    header_idx += mqtt_msg_write_char(&variable_header[header_idx], (char)MQTT_PROTOCOL_VERSION);

    int flags_offset = header_idx;
    variable_header[header_idx++] = 0;                              // Flags
    header_idx += mqtt_msg_write_int16(&variable_header[header_idx], (int)info->keepalive);

#ifdef CONFIG_MQTT_PROTOCOL_50
    mqtt_properties_t props = { 0 };

    if(info->session_expiry_interval) {
        if(mqtt_property_put_number(&props, SESSION_EXPIRY_INTERVAL, info->session_expiry_interval) != ESP_OK) {
            return fail_message(connection);
        }
    }

    if(info->client_receive_maximum) {
        if(mqtt_property_put_number(&props, RECEIVE_MAXIMUM, info->client_receive_maximum) != ESP_OK) {
            return fail_message(connection);
        }
    }

    if(info->client_maximum_packet_size) {
        if(mqtt_property_put_number(&props, RECEIVE_MAXIMUM, info->client_maximum_packet_size) != ESP_OK) {
            return fail_message(connection);
        }
    }

    if(info->client_topic_alias_maximum) {
        if(mqtt_property_put_number(&props, TOPIC_ALIAS_MAXIMUM, info->client_topic_alias_maximum) != ESP_OK) {
            return fail_message(connection);
        }
    }

    if(info->request_response_info) {
        if(mqtt_property_put_number(&props, REQUEST_RESPONSE_INFORMATION, info->request_response_info) != ESP_OK) {
            return fail_message(connection);
        }
    }

    if(!info->request_problem_info) { // Default is 1
        if(mqtt_property_put_number(&props, REQUEST_PROBLEM_INFORMATION, info->request_problem_info) != ESP_OK) {
            return fail_message(connection);
        }
    }

    // User props
    for(int i = 0; i < info->user_props_count; ++i) {
        if(mqtt_property_put_pair(&props, USER_PROPERTY, info->user_props[i].key, info->user_props[i].value) != ESP_OK) {
            return fail_message(connection);
        }
    }

    if(info->auth.method) {
        if(mqtt_property_put_buffer(&props, AUTHENTICATION_METHOD, (uint8_t*)info->auth.method, strlen(info->auth.method)) != ESP_OK) {
            return fail_message(connection);
        }
    }

    if(info->auth.data && info->auth.data_len) {
        if(mqtt_property_put_buffer(&props, AUTHENTICATION_DATA, info->auth.data, info->auth.data_len) != ESP_OK) {
            return fail_message(connection);
        }
    }

    connection->message.length += mqtt_msg_write_properties(
        &connection->buffer[connection->message.length],
        &props
    );
#endif

    if (info->clean_session) {
        variable_header[flags_offset] |= MQTT_CONNECT_FLAG_CLEAN_SESSION;
    }

    if (info->client_id != NULL && info->client_id[0] != '\0') {
        if (append_string(connection, info->client_id, strlen(info->client_id)) < 0) {
            return fail_message(connection);
        }
    } else {
        if (append_string(connection, "", 0) < 0) {
            return fail_message(connection);
        }
    }

    if (info->will.topic != NULL && info->will.topic[0] != '\0') {
#ifdef CONFIG_MQTT_PROTOCOL_50
        mqtt_properties_t will_props = { 0 };
        
        if(info->will.delay_interval) {
            if(mqtt_property_put_number(&will_props, WILL_DELAY_INTERVAL, info->will.delay_interval) != ESP_OK) {
                return fail_message(connection);
            }
        }

        if(info->will.payload_format_indicator) {
            if(mqtt_property_put_number(&will_props, PAYLOAD_FORMAT_INDICATOR, info->will.payload_format_indicator) != ESP_OK) {
                return fail_message(connection);
            }
        }

        if(info->will.message_expiry_interval) {
            if(mqtt_property_put_number(&will_props, MESSAGE_EXPIRY_INTERVAL, info->will.message_expiry_interval) != ESP_OK) {
                return fail_message(connection);
            }
        }

        if(info->will.content_type) {
            if(mqtt_property_put_buffer(&will_props, CONTENT_TYPE, (uint8_t*)info->will.content_type, strlen(info->will.content_type)) != ESP_OK) {
                return fail_message(connection);
            }
        }

        if(info->will.response_topic) {
            if(mqtt_property_put_buffer(&will_props, RESPONSE_TOPIC, (uint8_t*)info->will.response_topic, strlen(info->will.response_topic)) != ESP_OK) {
                return fail_message(connection);
            }
        }

        if(info->will.correlation_data && info->will.correlation_data_len) {
            if(mqtt_property_put_buffer(&will_props, CORRELATION_DATA, info->will.correlation_data, info->will.correlation_data_len) != ESP_OK) {
                return fail_message(connection);
            }
        }

        // User props
        for (int i = 0; i < info->will.user_props_count; ++i) {
            if (mqtt_property_put_pair(&will_props, USER_PROPERTY, info->will.user_props[i].key, info->will.user_props[i].value) != ESP_OK) {
                return fail_message(connection);
            }
        }

        connection->message.length += mqtt_msg_write_properties(
            &connection->buffer[connection->message.length],
            &will_props
        );
#endif

        if (append_string(connection, info->will.topic, strlen(info->will.topic)) < 0) {
            return fail_message(connection);
        }

        if (append_string(connection, info->will.message, info->will.message_length) < 0) {
            return fail_message(connection);
        }

        variable_header[flags_offset] |= MQTT_CONNECT_FLAG_WILL;
        if (info->will.retain) {
            variable_header[flags_offset] |= MQTT_CONNECT_FLAG_WILL_RETAIN;
        }
        variable_header[flags_offset] |= (info->will.qos & 3) << 3;
    }

    if (info->username != NULL && info->username[0] != '\0') {
        if (append_string(connection, info->username, strlen(info->username)) < 0) {
            return fail_message(connection);
        }

        variable_header[flags_offset] |= MQTT_CONNECT_FLAG_USERNAME;
    }

    if (info->password != NULL && info->password[0] != '\0') {
        if (info->username == NULL || info->username[0] == '\0') {
            /* In case if password is set without username, we need to set a zero length username.
             * (otherwise we violate: MQTT-3.1.2-22: If the User Name Flag is set to 0 then the Password Flag MUST be set to 0.)
             */
            if (append_string(connection, "", 0) < 0) {
                return fail_message(connection);
            }

            variable_header[flags_offset] |= MQTT_CONNECT_FLAG_USERNAME;
        }

        if (append_string(connection, info->password, strlen(info->password)) < 0) {
            return fail_message(connection);
        }

        variable_header[flags_offset] |= MQTT_CONNECT_FLAG_PASSWORD;
    }

    return fini_message(connection, MQTT_MSG_TYPE_CONNECT, 0, 0, 0);
}

#ifdef CONFIG_MQTT_PROTOCOL_50
esp_err_t mqtt_msg_parse_connack(uint8_t* buf, int len, bool* out_session_present, int* out_reason, mqtt_properties_t* out_props)
#else
esp_err_t mqtt_msg_parse_connack(uint8_t* buf, int len, bool* out_session_present, int* out_reason)
#endif
{
    size_t idx = 0;
    char t8;
    int msg_len;
    int tlen;

    // Read the header
    idx += mqtt_msg_read_char(&buf[idx], &t8);

    if (mqtt_get_type(buf) != MQTT_MSG_TYPE_CONNACK) {
        ESP_LOGE(TAG, "Invalid MSG_TYPE response: %d, read_len: %d", mqtt_get_type(buf), len);
        return ESP_FAIL;
    }

    // Read the length
    idx += mqtt_msg_decode_int(&buf[idx], &msg_len);

    // Read the flags
    tlen = mqtt_msg_read_char(&buf[idx], &t8);
    idx += tlen;
    msg_len -= tlen;
    *out_session_present = t8 & 0x01;

    // Read the reason code
    tlen = mqtt_msg_read_char(&buf[idx], &t8);
    idx += tlen;
    msg_len -= tlen;
    *out_reason = t8;

#ifdef CONFIG_MQTT_PROTOCOL_50
    // Read properties
    tlen = mqtt_msg_read_properties(&buf[idx], msg_len, out_props);
    if (tlen < 0) {
        return ESP_FAIL;
    }
    idx += tlen;

    if (*out_reason == SUCCESS) {
        ESP_LOGD(TAG, "Connected");
        return ESP_OK;
    }
    ESP_LOGW(TAG, "Connection refused with error 0x%02X", *out_reason);
#else
    if (*out_reason == MQTT_CONNECTION_ACCEPTED) {
        ESP_LOGD(TAG, "Connected");
        return ESP_OK;
    }
    switch (*out_reason) {
    case MQTT_CONNECTION_REFUSE_PROTOCOL:
        ESP_LOGW(TAG, "Connection refused, bad protocol");
        break;
    case MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE:
        ESP_LOGW(TAG, "Connection refused, server unavailable");
        break;
    case MQTT_CONNECTION_REFUSE_BAD_USERNAME:
        ESP_LOGW(TAG, "Connection refused, bad username or password");
        break;
    case MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED:
        ESP_LOGW(TAG, "Connection refused, not authorized");
        break;
    default:
        ESP_LOGW(TAG, "Connection refused, Unknow reason");
        break;
    }
#endif
    return ESP_FAIL;
}

#ifdef CONFIG_MQTT_PROTOCOL_50
mqtt_message_t* mqtt_msg_disconnect(mqtt_connection_t* connection, int reason_code, mqtt_properties_t *properties)
#else
mqtt_message_t* mqtt_msg_disconnect(mqtt_connection_t* connection)
#endif
{
    init_message(connection);

#ifdef CONFIG_MQTT_PROTOCOL_50
    if (reason_code >= 0 && reason_code <= 162) {
        connection->message.length += mqtt_msg_write_char(&connection->buffer[connection->message.length], reason_code); /* must have reasonCode before properties */
        if (properties) {
            mqtt_msg_write_properties(&connection->buffer[connection->message.length], properties);
        }
    }
#endif

    return fini_message(connection, MQTT_MSG_TYPE_DISCONNECT, 0, 0, 0);
}

#ifdef CONFIG_MQTT_PROTOCOL_50
mqtt_message_t* mqtt_msg_auth(mqtt_connection_t* connection, int reason_code, mqtt_properties_t *properties)
{
    init_message(connection);

    if (reason_code >= 0 && reason_code <= 162) {
        connection->message.length += mqtt_msg_write_char(&connection->buffer[connection->message.length], reason_code); /* must have reasonCode before properties */
        if (properties) {
            mqtt_msg_write_properties(&connection->buffer[connection->message.length], properties);
        }
    }

    return fini_message(connection, MQTT_MSG_TYPE_AUTH, 0, 0, 0);
}
#endif

mqtt_message_t* mqtt_msg_pingreq(mqtt_connection_t* connection)
{
    init_message(connection);
    return fini_message(connection, MQTT_MSG_TYPE_PINGREQ, 0, 0, 0);
}

mqtt_message_t* mqtt_msg_pingresp(mqtt_connection_t* connection)
{
    init_message(connection);
    return fini_message(connection, MQTT_MSG_TYPE_PINGRESP, 0, 0, 0);
}
