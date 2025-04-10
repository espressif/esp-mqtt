/*
 * SPDX-FileCopyrightText: 2014 Stephen Robinson
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "mqtt_msg.h"
#include "esp_log.h"
#include "mqtt_config.h"
#include "platform.h"

#define MQTT_MAX_FIXED_HEADER_SIZE 5
#define MQTT_3_1_VARIABLE_HEADER_SIZE 12
#define MQTT_3_1_1_VARIABLE_HEADER_SIZE 10

enum mqtt_connect_flag {
    MQTT_CONNECT_FLAG_USERNAME = 1 << 7,
    MQTT_CONNECT_FLAG_PASSWORD = 1 << 6,
    MQTT_CONNECT_FLAG_WILL_RETAIN = 1 << 5,
    MQTT_CONNECT_FLAG_WILL = 1 << 2,
    MQTT_CONNECT_FLAG_CLEAN_SESSION = 1 << 1
};

static int append_string(mqtt_message_t *message, const char *string, int len)
{
    if (message->length + len + 2 > message->buffer_length) {
        return -1;
    }

    message->buffer[message->length++] = len >> 8;
    message->buffer[message->length++] = len & 0xff;
    memcpy(message->buffer + message->length, string, len);
    message->length += len;
    return len + 2;
}

static uint16_t append_message_id(mqtt_message_t *message, uint16_t message_id)
{
    // If message_id is zero then we should assign one, otherwise
    // we'll use the one supplied by the caller
    while (message_id == 0) {
#if MQTT_MSG_ID_INCREMENTAL
        message_id = ++message->last_message_id;
#else
        message_id = platform_random(65535);
#endif
    }

    if (message->length + 2 > message->buffer_length) {
        return 0;
    }

    message->buffer[message->length++] = message_id >> 8;
    message->buffer[message->length++] = message_id & 0xff;
    return message_id;
}

static int set_message_header_size(mqtt_message_t *message)
{
    message->length = MQTT_MAX_FIXED_HEADER_SIZE;
    return MQTT_MAX_FIXED_HEADER_SIZE;
}

static mqtt_message_t *fail_message(mqtt_message_t *message)
{
    message->data = message->buffer;
    message->length = 0;
    return message;
}

static mqtt_message_t *fini_message(mqtt_message_t *message, int type, int dup, int qos, int retain)
{
    int message_length = message->length - MQTT_MAX_FIXED_HEADER_SIZE;
    int total_length = message_length;
    int encoded_length = 0;
    uint8_t encoded_lens[4] = {0};

    // Check if we have fragmented message and update total_len
    if (message->fragmented_msg_total_length) {
        total_length = message->fragmented_msg_total_length - MQTT_MAX_FIXED_HEADER_SIZE;
    }

    // Encode MQTT message length
    int len_bytes = 0; // size of encoded message length

    do {
        encoded_length = total_length % 128;
        total_length /= 128;

        if (total_length > 0) {
            encoded_length |= 0x80;
        }

        encoded_lens[len_bytes] = encoded_length;
        len_bytes++;
    } while (total_length > 0);

    // Sanity check for MQTT header
    if (len_bytes + 1 > MQTT_MAX_FIXED_HEADER_SIZE) {
        return fail_message(message);
    }

    // Save the header bytes
    message->length = message_length + len_bytes + 1; // msg len + encoded_size len + type (1 byte)
    int offs = MQTT_MAX_FIXED_HEADER_SIZE - 1 - len_bytes;
    message->data = message->buffer + offs;
    message->fragmented_msg_data_offset -= offs;
    // type byte
    message->buffer[offs++] = ((type & 0x0f) << 4) | ((dup & 1) << 3) | ((qos & 3) << 1) | (retain & 1);

    // length bytes
    for (int j = 0; j < len_bytes; j++) {
        message->buffer[offs++] = encoded_lens[j];
    }

    return message;
}

size_t mqtt_get_total_length(const uint8_t *buffer, size_t length, int *fixed_size_len)
{
    int i;
    size_t totlen = 0;

    for (i = 1; i < length && i <= 4; ++i) {
        totlen += (size_t)(buffer[i] & 0x7f) << (7 * (i - 1));

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

bool mqtt_header_complete(uint8_t *buffer, size_t buffer_length)
{
    uint16_t i;
    uint16_t topiclen;

    for (i = 1; i < MQTT_MAX_FIXED_HEADER_SIZE; ++i) {
        if (i >= buffer_length) {
            return false;
        }

        if ((buffer[i] & 0x80) == 0) {
            ++i;
            break;
        }
    }

    // i is now the length of the fixed header

    if (i + 2 >= buffer_length) {
        return false;
    }

    topiclen = buffer[i++] << 8;
    topiclen |= buffer[i++];
    i += topiclen;

    if (mqtt_get_qos(buffer) > 0) {
        i += 2;
    }

    // i is now the length of the fixed + variable header
    return buffer_length >= i;
}

char *mqtt_get_publish_topic(uint8_t *buffer, size_t *length)
{
    int i;
    int topiclen;

    for (i = 1; i < *length; ++i) {
        if ((buffer[i] & 0x80) == 0) {
            ++i;
            break;
        }
    }

    if (i + 2 >= *length) {
        return NULL;
    }

    topiclen = buffer[i++] << 8;
    topiclen |= buffer[i++];

    if (i + topiclen > *length) {
        return NULL;
    }

    *length = topiclen;
    return (char *)(buffer + i);
}

char *mqtt_get_publish_data(uint8_t *buffer, size_t *length)
{
    int i;
    int totlen = 0;
    int topiclen;
    int blength = *length;
    *length = 0;

    for (i = 1; i < blength && i <= 4; ++i) {
        totlen += (buffer[i] & 0x7f) << (7 * (i - 1));

        if ((buffer[i] & 0x80) == 0) {
            ++i;
            break;
        }
    }

    totlen += i;

    if (i + 2 >= blength) {
        return NULL;
    }

    topiclen = buffer[i++] << 8;
    topiclen |= buffer[i++];

    if (i + topiclen >= blength) {
        return NULL;
    }

    i += topiclen;

    if (mqtt_get_qos(buffer) > 0) {
        if (i + 2 >= blength) {
            return NULL;
        }

        i += 2;
    }

    if (totlen < i) {
        return NULL;
    }

    if (totlen <= blength) {
        *length = totlen - i;
    } else {
        *length = blength - i;
    }

    return (char *)(buffer + i);
}

char *mqtt_get_suback_data(uint8_t *buffer, size_t *length)
{
    // SUBACK payload length = total length - (fixed header (2 bytes) + variable header (2 bytes))
    // This requires the remaining length to be encoded in 1 byte.
    if (*length > 4) {
        *length -= 4;
        return (char *)(buffer + 4);
    }

    *length = 0;
    return NULL;
}

uint16_t mqtt_get_id(uint8_t *buffer, size_t length)
{
    if (length < 1) {
        return 0;
    }

    switch (mqtt_get_type(buffer)) {
    case MQTT_MSG_TYPE_PUBLISH: {
        int i;
        int topiclen;

        for (i = 1; i < length; ++i) {
            if ((buffer[i] & 0x80) == 0) {
                ++i;
                break;
            }
        }

        if (i + 2 >= length) {
            return 0;
        }

        topiclen = buffer[i++] << 8;
        topiclen |= buffer[i++];

        if (i + topiclen > length) {
            return 0;
        }

        i += topiclen;

        if (mqtt_get_qos(buffer) > 0) {
            if (i + 2 > length) {
                return 0;
            }

            //i += 2;
        } else {
            return 0;
        }

        return (buffer[i] << 8) | buffer[i + 1];
    }

    case MQTT_MSG_TYPE_PUBACK:
    case MQTT_MSG_TYPE_PUBREC:
    case MQTT_MSG_TYPE_PUBREL:
    case MQTT_MSG_TYPE_PUBCOMP:
    case MQTT_MSG_TYPE_SUBACK:
    case MQTT_MSG_TYPE_UNSUBACK:
    case MQTT_MSG_TYPE_SUBSCRIBE:
    case MQTT_MSG_TYPE_UNSUBSCRIBE: {
        // This requires the remaining length to be encoded in 1 byte,
        // which it should be.
        if (length >= 4 && (buffer[1] & 0x80) == 0) {
            return (buffer[2] << 8) | buffer[3];
        } else {
            return 0;
        }
    }

    default:
        return 0;
    }
}

mqtt_message_t *mqtt_msg_connect(mqtt_message_t *message, mqtt_connect_info_t *info)
{
    set_message_header_size(message);
    int header_len;

    if (info->protocol_ver == MQTT_PROTOCOL_V_3_1) {
        header_len = MQTT_3_1_VARIABLE_HEADER_SIZE;
    } else {
        header_len = MQTT_3_1_1_VARIABLE_HEADER_SIZE;
    }

    if (message->length + header_len > message->buffer_length) {
        return fail_message(message);
    }

    char *variable_header = (char *)(message->buffer + message->length);
    message->length += header_len;
    int header_idx = 0;
    variable_header[header_idx++] = 0;                              // Variable header length MSB

    if (info->protocol_ver == MQTT_PROTOCOL_V_3_1) {
        variable_header[header_idx++] = 6;                          // Variable header length LSB
        memcpy(&variable_header[header_idx], "MQIsdp", 6);          // Protocol name
        header_idx = header_idx + 6;
        variable_header[header_idx++] = 3;                          // Protocol version
    } else {
        /* Defaults to protocol version 3.1.1 values */
        variable_header[header_idx++] = 4;                          // Variable header length LSB
        memcpy(&variable_header[header_idx], "MQTT", 4);            // Protocol name
        header_idx = header_idx + 4;
        variable_header[header_idx++] = 4;                          // Protocol version
    }

    int flags_offset = header_idx;
    variable_header[header_idx++] = 0;                              // Flags
    variable_header[header_idx++] = info->keepalive >> 8;           // Keep-alive MSB
    variable_header[header_idx] = info->keepalive & 0xff;         // Keep-alive LSB

    if (info->clean_session) {
        variable_header[flags_offset] |= MQTT_CONNECT_FLAG_CLEAN_SESSION;
    }

    if (info->client_id != NULL && info->client_id[0] != '\0') {
        if (append_string(message, info->client_id, strlen(info->client_id)) < 0) {
            return fail_message(message);
        }
    } else {
        if (append_string(message, "", 0) < 0) {
            return fail_message(message);
        }
    }

    if (info->will_topic != NULL && info->will_topic[0] != '\0') {
        if (append_string(message, info->will_topic, strlen(info->will_topic)) < 0) {
            return fail_message(message);
        }

        if (append_string(message, info->will_message, info->will_length) < 0) {
            return fail_message(message);
        }

        variable_header[flags_offset] |= MQTT_CONNECT_FLAG_WILL;

        if (info->will_retain) {
            variable_header[flags_offset] |= MQTT_CONNECT_FLAG_WILL_RETAIN;
        }

        variable_header[flags_offset] |= (info->will_qos & 3) << 3;
    }

    if (info->username != NULL && info->username[0] != '\0') {
        if (append_string(message, info->username, strlen(info->username)) < 0) {
            return fail_message(message);
        }

        variable_header[flags_offset] |= MQTT_CONNECT_FLAG_USERNAME;
    }

    if (info->password != NULL && info->password[0] != '\0') {
        if (info->username == NULL || info->username[0] == '\0') {
            /* In case if password is set without username, we need to set a zero length username.
             * (otherwise we violate: MQTT-3.1.2-22: If the User Name Flag is set to 0 then the Password Flag MUST be set to 0.)
             */
            if (append_string(message, "", 0) < 0) {
                return fail_message(message);
            }

            variable_header[flags_offset] |= MQTT_CONNECT_FLAG_USERNAME;
        }

        if (append_string(message, info->password, strlen(info->password)) < 0) {
            return fail_message(message);
        }

        variable_header[flags_offset] |= MQTT_CONNECT_FLAG_PASSWORD;
    }

    return fini_message(message, MQTT_MSG_TYPE_CONNECT, 0, 0, 0);
}

