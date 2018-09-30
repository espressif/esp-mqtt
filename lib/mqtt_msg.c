/*
* Copyright (c) 2014, Stephen Robinson
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
#include <stdint.h>
#include <string.h>
#include "mqtt_msg.h"
#include "mqtt_config.h"
#include "platform.h"

#define MQTT_MAX_FIXED_HEADER_SIZE 3

enum mqtt_connect_flag
{
    MQTT_CONNECT_FLAG_USERNAME = 1 << 7,
    MQTT_CONNECT_FLAG_PASSWORD = 1 << 6,
    MQTT_CONNECT_FLAG_WILL_RETAIN = 1 << 5,
    MQTT_CONNECT_FLAG_WILL = 1 << 2,
    MQTT_CONNECT_FLAG_CLEAN_SESSION = 1 << 1
};

struct __attribute((__packed__)) mqtt_connect_variable_header
{
    uint8_t lengthMsb;
    uint8_t lengthLsb;
#if defined(MQTT_PROTOCOL_311)
    uint8_t magic[4];
#else
    uint8_t magic[6];
#endif
    uint8_t version;
    uint8_t flags;
    uint8_t keepaliveMsb;
    uint8_t keepaliveLsb;
};

static int append_string(mqtt_connection_t* connection, const char* string, int len)
{
    if (connection->message.length + len + 2 > connection->buffer_length)
        return -1;

    connection->buffer[connection->message.length++] = len >> 8;
    connection->buffer[connection->message.length++] = len & 0xff;
    memcpy(connection->buffer + connection->message.length, string, len);
    connection->message.length += len;

    return len + 2;
}

static uint16_t append_message_id(mqtt_connection_t* connection, uint16_t message_id)
{
    // If message_id is zero then we should assign one, otherwise
    // we'll use the one supplied by the caller
    while (message_id == 0) {
        message_id = platform_random(65535);
    }

    if (connection->message.length + 2 > connection->buffer_length)
        return 0;

    connection->buffer[connection->message.length++] = message_id >> 8;
    connection->buffer[connection->message.length++] = message_id & 0xff;

    return message_id;
}

/**
 * \brief Initialize an empty message
 *
 * \param[inout] connection Connection on which to build the message
 * \return int Length of the message created
 */
static int init_message(mqtt_connection_t* connection)
{
    connection->message.length = MQTT_MAX_FIXED_HEADER_SIZE;
    return MQTT_MAX_FIXED_HEADER_SIZE;
}

static mqtt_message_t* fail_message(mqtt_connection_t* connection)
{
    connection->message.data = connection->buffer;
    connection->message.length = 0;
    return &connection->message;
}

/**
 * \brief Finish to build a message from data in connection
 *
 * \param[inout] connection Connection on which to build the message
 * \param[in] type Type of MQTT message
 * \param[in] dup DUPlicate flag
 * \param[in] qos Quality of service
 * \param[in] retain Retain flag
 * \return mqtt_message_t* Pointer on the message completely built
 */
static mqtt_message_t* fini_message(mqtt_connection_t* connection, int type, int dup, int qos, int retain)
{
    int remaining_length = connection->message.length - MQTT_MAX_FIXED_HEADER_SIZE;

    // If the remaining length won't fit on one byte
    if (remaining_length > 127)
    {
        connection->buffer[0] = ((type & 0x0f) << 4) | ((dup & 1) << 3) | ((qos & 3) << 1) | (retain & 1);
        connection->buffer[1] = 0x80 | (remaining_length % 128);
        connection->buffer[2] = remaining_length / 128;
        connection->message.length = remaining_length + 3;
        connection->message.data = connection->buffer;
    }
    else
    {
        connection->buffer[1] = ((type & 0x0f) << 4) | ((dup & 1) << 3) | ((qos & 3) << 1) | (retain & 1);
        connection->buffer[2] = remaining_length;
        connection->message.length = remaining_length + 2;
        connection->message.data = connection->buffer + 1;
    }

    return &connection->message;
}

/**
 * \brief Initialize a message with data
 *
 * \param[inout] connection Connection on which to initialize the message
 * \param[in] buffer Buffer of bytes containing the MQTT message
 * \param[in] buffer_length Length of the MQTT message
 */
