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
mqtt_message_t* mqtt_msg_unsubscribe(mqtt_connection_t* connection, uint16_t* message_id, mqtt_properties_t* properties, int count, const char* topics[])
#else
mqtt_message_t* mqtt_msg_unsubscribe(mqtt_connection_t* connection, uint16_t* message_id, int count, const char* topics[])
#endif
{
    init_message(connection);

    if (topics == NULL || topics[0][0] == '\0' || !count) {
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
        if (append_string(connection, topics[i], strlen(topics[i])) < 0) {
            return fail_message(connection);
        }
    }

    return fini_message(connection, MQTT_MSG_TYPE_UNSUBSCRIBE, 0, 1, 0);
}

#ifdef CONFIG_MQTT_PROTOCOL_50
esp_err_t mqtt_msg_parse_unsuback(uint8_t* buf, size_t len, int* out_message_id, mqtt_properties_t* out_props, int max_count, int* out_count, int* out_reason_codes)
#else
esp_err_t mqtt_msg_parse_unsuback(uint8_t* buf, size_t len, int* out_message_id)
#endif
{
#ifdef CONFIG_MQTT_PROTOCOL_50
    return mqtt_msg_parse_subunsuback(buf, len, MQTT_MSG_TYPE_SUBACK, out_message_id, out_props, max_count, out_count, out_reason_codes);
#else
    esp_err_t err;
    int out_type;
    bool out_dup;
    err = mqtt_msg_parse_ack(buf, len, &out_type, &out_dup, out_message_id);
    if(out_type != MQTT_MSG_TYPE_UNSUBACK) {
        err = ESP_FAIL;
    }
    return err;
#endif
}