mqtt_message_t *mqtt_msg_publish(mqtt_message_t *message, const char *topic, const char *data, int data_length,
                                 int qos, int retain, uint16_t *message_id)
{
    set_message_header_size(message);

    if (topic == NULL || topic[0] == '\0') {
        return fail_message(message);
    }

    if (append_string(message, topic, strlen(topic)) < 0) {
        return fail_message(message);
    }

    if (data == NULL && data_length > 0) {
        return fail_message(message);
    }

    if (qos > 0) {
        if ((*message_id = append_message_id(message, 0)) == 0) {
            return fail_message(message);
        }
    } else {
        *message_id = 0;
    }

    if (data != NULL) {
        if (message->length + data_length > message->buffer_length) {
            // Not enough size in buffer -> fragment this message
            message->fragmented_msg_data_offset = message->length;
            memcpy(message->buffer + message->length, data,
                   message->buffer_length - message->length);
            message->length = message->buffer_length;
            message->fragmented_msg_total_length = data_length +
                                                   message->fragmented_msg_data_offset;
        } else {
            memcpy(message->buffer + message->length, data, data_length);
            message->length += data_length;
            message->fragmented_msg_total_length = 0;
        }
    }

    return fini_message(message, MQTT_MSG_TYPE_PUBLISH, 0, qos, retain);
}