void mqtt_msg_init(mqtt_connection_t* connection, uint8_t* buffer, uint16_t buffer_length)
{
    memset(connection, 0, sizeof(mqtt_connection_t));
    connection->buffer = buffer;
    connection->buffer_length = buffer_length;
}

/**
 * \brief Get the total length of a MQTT message
 *
 * \param[in] buffer Buffer of bytes containing the MQTT message
 * \param[in] length Maximum length to read in the buffer
 * \return uint32_t Total length of the MQTT message
 *
 * MQTT messages must contain a "remaining length" info as part of the fixed header of MQTT messages. It starts at byte 2 and is coded using a variable length encoding. Length <= 127 bytes are coded on a single byte. In other case, each byte is composed as follows : MSB continuation bit | length (7 bits) LSB. The maximum number of bytes for remaining length is 4.
 *
 */
uint32_t mqtt_get_total_length(uint8_t* buffer, uint16_t length)
{
    int i;
    uint32_t totlen = 0;

    // Remaining length info starts at byte 2
    for (i = 1; i < length; ++i)
    {
        totlen += (buffer[i] & 0x7f) << (7 * (i - 1));
        // If there's no continuation bit, we can stop
        if ((buffer[i] & 0x80) == 0)
        {
            ++i;
            break;
        }
    }
    // Add to the total length the size of the fixed header (1 byte + length of remaining length info)
    totlen += i;

    return totlen;
}

/**
 * \brief Get the topic of the publish message
 *
 * \param[in] buffer Buffer of bytes containing the MQTT message
 * \param[inout] length Acts first as the total length of the message but modified to contain the topic's length
 * \return const char* String containing the topic
 *
 * A publish message contains : the fixed header (1 byte + variable number of bytes for "remaining length" info) then a variable header. The topic MUST be the first field of this variable header. As all strings in MQTT, it is UTF8 encoded and the format is the following : 2 bytes to describe the length then N bytes for the string, N being described in the first 2 bytes.
 *
 */
const char* mqtt_get_publish_topic(uint8_t* buffer, uint32_t* length)
{
    int i;
    int totlen = 0;
    int topiclen;

    // Go past the fixed header of the message
    for (i = 1; i < *length; ++i)
    {
        totlen += (buffer[i] & 0x7f) << (7 * (i - 1));
        if ((buffer[i] & 0x80) == 0)
        {
            ++i;
            break;
        }
    }
    totlen += i;

    // First field in variable header is the topic. As all strings, 2 bytes are used to described its length.
    if (i + 2 >= *length)
        return NULL;
    topiclen = buffer[i++] << 8;
    topiclen |= buffer[i++];

    // Stop if the remaining size of the buffer isn't big enough to contain the topic
    if (i + topiclen > *length)
        return NULL;

    *length = topiclen;
    return (const char*)(buffer + i);
}

/**
 * \brief Get the data of the publish message
 *
 * \param[in] buffer Buffer of bytes containing the MQTT message
 * \param[inout] length Acts first as total length of the message but modified to contain the data's length
 * \return const char* Pointer on the first byte of the data contained in the message
 */
const char* mqtt_get_publish_data(uint8_t* buffer, uint32_t* length)
{
    int i;
    int totlen = 0;
    int topiclen;
    int blength = *length;
    *length = 0;

    // Go past the fixed header of the message
    for (i = 1; i < blength; ++i)
    {
        totlen += (buffer[i] & 0x7f) << (7 * (i - 1));
        if ((buffer[i] & 0x80) == 0)
        {
            ++i;
            break;
        }
    }
    totlen += i;

    // Go past the topic (first field of variable header)
    if (i + 2 >= blength)
        return NULL;
    topiclen = buffer[i++] << 8;
    topiclen |= buffer[i++];

    if (i + topiclen >= blength)
        return NULL;

    i += topiclen;

    // Messages with quality of service greater than 0 have two bytes for the packet identifier
    if (mqtt_get_qos(buffer) > 0)
    {
        if (i + 2 >= blength)
            return NULL;
        i += 2;
    }

    if (totlen < i)
        return NULL;

    if (totlen <= blength)
        *length = totlen - i;
    else
        *length = blength - i;
    return (const char*)(buffer + i);
}

/**
 * \brief Get the packed identifier of a message
 *
 * \param buffer Buffer of bytes containing the MQTT message
 * \param length Total length of the message
 * \return uint16_t Packed identitifier of the message
 */
