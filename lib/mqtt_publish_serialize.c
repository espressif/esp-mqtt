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
#include "mqtt_properties.h"

#ifdef CONFIG_MQTT_PROTOCOL_50
mqtt_message_t* mqtt_msg_publish(mqtt_connection_t* connection, const char* topic, mqtt_properties_t* properties, const char* data, int data_length, int qos, int retain, uint16_t* message_id)
#else
mqtt_message_t* mqtt_msg_publish(mqtt_connection_t* connection, const char* topic, const char* data, int data_length, int qos, int retain, uint16_t* message_id)
#endif
{
    init_message(connection);

    if (topic == NULL || topic[0] == '\0') {
        return fail_message(connection);
    }

    if (append_string(connection, topic, strlen(topic)) < 0) {
        return fail_message(connection);
    }

    if (data == NULL && data_length > 0) {
        return fail_message(connection);
    }

    if (qos > 0) {
        if ((*message_id = append_message_id(connection, 0)) == 0) {
            return fail_message(connection);
        }
    } else {
        *message_id = 0;
    }

#ifdef CONFIG_MQTT_PROTOCOL_50
    if (properties) {
        connection->message.length += mqtt_msg_write_properties(&connection->buffer[connection->message.length], properties);
    }
#endif

    if (connection->message.length + data_length > connection->buffer_length) {
        // Not enough size in buffer -> fragment this message
        connection->message.fragmented_msg_data_offset = connection->message.length;
        memcpy(connection->buffer + connection->message.length, data, connection->buffer_length - connection->message.length);
        connection->message.length = connection->buffer_length;
        connection->message.fragmented_msg_total_length = data_length + connection->message.fragmented_msg_data_offset;
    } else {
        if (data != NULL) {
            memcpy(connection->buffer + connection->message.length, data, data_length);
            connection->message.length += data_length;
        }
        connection->message.fragmented_msg_total_length = 0;
    }
    return fini_message(connection, MQTT_MSG_TYPE_PUBLISH, 0, qos, retain);
}

#ifdef CONFIG_MQTT_PROTOCOL_50
static mqtt_message_t* mqtt_msg_ack(mqtt_connection_t* connection, uint8_t packet_type, uint8_t dup, uint16_t message_id, int reason_code, mqtt_properties_t* properties)
#else
static mqtt_message_t* mqtt_msg_ack(mqtt_connection_t* connection, uint8_t packet_type, uint8_t dup, uint16_t message_id)
#endif
{
    init_message(connection);
    if (append_message_id(connection, message_id) == 0) {
        return fail_message(connection);
    }

#ifdef CONFIG_MQTT_PROTOCOL_50
    if (reason_code >= 0) {
        connection->message.length += mqtt_msg_write_char(&connection->buffer[connection->message.length], reason_code);
        if (properties) {
            connection->message.length += mqtt_msg_write_properties(&connection->buffer[connection->message.length], properties);
        }
    }
#endif

    return fini_message(connection, packet_type, dup, (packet_type == MQTT_MSG_TYPE_PUBREL) ? 1 : 0, 0);
}

#ifdef CONFIG_MQTT_PROTOCOL_50
mqtt_message_t* mqtt_msg_puback(mqtt_connection_t* connection, uint16_t message_id, int reason_code, mqtt_properties_t* properties)
#else
mqtt_message_t* mqtt_msg_puback(mqtt_connection_t* connection, uint16_t message_id)
#endif
{
#ifdef CONFIG_MQTT_PROTOCOL_50
    return mqtt_msg_ack(connection, MQTT_MSG_TYPE_PUBACK, 0, message_id, reason_code, properties);
#else
    return mqtt_msg_ack(connection, MQTT_MSG_TYPE_PUBACK, 0, message_id);
#endif
}

#ifdef CONFIG_MQTT_PROTOCOL_50
mqtt_message_t* mqtt_msg_pubrec(mqtt_connection_t* connection, uint16_t message_id, int reason_code, mqtt_properties_t* properties)
#else
mqtt_message_t* mqtt_msg_pubrec(mqtt_connection_t* connection, uint16_t message_id)
#endif
{
#ifdef CONFIG_MQTT_PROTOCOL_50
    return mqtt_msg_ack(connection, MQTT_MSG_TYPE_PUBREC, 0, message_id, reason_code, properties);
#else
    return mqtt_msg_ack(connection, MQTT_MSG_TYPE_PUBREC, 0, message_id);
#endif
}

#ifdef CONFIG_MQTT_PROTOCOL_50
mqtt_message_t* mqtt_msg_pubrel(mqtt_connection_t* connection, uint8_t dup, uint16_t message_id, int reason_code, mqtt_properties_t* properties)
#else
mqtt_message_t* mqtt_msg_pubrel(mqtt_connection_t* connection, uint8_t dup, uint16_t message_id)
#endif
{
#ifdef CONFIG_MQTT_PROTOCOL_50
    return mqtt_msg_ack(connection, MQTT_MSG_TYPE_PUBREL, dup, message_id, reason_code, properties);
#else
    return mqtt_msg_ack(connection, MQTT_MSG_TYPE_PUBREL, dup, message_id);
#endif
}

#ifdef CONFIG_MQTT_PROTOCOL_50
mqtt_message_t* mqtt_msg_pubcomp(mqtt_connection_t* connection, uint16_t message_id, int reason_code, mqtt_properties_t* properties)
#else
mqtt_message_t* mqtt_msg_pubcomp(mqtt_connection_t* connection, uint16_t message_id)
#endif
{
#ifdef CONFIG_MQTT_PROTOCOL_50
    return mqtt_msg_ack(connection, MQTT_MSG_TYPE_PUBCOMP, 0, message_id, reason_code, properties);
#else
    return mqtt_msg_ack(connection, MQTT_MSG_TYPE_PUBCOMP, 0, message_id);
#endif
}