mqtt_message_t *mqtt_msg_puback(mqtt_message_t *message, uint16_t message_id)
{
    set_message_header_size(message);

    if (append_message_id(message, message_id) == 0) {
        return fail_message(message);
    }

    return fini_message(message, MQTT_MSG_TYPE_PUBACK, 0, 0, 0);
}

mqtt_message_t *mqtt_msg_pubrec(mqtt_message_t *message, uint16_t message_id)
{
    set_message_header_size(message);

    if (append_message_id(message, message_id) == 0) {
        return fail_message(message);
    }

    return fini_message(message, MQTT_MSG_TYPE_PUBREC, 0, 0, 0);
}

mqtt_message_t *mqtt_msg_pubrel(mqtt_message_t *message, uint16_t message_id)
{
    set_message_header_size(message);

    if (append_message_id(message, message_id) == 0) {
        return fail_message(message);
    }

    return fini_message(message, MQTT_MSG_TYPE_PUBREL, 0, 1, 0);
}

mqtt_message_t *mqtt_msg_pubcomp(mqtt_message_t *message, uint16_t message_id)
{
    set_message_header_size(message);

    if (append_message_id(message, message_id) == 0) {
        return fail_message(message);
    }

    return fini_message(message, MQTT_MSG_TYPE_PUBCOMP, 0, 0, 0);
}

