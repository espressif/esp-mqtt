/*
* Copyright (c) 2014, Stephen Robinson
* Contributions: 2022 Lorenzo Consolaro. tiko Energy Solutions
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

#define MQTT_MAX_FIXED_HEADER_SIZE 5


// Write MQTT String
int append_string(mqtt_connection_t* connection, const char* string, int len)
{
    if (connection->message.length + len + sizeof(uint16_t) > connection->buffer_length) {
        return -1;
    }

    connection->message.length += mqtt_msg_write_string(&connection->buffer[connection->message.length], string, len);

    return len + sizeof(uint16_t);
}

// Write int
uint16_t append_message_id(mqtt_connection_t* connection, uint16_t message_id)
{
    // If message_id is zero then we should assign one, otherwise
    // we'll use the one supplied by the caller
    while (message_id == 0) {
#if MQTT_MSG_ID_INCREMENTAL
        message_id = ++connection->last_message_id;
#else
        message_id = platform_random(65535);
#endif
    }

    if (connection->message.length + 2 > connection->buffer_length) {
        return 0;
    }

    connection->message.length += mqtt_msg_write_int16(&connection->buffer[connection->message.length], message_id);

    return message_id;
}

int init_message(mqtt_connection_t *connection)
{
    connection->message.length = MQTT_MAX_FIXED_HEADER_SIZE;
    return MQTT_MAX_FIXED_HEADER_SIZE;
}

mqtt_message_t *fail_message(mqtt_connection_t *connection)
{
    connection->message.data = connection->buffer;
    connection->message.length = 0;
    return &connection->message;
}

/**
 * Encodes the message length according to the MQTT algorithm
 * @param buf the buffer into which the encoded data is written
 * @param length the length to be encoded
 * @return the number of bytes written to buffer
 */
int mqtt_msg_encode_int(uint8_t *buf, int length)
{
    char encoded_length = 0;
    // Encode MQTT message length
    int len_bytes = 0; // size of encoded message length
    do {
        encoded_length = length % 128;
        length /= 128;
        if (length > 0) {
            encoded_length |= 0x80;
        }
        buf[len_bytes] = encoded_length;
        len_bytes++;
    } while (length > 0);
    return len_bytes;
}

int mqtt_msg_decode_int(uint8_t *buf, int* out_length)
{
    int i;

    *out_length = 0;
    for (i = 0; i < sizeof(uint32_t); ++i) {
        *out_length += (buf[i] & 0x7f) << (0x7 * i);
        if ((buf[i] & 0x80) == 0) {
            break;
        }
    }

    if(i > sizeof(uint32_t)) {
        // Wrong len - Try to limit the damage
        *out_length = 0;
        i = 0;
    }

    // Return number of bytes read
    return i + 1;
}

mqtt_message_t *fini_message(mqtt_connection_t *connection, int type, int dup, int qos, int retain)
{
    int message_length = connection->message.length - MQTT_MAX_FIXED_HEADER_SIZE;
    int total_length = message_length;

    uint8_t encoded_lens[4] = {0};
    // Check if we have fragmented message and update total_len
    if (connection->message.fragmented_msg_total_length) {
        total_length = connection->message.fragmented_msg_total_length - MQTT_MAX_FIXED_HEADER_SIZE;
    }

    // Encode MQTT message length
    int len_bytes = mqtt_msg_encode_int(encoded_lens, total_length);

    // Sanity check for MQTT header
    if (len_bytes + 1 > MQTT_MAX_FIXED_HEADER_SIZE) {
        return fail_message(connection);
    }

    // Save the header bytes
    connection->message.length = message_length + len_bytes + 1; // msg len + encoded_size len + type (1 byte)
    int offs = MQTT_MAX_FIXED_HEADER_SIZE - 1 - len_bytes;
    connection->message.data = connection->buffer + offs;
    connection->message.fragmented_msg_data_offset -= offs;
    // type byte
    connection->buffer[offs++] =  ((type & 0x0f) << 4) | ((dup & 1) << 3) | ((qos & 3) << 1) | (retain & 1);
    // length bytes
    for (int j = 0; j < len_bytes; j++) {
        connection->buffer[offs++] = encoded_lens[j];
    }

    return &connection->message;
}

void mqtt_msg_init(mqtt_connection_t *connection, uint8_t *buffer, size_t buffer_length)
{
    memset(connection, 0, sizeof(mqtt_connection_t));
    connection->buffer = buffer;
    connection->buffer_length = buffer_length;
}

size_t mqtt_get_total_length(const uint8_t *buffer, size_t length, int *fixed_size_len)
{
    int i;
    size_t totlen = 0;

    for (i = 1; i < length; ++i) {
        totlen += (buffer[i] & 0x7f) << (7 * (i - 1));
        if ((buffer[i] & 0x80) == 0) {
            ++i;
            break;
        }
    }
    totlen += i;
    if (fixed_size_len) {
        *fixed_size_len = i;
    }

    return totlen;
}

/*
 * check flags: [MQTT-2.2.2-1], [MQTT-2.2.2-2]
 * returns 0 if flags are invalid, otherwise returns 1
 */
int mqtt_has_valid_msg_hdr(uint8_t *buffer, size_t length)
{
    int qos, dup;

    if (length < 1) {
        return 0;
    }
    switch (mqtt_get_type(buffer)) {
    case MQTT_MSG_TYPE_CONNECT:
    case MQTT_MSG_TYPE_CONNACK:
    case MQTT_MSG_TYPE_PUBACK:
    case MQTT_MSG_TYPE_PUBREC:
    case MQTT_MSG_TYPE_PUBCOMP:
    case MQTT_MSG_TYPE_SUBACK:
    case MQTT_MSG_TYPE_UNSUBACK:
    case MQTT_MSG_TYPE_PINGREQ:
    case MQTT_MSG_TYPE_PINGRESP:
    case MQTT_MSG_TYPE_DISCONNECT:
        return (buffer[0] & 0x0f) == 0;  /* all flag bits are 0 */
    case MQTT_MSG_TYPE_PUBREL:
    case MQTT_MSG_TYPE_SUBSCRIBE:
    case MQTT_MSG_TYPE_UNSUBSCRIBE:
        return (buffer[0] & 0x0f) == 0x02;  /* only bit 1 is set */
    case MQTT_MSG_TYPE_PUBLISH:
        qos = mqtt_get_qos(buffer);
        dup = mqtt_get_dup(buffer);
        /*
         * there is no qos=3  [MQTT-3.3.1-4]
         * dup flag must be set to 0 for all qos=0 messages [MQTT-3.3.1-2]
         */
        return (qos < 3) && ((qos > 0) || (dup == 0));
    default:
        return 0;
    }
}
