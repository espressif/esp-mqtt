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
#include <string.h>
#include "mqtt_msg.h"
#include "mqtt_config.h"
#include "platform.h"

#define MQTT_MAX_NB_BYTES_REMAINING_LENGTH 4
#define MQTT_MAX_FIXED_HEADER_SIZE (1 + MQTT_MAX_NB_BYTES_REMAINING_LENGTH)
#define MQTT_NB_BYTES_SIZE_STRING 2

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

/**
 * \brief Append a string to the message
 *
 * \param connection Connection on which to add the string
 * \param string String to add
 * \param len Length of the string to add
 * \return int32_t Length added, including the header
 *
 * A string is always prefixed by its length, on 2 bytes. Therefore, the length is expressed on 2 bytes. Since we return -1 in case of error and the size of the prefix is included in the length returned in case of success, we have to return an int32_t to handle all cases. Note that this function is also used for the will message that is not a string per se, but has the same requirements.
 *
 */
static int32_t append_string(mqtt_connection_t* connection, const char* string, uint16_t len)
{
    if (connection->message.length + len + MQTT_NB_BYTES_SIZE_STRING > connection->buffer_length)
        return -1;

    connection->buffer[connection->message.length++] = len >> 8;
    connection->buffer[connection->message.length++] = len & 0xff;
    memcpy(connection->buffer + connection->message.length, string, len);
    connection->message.length += len;

    return len + MQTT_NB_BYTES_SIZE_STRING;
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
 * \brief Get the remaining length information
 *
 * \param buffer Buffer of bytes containing the MQTT message
 * \return uint32_t The remaining length
 */
static uint32_t get_remaining_length(uint8_t* buffer, uint32_t length){
    uint32_t remaining_length = 0 ;

    // Remaining length info starts at byte 2
    for (uint8_t i_byte = 1; i_byte < length; ++i_byte) {
        remaining_length += (buffer[i_byte] & 0x7f) << (7 * (i_byte - 1));
        // If there's no continuation bit, we can stop
        if ((buffer[i_byte] & 0x80) == 0){
            break;
        }
    }

    return remaining_length ;
}

/**
 * \brief Get the number of bytes on which is coded the remaining length information
 *
 * \param remaining_length The remaining length
 * \return uint8_t The number of bytes it takes to encode the remaining length
 *
 * Remaining length is coded using a variable length encoding. It cannot contain more than 4 bytes. Each byte contains as its MSB a continuation bit. If set to 1, it means the next byte also contain a part of the remaining length information. Due to this continuation bit, the length is coded in a base 128. First limit is 128-1, second limit (128*128)-1 and so on.
 *
 */
static uint8_t get_nb_bytes_remaining_length(uint32_t remaining_length) {
    if ( remaining_length <= 127) {
        return 1 ;
    } else if ( remaining_length <= 16383 ) {
        return 2 ;
    } else if ( remaining_length <= 2097151 ) {
        return 3 ;
    } else if ( remaining_length <= 268435455 ) {
        return 4 ;
    } else {
        return 0 ;
    }
}


static uint8_t get_fixed_header_size(uint8_t* buffer, uint32_t length) {
    uint8_t nb_bytes_remaining_length = get_nb_bytes_remaining_length(get_remaining_length(buffer, length)) ;
    if ( nb_bytes_remaining_length == 0) {
        return 0 ;
    } else {
        return nb_bytes_remaining_length + 1 ;
    }
}


/**
 * \brief Initialize an empty message
 *
 * \param[inout] connection Connection on which to build the message
 * \return int Length of the message created
 */
static uint32_t init_message(mqtt_connection_t* connection)
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
 *
 * Due to the variable number of bytes to encode the remaining length information, the buffer was initialized with the maximum size of the fixed header in mind. Which means the rest of the message has been put at the position MQTT_MAX_FIXED_HEADER_SIZE in the buffer. So, for example, for a remaining length of 112 bytes, the fixed header will have two bytes : one for the flags and one for remaining length. This data must be written at position MQTT_MAX_FIXED_HEADER_SIZE - 1 and MQTT_MAX_FIXED_HEADER_SIZE - 2. This offset is computed to put the fixed header at the appropriate position and indicate the "true" start position of the message data.
 *
 */
static mqtt_message_t* fini_message(mqtt_connection_t* connection, int type, int dup, int qos, int retain)
{
    uint32_t remaining_length = connection->message.length - MQTT_MAX_FIXED_HEADER_SIZE;
    int8_t nb_bytes_remaining_length = get_nb_bytes_remaining_length(remaining_length) ;

    if ( nb_bytes_remaining_length == 0 ) {
        return fail_message(connection) ;
    } else {
        // Set the length taking into account the fixed header
        connection->message.length = 1 + nb_bytes_remaining_length + remaining_length ;

        // First byte contains all the flags
        int offset_buffer = MQTT_MAX_NB_BYTES_REMAINING_LENGTH - nb_bytes_remaining_length ;
        connection->buffer[offset_buffer] = ((type & 0x0f) << 4) | ((dup & 1) << 3) | ((qos & 3) << 1) | (retain & 1);

        // Build the remaining length bytes
        uint32_t divider = 1 ;
        for ( int8_t i_byte_remaining_length = 1 ; i_byte_remaining_length <= nb_bytes_remaining_length ; i_byte_remaining_length++ ) {
            connection->buffer[offset_buffer + i_byte_remaining_length] = (remaining_length / divider) % 128 ;
            if ( i_byte_remaining_length != nb_bytes_remaining_length) {
                connection->buffer[offset_buffer + i_byte_remaining_length] |= 0x80 ;
                divider *= 128 ;
            }
        }

        // Set the pointer for the data to the right position
        connection->message.data = connection->buffer + offset_buffer ;
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
void mqtt_msg_init(mqtt_connection_t* connection, uint8_t* buffer, uint32_t buffer_length)
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
uint32_t mqtt_get_total_length(uint8_t* buffer, uint32_t length)
{
    uint32_t total_length = 1;
    uint32_t remaining_length = get_remaining_length(buffer, length) ;
    uint8_t nb_bytes_remaining_length = get_nb_bytes_remaining_length(remaining_length) ;

    if ( nb_bytes_remaining_length == 0 ){
        return 0 ;
    } else {
        total_length += remaining_length + nb_bytes_remaining_length ;
        return total_length ;
    }
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
    uint32_t i_byte;
    uint16_t topic_length;

    // Go past the fixed header of the message
    i_byte = get_fixed_header_size(buffer, *length) ;
    if ( i_byte == 0 ) {
        return NULL ;
    }

    // First field in variable header is the topic. As all strings, 2 bytes are used to described its length.
    if (i_byte + MQTT_NB_BYTES_SIZE_STRING >= *length)
        return NULL;
    topic_length = buffer[i_byte++] << 8;
    topic_length |= buffer[i_byte++];

    // Stop if the remaining size of the buffer isn't big enough to contain the topic
    if (i_byte + topic_length > *length)
        return NULL;

    *length = topic_length;
    return (const char*)(buffer + i_byte);
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
    uint32_t i_byte;
    uint32_t totlen = 0;
    uint16_t topiclen;
    uint32_t buffer_length = *length;
    *length = 0;

    // Go past the fixed header of the message
    i_byte = get_fixed_header_size(buffer, buffer_length) ;
    if ( i_byte == 0 ) {
        return NULL ;
    }
    totlen = mqtt_get_total_length(buffer, buffer_length) ;

    // Go past the topic (first field of variable header)
    if (i_byte + MQTT_NB_BYTES_SIZE_STRING >= buffer_length)
        return NULL;
    topiclen = buffer[i_byte++] << 8;
    topiclen |= buffer[i_byte++];

    if (i_byte + topiclen >= buffer_length)
        return NULL;

    i_byte += topiclen;

    // Messages with quality of service greater than 0 have two bytes for the packet identifier
    if (mqtt_get_qos(buffer) > 0)
    {
        if (i_byte + 2 >= buffer_length)
            return NULL;
        i_byte += 2;
    }

    if (totlen < i_byte)
        return NULL;

    if (totlen <= buffer_length)
        *length = totlen - i_byte;
    else
        *length = buffer_length - i_byte;
    return (const char*)(buffer + i_byte);
}

/**
 * \brief Get the packed identifier of a message
 *
 * \param buffer Buffer of bytes containing the MQTT message
 * \param length Total length of the message
 * \return uint16_t Packed identitifier of the message
 */
uint16_t mqtt_get_id(uint8_t* buffer, uint32_t length)
{
    if (length < 1)
        return 0;

    switch (mqtt_get_type(buffer))
    {
        // Fetching packet identifier in case of publish message is non trivial
        case MQTT_MSG_TYPE_PUBLISH:
            {
                uint32_t i;
                uint16_t topiclen;

                // Go past fixed header
                i = get_fixed_header_size(buffer, length) ;
                if ( i == 0 ) {
                    return 0 ;
                }

                // Go past the first field of the variable header (topic)
                if (i + MQTT_NB_BYTES_SIZE_STRING >= length)
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
mqtt_message_t* mqtt_msg_publish(mqtt_connection_t* connection, const char* topic, const char* data, uint32_t data_length, int qos, int retain, uint16_t* message_id)
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