mqtt_message_t *mqtt_msg_subscribe(mqtt_message_t *message, const esp_mqtt_topic_t topic_list[], int size,
                                   uint16_t *message_id)
{
    set_message_header_size(message);

    if ((*message_id = append_message_id(message, 0)) == 0) {
        return fail_message(message);
    }

    for (int topic_number = 0; topic_number < size; ++topic_number) {
        if (topic_list[topic_number].filter[0] == '\0') {
            return fail_message(message);
        }

        if (append_string(message, topic_list[topic_number].filter, strlen(topic_list[topic_number].filter)) < 0) {
            return fail_message(message);
        }

        if (message->length + 1 > message->buffer_length) {
            return fail_message(message);
        }

        message->buffer[message->length] = topic_list[topic_number].qos;
        message->length ++;
    }

    return fini_message(message, MQTT_MSG_TYPE_SUBSCRIBE, 0, 1, 0);
}

mqtt_message_t *mqtt_msg_unsubscribe(mqtt_message_t *message, const char *topic, uint16_t *message_id)
{
    set_message_header_size(message);

    if (topic == NULL || topic[0] == '\0') {
        return fail_message(message);
    }

    if ((*message_id = append_message_id(message, 0)) == 0) {
        return fail_message(message);
    }

    if (append_string(message, topic, strlen(topic)) < 0) {
        return fail_message(message);
    }

    return fini_message(message, MQTT_MSG_TYPE_UNSUBSCRIBE, 0, 1, 0);
}

mqtt_message_t *mqtt_msg_pingreq(mqtt_message_t *message)
{
    set_message_header_size(message);
    return fini_message(message, MQTT_MSG_TYPE_PINGREQ, 0, 0, 0);
}

