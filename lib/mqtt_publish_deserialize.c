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
int mqtt_msg_parse_publish(
    uint8_t* buf, size_t len, 
    bool* out_dup, int* out_qos, bool* out_retained, int* out_message_id,
    char** out_topic,  int* out_topic_len,
    mqtt_properties_t* out_props,
    uint8_t** out_data, int* out_data_len)
#else
int mqtt_msg_parse_publish(
    uint8_t* buf, size_t len, 
    bool* out_dup, int* out_qos, bool* out_retained, int* out_message_id,
    char** out_topic, int* out_topic_len, 
    uint8_t** out_data, int* out_data_len)
#endif
{
    int idx = 0;
    uint8_t t8;
    int msg_len;
    int tlen;
    
    t8 = mqtt_get_type(buf);
    idx += sizeof(uint8_t);

    if(t8 != MQTT_MSG_TYPE_PUBLISH) {
        return ESP_FAIL;
    }
    *out_dup = (mqtt_get_dup(buf) != 0);
    *out_qos = mqtt_get_qos(buf);
    *out_retained = (mqtt_get_retain(buf) != 0);

    idx += mqtt_msg_decode_int(&buf[idx], &msg_len);

    *out_topic = NULL;
    tlen = mqtt_msg_read_string(&buf[idx], out_topic, out_topic_len);
    idx += tlen;
    msg_len -= tlen;
    if(*out_topic_len <= 0 || msg_len < 0) {
        return ESP_FAIL;
    }

    if(*out_qos > 0) {
        tlen = mqtt_msg_read_int16(&buf[idx], out_message_id);
        idx += tlen;
        msg_len -= tlen;
    }

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

    *out_data = &buf[idx];

    // Data is truncated into multiple chunks if it doesn't fit the fixed buffer size
    // Return the amount of data that is available in this part of the packet
    *out_data_len = len - idx;
    return ESP_OK;
}

#ifdef CONFIG_MQTT_PROTOCOL_50
esp_err_t mqtt_msg_parse_ack(uint8_t* buf, size_t len, int* out_type, bool* out_dup, int* out_message_id, int* out_reason, mqtt_properties_t* out_props)
#else
esp_err_t mqtt_msg_parse_ack(uint8_t* buf, size_t len, int* out_type, bool* out_dup, int* out_message_id)
#endif
{
    int idx = 0;
    int msg_len;
    int tlen;
    
    *out_type = mqtt_get_type(buf);
    *out_dup = (mqtt_get_dup(buf) != 0);
    idx += sizeof(uint8_t);

    idx += mqtt_msg_decode_int(&buf[idx], &msg_len);
    if(msg_len < sizeof(uint16_t)) {
        return ESP_FAIL;
    }

    tlen = mqtt_msg_read_int16(&buf[idx], out_message_id);
    idx += tlen;
    msg_len -= tlen;

#ifdef CONFIG_MQTT_PROTOCOL_50
    char t8;
    if(out_reason) {
        // If the packet has no data it means the reason code was omitted
        // So we initialize it to 0
        *out_reason = 0;

        if (out_props) {
            out_props->count = 0;
        }

        if (msg_len > sizeof(uint16_t)) {
            // The packet contain a reason code
            idx += mqtt_msg_read_char(&buf[idx], &t8);
            msg_len--;
            *out_reason = t8;

            tlen = mqtt_msg_read_properties(&buf[idx], msg_len, out_props);
            if(tlen < 0) {
                return ESP_FAIL;
            }
            idx += tlen;
            msg_len -= tlen;
        }
    }
#endif

    return ESP_OK;
}