uint16_t mqtt_get_id(uint8_t* buffer, uint16_t length)
{
    if (length < 1)
        return 0;

    switch (mqtt_get_type(buffer))
    {
        // Fetching packet identifier in case of publish message is non trivial
        case MQTT_MSG_TYPE_PUBLISH:
            {
                int i;
                int topiclen;

                // Go past fixed header
                for (i = 1; i < length; ++i)
                {
                    if ((buffer[i] & 0x80) == 0)
                    {
                        ++i;
                        break;
                    }
                }

                // Go past the first field of the variable header (topic)
                if (i + 2 >= length)
                    return 0;
                topiclen = buffer[i++] << 8;
                topiclen |= buffer[i++];

                if (i + topiclen >= length)
                    return 0;
                i += topiclen;

                // Only publish messages with quality of service greater than 0 have a non-zero packet identifier
                if (mqtt_get_qos(buffer) > 0)
                {
                    if (i + 2 >= length)
                        return 0;
                } else {
                    return 0;
                }

                return (buffer[i] << 8) | buffer[i + 1];
            }

        // For all other types of messages containing a packet identifier, retrieval is trivial
        case MQTT_MSG_TYPE_PUBACK:
        case MQTT_MSG_TYPE_PUBREC:
        case MQTT_MSG_TYPE_PUBREL:
        case MQTT_MSG_TYPE_PUBCOMP:
        case MQTT_MSG_TYPE_SUBACK:
        case MQTT_MSG_TYPE_UNSUBACK:
        case MQTT_MSG_TYPE_SUBSCRIBE:
            {
                // This requires the remaining length to be encoded in 1 byte,
                // which it should be.
                if (length >= 4 && (buffer[1] & 0x80) == 0)
                    return (buffer[2] << 8) | buffer[3];
                else
                    return 0;
            }

        default:
            return 0;
    }
}

/**
 * \brief Build a CONNECT message
 *
 * \param[inout] connection Connection on which to build this message
 * \param[in] info Connection information (among others : will, keep alive...)
 * \return mqtt_message_t* Pointer on the message completely built
 */
mqtt_message_t* mqtt_msg_connect(mqtt_connection_t* connection, mqtt_connect_info_t* info)
{
    struct mqtt_connect_variable_header* variable_header;

    init_message(connection);

    if (connection->message.length + sizeof(*variable_header) > connection->buffer_length)
        return fail_message(connection);
    variable_header = (void*)(connection->buffer + connection->message.length);
    connection->message.length += sizeof(*variable_header);

    variable_header->lengthMsb = 0;
#if defined(CONFIG_MQTT_PROTOCOL_311)
    variable_header->lengthLsb = 4;
    memcpy(variable_header->magic, "MQTT", 4);
    variable_header->version = 4;
#else
    variable_header->lengthLsb = 6;
    memcpy(variable_header->magic, "MQIsdp", 6);
    variable_header->version = 3;
#endif

    variable_header->flags = 0;
    variable_header->keepaliveMsb = info->keepalive >> 8;
    variable_header->keepaliveLsb = info->keepalive & 0xff;

    if (info->clean_session)
        variable_header->flags |= MQTT_CONNECT_FLAG_CLEAN_SESSION;

    if (info->client_id != NULL && info->client_id[0] != '\0')
    {
        if (append_string(connection, info->client_id, strlen(info->client_id)) < 0)
            return fail_message(connection);
    }
    else
        return fail_message(connection);

    // Append the will if there's one specified
    if (info->will_topic != NULL && info->will_topic[0] != '\0')
    {
        if (append_string(connection, info->will_topic, strlen(info->will_topic)) < 0)
            return fail_message(connection);

        if (append_string(connection, info->will_message, info->will_length) < 0)
            return fail_message(connection);

        variable_header->flags |= MQTT_CONNECT_FLAG_WILL;
        if (info->will_retain)
            variable_header->flags |= MQTT_CONNECT_FLAG_WILL_RETAIN;
        variable_header->flags |= (info->will_qos & 3) << 3;
    }

    // Append username if there's one specified
    if (info->username != NULL && info->username[0] != '\0')
    {
        if (append_string(connection, info->username, strlen(info->username)) < 0)
            return fail_message(connection);

        variable_header->flags |= MQTT_CONNECT_FLAG_USERNAME;
    }

    // Append password if there's one specified
    if (info->password != NULL && info->password[0] != '\0')
    {
        if (append_string(connection, info->password, strlen(info->password)) < 0)
            return fail_message(connection);

        variable_header->flags |= MQTT_CONNECT_FLAG_PASSWORD;
    }

    // Finish to build the message and return the pointer on it
    return fini_message(connection, MQTT_MSG_TYPE_CONNECT, 0, 0, 0);
}