mqtt_message_t *mqtt_msg_pingresp(mqtt_message_t *message)
{
    set_message_header_size(message);
    return fini_message(message, MQTT_MSG_TYPE_PINGRESP, 0, 0, 0);
}

mqtt_message_t *mqtt_msg_disconnect(mqtt_message_t *message)
{
    set_message_header_size(message);
    return fini_message(message, MQTT_MSG_TYPE_DISCONNECT, 0, 0, 0);
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

static esp_err_t msg_buffer_init(mqtt_message_t *message, size_t buffer_size, uint32_t caps)
{
    memset(message, 0, sizeof(mqtt_message_t));
    message->buffer = (uint8_t *)heap_caps_calloc(buffer_size, sizeof(uint8_t), caps);

    if (!message->buffer) {
        return ESP_ERR_NO_MEM;
    }

    message->data = message->buffer;
    message->buffer_length = buffer_size;
    return ESP_OK;
}

esp_err_t mqtt_msg_buffer_init(mqtt_message_t *message, size_t buffer_size)
{
    return msg_buffer_init(message, buffer_size, MQTT_BUFFER_MEMORY);
}

void mqtt_msg_buffer_destroy(mqtt_message_t *message)
{
    if (message) {
        free(message->buffer);
    }
}

/* Messages created here are owned by the outbox, hence the outbox memory capabilities. */
mqtt_message_t *mqtt_msg_create(size_t buffer_size)
{
    mqtt_message_t *message = heap_caps_calloc(1, sizeof(mqtt_message_t), MQTT_OUTBOX_MEMORY);

    if (!message) {
        return NULL;
    }

    if (msg_buffer_init(message, buffer_size, MQTT_OUTBOX_MEMORY) != ESP_OK) {
        free(message);
        return NULL;
    }

    return message;
}

void mqtt_msg_destroy(mqtt_message_t *message)
{
    if (message) {
        mqtt_msg_buffer_destroy(message);
        free(message);
    }
}

/* `data` may be offset from `buffer` start to leave room for the fixed header, so the copy is
   placed at `data_offset` and the payload is taken from `src->data`, not from `src->buffer`. */
static esp_err_t msg_copy_at_offset(mqtt_message_t *message, const mqtt_message_t *src, size_t data_offset)
{
    if (message->buffer_length < data_offset + src->length) {
        return ESP_ERR_INVALID_SIZE;
    }

    message->length = src->length;
    message->fragmented_msg_data_offset = src->fragmented_msg_data_offset;
    message->fragmented_msg_total_length = src->fragmented_msg_total_length;
    message->read_len = src->read_len;
    message->id = src->id;
    message->type = src->type;
    message->qos = src->qos;
    message->data = message->buffer + data_offset;
    memcpy(message->data, src->data, src->length);
    return ESP_OK;
}

mqtt_message_t *mqtt_msg_dup(const mqtt_message_t *message)
{
    if (!message || !message->buffer || !message->data) {
        return NULL;
    }

    mqtt_message_t *new_message = mqtt_msg_create(message->buffer_length);

    if (!new_message) {
        return NULL;
    }

    if (msg_copy_at_offset(new_message, message, message->data - message->buffer) != ESP_OK) {
        mqtt_msg_destroy(new_message);
        return NULL;
    }

    return new_message;
}

/* The copy is compacted: `data` starts at the beginning of the destination buffer, so only
   `src->length` bytes are required in it. */
esp_err_t mqtt_msg_copy(mqtt_message_t *message, const mqtt_message_t *src)
{
    if (!message || !src || !message->buffer || !src->data) {
        return ESP_ERR_INVALID_ARG;
    }

    return msg_copy_at_offset(message, src, 0);
}

esp_err_t mqtt_msg_append(mqtt_message_t *message, const uint8_t *data, size_t length)
{
    if (!message || !message->buffer || !message->data || !data) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t data_offset = message->data - message->buffer;

    if (data_offset + message->length + length > message->buffer_length) {
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(message->data + message->length, data, length);
    message->length += length;
    return ESP_OK;
}
