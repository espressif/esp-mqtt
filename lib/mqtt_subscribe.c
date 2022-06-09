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

#ifdef CONFIG_MQTT_PROTOCOL_50
mqtt_message_t* mqtt_msg_subscribe(mqtt_connection_t* connection, uint16_t* message_id, mqtt_properties_t* properties, int count, mqtt_subscribe_topic_t topics[])
#else
mqtt_message_t* mqtt_msg_subscribe(mqtt_connection_t* connection, uint16_t* message_id, int count, mqtt_subscribe_topic_t topics[])
#endif
{
    init_message(connection);

    if (topics == NULL || topics[0].name[0] == '\0' || !count) {
        return fail_message(connection);
    }

    if ((*message_id = append_message_id(connection, 0)) == 0) {
        return fail_message(connection);
    }

#ifdef CONFIG_MQTT_PROTOCOL_50
    if (properties) {
        connection->message.length += mqtt_msg_write_properties(&connection->buffer[connection->message.length], properties);
    }
#endif

    for (int i = 0; i < count; ++i) {
        if (append_string(connection, topics[i].name, strlen(topics[i].name)) < 0) {
            return fail_message(connection);
        }

        if (connection->message.length + 1 > connection->buffer_length) {
            return fail_message(connection);
        }

        uint8_t opts = (uint8_t)topics[i].qos;
#ifdef CONFIG_MQTT_PROTOCOL_50
        opts |= (topics[i].no_local << 2); /* 1 bit */
        opts |= (topics[i].retain_as_published << 3); /* 1 bit */
        opts |= (topics[i].retain_handling << 4); /* 2 bits */
#endif
        connection->message.length += mqtt_msg_write_char(&connection->buffer[connection->message.length], opts);
    }

    return fini_message(connection, MQTT_MSG_TYPE_SUBSCRIBE, 0, 1, 0);
}

#ifdef CONFIG_MQTT_PROTOCOL_50
esp_err_t mqtt_msg_parse_subunsuback(uint8_t* buf, size_t len, int type, int* out_message_id, mqtt_properties_t* out_props, int max_count, int* out_count, int* out_reason_codes)
#else
esp_err_t mqtt_msg_parse_suback(uint8_t* buf, size_t len, int* out_message_id, int max_count, int* out_count, int out_qos[])
#endif
{
    int idx = 0;
    char t8;
    int msg_len;
    int tlen;
    
    t8 = mqtt_get_type(buf);
    idx += sizeof(uint8_t);

#ifdef CONFIG_MQTT_PROTOCOL_50
    if(t8 != (uint8_t)type) {
#else
    if(t8 != MQTT_MSG_TYPE_SUBACK) {
#endif
        return ESP_FAIL;
    }

    idx += mqtt_msg_decode_int(&buf[idx], &msg_len);
    if(msg_len < sizeof(uint16_t)) {
        // No space for message id
        return ESP_FAIL;
    }

    tlen = mqtt_msg_read_int16(&buf[idx], out_message_id);
    idx += tlen;
    msg_len -= tlen;

#ifdef CONFIG_MQTT_PROTOCOL_50
    if(out_props) {
        tlen = mqtt_msg_read_properties(&buf[idx], msg_len, out_props);
        if(tlen < 0) {
            return ESP_FAIL;
        }
        idx += tlen;
        msg_len -= tlen;
    }
#endif

    *out_count = 0;
    while(max_count && (msg_len > 0)) {
        idx += mqtt_msg_read_char(&buf[idx], &t8);
#ifdef CONFIG_MQTT_PROTOCOL_50
        out_reason_codes[*out_count] = t8;
#else
        out_qos[*out_count] = t8;
#endif
        msg_len--;
        (*out_count)++;

        if(*out_count > max_count) {
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

#ifdef CONFIG_MQTT_PROTOCOL_50
esp_err_t mqtt_msg_parse_suback(uint8_t* buf, size_t len, int* out_message_id, mqtt_properties_t* out_props, int max_count, int* out_count, int* out_reason_codes)
{
    return mqtt_msg_parse_subunsuback(buf, len, MQTT_MSG_TYPE_SUBACK, out_message_id, out_props, max_count, out_count, out_reason_codes);
}
#endif



char* mqtt_get_suback_data(uint8_t* buffer, size_t* length)
{
    // SUBACK payload length = total length - (fixed header (2 bytes) + variable header (2 bytes))
    // This requires the remaining length to be encoded in 1 byte.
    if (*length > 4) {
        *length -= 4;
        return (char*)(buffer + 4);
    }
    *length = 0;
    return NULL;
}