/**
 * \brief Build a publish a message on the specified topic with the specified quality of service
 *
 * \param[inout] connection Connection on which to build the message
 * \param[in] topic The topic to publish on
 * \param[in] data Data to be published
 * \param[in] data_length The data length, in bytes
 * \param[in] qos The quality of service for this publish
 * \param[in] retain Retain flag
 * \param[out] message_id ID of the message generated
 * \return mqtt_message_t*
 */
mqtt_message_t* mqtt_msg_publish(mqtt_connection_t* connection, const char* topic, const char* data, int data_length, int qos, int retain, uint16_t* message_id)
{
    init_message(connection);

    // Append topic to the connection if everything's right with it
    if (topic == NULL || topic[0] == '\0')
        return fail_message(connection);
    if (append_string(connection, topic, strlen(topic)) < 0)
        return fail_message(connection);

    // For QoS > 0, a message ID is necessary for acknowledgement process
    if (qos > 0)
    {
        if ((*message_id = append_message_id(connection, 0)) == 0)
            return fail_message(connection);
    }
    else
        *message_id = 0;

    // Append message if enough space left in buffer
    if (connection->message.length + data_length > connection->buffer_length)
        return fail_message(connection);
    memcpy(connection->buffer + connection->message.length, data, data_length);
    connection->message.length += data_length;

    return fini_message(connection, MQTT_MSG_TYPE_PUBLISH, 0, qos, retain);
}

/**
 * \brief Build the "PUBlish ACKnowledgement" message on the current connection
 *
 * \param[inout] connection Connection on which to build the message
 * \param[in] message_id ID of the message of the acknowledgement sequence
 * \return mqtt_message_t* The pointer on the message built
 *
 * A PUBACK packet is the response to a PUBLISH packet with QoS level 1.
 *
 */
mqtt_message_t* mqtt_msg_puback(mqtt_connection_t* connection, uint16_t message_id)
{
    init_message(connection);
    if (append_message_id(connection, message_id) == 0)
        return fail_message(connection);
    return fini_message(connection, MQTT_MSG_TYPE_PUBACK, 0, 0, 0);
}

/**
 * \brief Build the "PUBlish RECeived" message on the current connection
 *
 * \param[inout] connection Connection on which to build the message
 * \param[in] message_id ID of the message of the acknowledgement sequence
 * \return mqtt_message_t* The pointer on the message built
 *
 * A PUBREC packet is the response to a PUBLISH packet with QoS 2. It is the second packet of the QoS 2 protocol exchange.
 *
 */
mqtt_message_t* mqtt_msg_pubrec(mqtt_connection_t* connection, uint16_t message_id)
{
    init_message(connection);
    if (append_message_id(connection, message_id) == 0)
        return fail_message(connection);
    return fini_message(connection, MQTT_MSG_TYPE_PUBREC, 0, 0, 0);
}

/**
 * \brief Build the "PUBlish RELease" message on the current connection
 *
 * \param[inout] connection Connection on which to build the message
 * \param[in] message_id ID of the message of the acknowledgement sequence
 * \return mqtt_message_t* The pointer on the message built
 *
 * A PUBREL packet is the response to a PUBREC packet. It is the third packet of the QoS 2 protocol exchange.
 *
 */
mqtt_message_t* mqtt_msg_pubrel(mqtt_connection_t* connection, uint16_t message_id)
{
    init_message(connection);
    if (append_message_id(connection, message_id) == 0)
        return fail_message(connection);
    return fini_message(connection, MQTT_MSG_TYPE_PUBREL, 0, 1, 0);
}

/**
 * \brief Build the "PUBlish COMPlete" message on the current connection
 *
 * \param[inout] connection Connection on which to build the message
 * \param[in] message_id ID of the message of the acknowledgement sequence
 * \return mqtt_message_t* The pointer on the message built
 *
 * The PUBCOMP packet is the response to a PUBREL packet. It is the fourth and final packet of the QoS 2 protocol exchange.
 *
 */
mqtt_message_t* mqtt_msg_pubcomp(mqtt_connection_t* connection, uint16_t message_id)
{
    init_message(connection);
    if (append_message_id(connection, message_id) == 0)
        return fail_message(connection);
    return fini_message(connection, MQTT_MSG_TYPE_PUBCOMP, 0, 0, 0);
}

/**
 * \brief Build a message to subscribe tp the specified topic
 *
 * \param[inout] connection Connection on whioh to build the message
 * \param[in] topic The topic to subscribe to
 * \param[in] qos The Quality of Service wanted for this subscription
 * \param[out] message_id ID of the message generated
 * \return mqtt_message_t* The pointer on the message built
 */
mqtt_message_t* mqtt_msg_subscribe(mqtt_connection_t* connection, const char* topic, int qos, uint16_t* message_id)
{
    init_message(connection);

    if (topic == NULL || topic[0] == '\0')
        return fail_message(connection);

    if ((*message_id = append_message_id(connection, 0)) == 0)
        return fail_message(connection);

    if (append_string(connection, topic, strlen(topic)) < 0)
        return fail_message(connection);

    if (connection->message.length + 1 > connection->buffer_length)
        return fail_message(connection);
    connection->buffer[connection->message.length++] = qos;

    return fini_message(connection, MQTT_MSG_TYPE_SUBSCRIBE, 0, 1, 0);
}

/**
 * \brief Build a message to unsubscribe from the specified topic
 *
 * \param[inout] connection Connection on which to build the message
 * \param[in] topic The topic to unsubscribe from
 * \param[out] message_id ID of the message generated
 * \return mqtt_message_t* The pointer on the message built
 */
mqtt_message_t* mqtt_msg_unsubscribe(mqtt_connection_t* connection, const char* topic, uint16_t* message_id)
{
    init_message(connection);

    if (topic == NULL || topic[0] == '\0')
        return fail_message(connection);

    if ((*message_id = append_message_id(connection, 0)) == 0)
        return fail_message(connection);

    if (append_string(connection, topic, strlen(topic)) < 0)
        return fail_message(connection);

    return fini_message(connection, MQTT_MSG_TYPE_UNSUBSCRIBE, 0, 1, 0);
}

/**
 * \brief Build the "PING request" message on the current connection
 *
 * \param[inout] connection Connection on which to build the message
 * \return mqtt_message_t* The pointer on the message built
 *
 * The PINGREQ packet is sent from a client to the server. It can be used to:
 *  - Indicate to the server that the client is alive in the absence of any other Control packets being sent from the client to the server.
 *  - Request that the server responds to confirm that it is alive.
 *  - Exercise the network to indicate that the network connection is active.
 *
 * This packet is used in Keep Alive processing.
 *
 */
mqtt_message_t* mqtt_msg_pingreq(mqtt_connection_t* connection)
{
    init_message(connection);
    return fini_message(connection, MQTT_MSG_TYPE_PINGREQ, 0, 0, 0);
}

/**
 * \brief Build the "PING response" message on the current connection
 *
 * \param[inout] connection Connection on which to build the message
 * \return mqtt_message_t* The pointer on the message built
 *
 * A PINGRESP packet is sent by the server to the client in response to a PINGREQ packet. It indicates that the server is alive.
 *
 */
mqtt_message_t* mqtt_msg_pingresp(mqtt_connection_t* connection)
{
    init_message(connection);
    return fini_message(connection, MQTT_MSG_TYPE_PINGRESP, 0, 0, 0);
}

/**
 * \brief Build the "DISCONNECT" message on the current connection
 *
 * \param[inout] connection Connection on which to build the message
 * \return mqtt_message_t* The pointer on the message built
 *
 * The DISCONNECT packet is the final control packet sent from the client to the server. It indicates that the client is disconnecting cleanly.
 *
 */
mqtt_message_t* mqtt_msg_disconnect(mqtt_connection_t* connection)
{
    init_message(connection);
    return fini_message(connection, MQTT_MSG_TYPE_DISCONNECT, 0, 0, 0);
}
