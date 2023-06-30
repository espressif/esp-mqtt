#include <string.h>
#include "mqtt5_msg.h"
#include "mqtt_client.h"
#include "mqtt_config.h"
#include "platform.h"
#include "esp_log.h"

#define MQTT5_MAX_FIXED_HEADER_SIZE 5

static const char *TAG = "mqtt5_msg";

#define APPEND_CHECK(a, ret)  if(a == -1) {                         \
        ESP_LOGE(TAG,"%s(%d) fail",__FUNCTION__, __LINE__);      \
        return (ret);                                           \
        }
#define MQTT5_SHARED_SUB "$share/%s/%s"
#define MQTT5_CONVERT_ONE_BYTE_TO_FOUR(i, a, b, c, d) i = (a << 24); \
                                                      i |= (b << 16); \
                                                      i |= (c << 8); \
                                                      i |= d;

#define MQTT5_CONVERT_ONE_BYTE_TO_TWO(i, a, b)        i = (a << 8); \
                                                      i |= b;

#define MQTT5_CONVERT_TWO_BYTE(i, a)                  i = (a >> 8) & 0xff; \
                                                      i = a & 0xff;

enum mqtt5_connect_flag {
    MQTT5_CONNECT_FLAG_USERNAME = 1 << 7,
    MQTT5_CONNECT_FLAG_PASSWORD = 1 << 6,
    MQTT5_CONNECT_FLAG_WILL_RETAIN = 1 << 5,
    MQTT5_CONNECT_FLAG_WILL = 1 << 2,
    MQTT5_CONNECT_FLAG_CLEAN_SESSION = 1 << 1
};

static void generate_variable_len(size_t len, uint8_t *len_bytes, uint8_t *encoded_lens)
{
    uint8_t bytes = 0;
    do {
        uint8_t i = len % 128;
        len /= 128;
        if (len > 0) {
            i |= 0x80;
        }
        encoded_lens[bytes ++] = i;
    } while (len > 0);
    *len_bytes = bytes;
}

static size_t get_variable_len(uint8_t *buffer, size_t offset, size_t buffer_length, uint8_t *len_bytes)
{
    *len_bytes = 0;
    size_t len = 0, i = 0;
    for (i = offset; i < buffer_length; i ++) {
        len += (buffer[i] & 0x7f) << (7 * (i - offset));
        if ((buffer[i] & 0x80) == 0) {
            i ++;
            break;
        }
    }
    *len_bytes = i - offset;
    return len;
}

static int update_property_len_value(mqtt_connection_t *connection, size_t property_len, int property_offset)
{
    uint8_t encoded_lens[4] = {0}, len_bytes = 0;
    size_t len = property_len, message_offset = property_offset + property_len;
    generate_variable_len(len, &len_bytes, encoded_lens);
    int offset = len_bytes - 1;

    connection->outbound_message.length += offset;
    if (connection->outbound_message.length > connection->buffer_length) {
        return -1;
    }

    if (offset > 0) {
        for (int i = 0; i < property_len; i ++) {
            connection->buffer[message_offset + offset] = connection->buffer[message_offset];
            message_offset --;
        }
    }

    for (int i = 0; i < len_bytes; i ++) {
        connection->buffer[property_offset ++] = encoded_lens[i];
    }
    return offset;
}

static int append_property(mqtt_connection_t *connection, uint8_t property_type, uint8_t len_occupy, const char *data, size_t data_len)
{
    if ((connection->outbound_message.length + len_occupy + (data ? data_len : 0) + (property_type ? 1 : 0)) > connection->buffer_length) {
        return -1;
    }

    size_t origin_message_len = connection->outbound_message.length;
    if (property_type) {
        connection->buffer[connection->outbound_message.length ++] = property_type;
    }

    if (len_occupy == 0) {
        uint8_t encoded_lens[4] = {0}, len_bytes = 0;
        generate_variable_len(data_len, &len_bytes, encoded_lens);
        for (int j = 0; j < len_bytes; j ++) {
            connection->buffer[connection->outbound_message.length ++] = encoded_lens[j];
        }
    } else {
        for (int i = 1; i <= len_occupy; i ++) {
            connection->buffer[connection->outbound_message.length ++] = (data_len >> (8 * (len_occupy - i))) & 0xff;
        }
    }

    if (data) {
        memcpy(connection->buffer + connection->outbound_message.length, data, data_len);
        connection->outbound_message.length += data_len;
    }

    return connection->outbound_message.length - origin_message_len;
}

static uint16_t append_message_id(mqtt_connection_t *connection, uint16_t message_id)
{
    // If message_id is zero then we should assign one, otherwise
    // we'll use the one supplied by the caller
    while (message_id == 0) {
#if MQTT_MSG_ID_INCREMENTAL
        message_id = ++ connection->last_message_id;
#else
        message_id = platform_random(65535);
#endif
    }

    if (connection->outbound_message.length + 2 > connection->buffer_length) {
        return 0;
    }

    MQTT5_CONVERT_TWO_BYTE(connection->buffer[connection->outbound_message.length ++], message_id)

    return message_id;
}

static int init_message(mqtt_connection_t *connection)
{
    connection->outbound_message.length = MQTT5_MAX_FIXED_HEADER_SIZE;
    return MQTT5_MAX_FIXED_HEADER_SIZE;
}

static mqtt_message_t *fail_message(mqtt_connection_t *connection)
{
    connection->outbound_message.data = connection->buffer;
    connection->outbound_message.length = 0;
    return &connection->outbound_message;
}

static mqtt_message_t *fini_message(mqtt_connection_t *connection, int type, int dup, int qos, int retain)
{
    int message_length = connection->outbound_message.length - MQTT5_MAX_FIXED_HEADER_SIZE;
    int total_length = message_length;
    uint8_t encoded_lens[4] = {0}, len_bytes = 0;
    // Check if we have fragmented message and update total_len
    if (connection->outbound_message.fragmented_msg_total_length) {
        total_length = connection->outbound_message.fragmented_msg_total_length - MQTT5_MAX_FIXED_HEADER_SIZE;
    }

    // Encode MQTT message length
    generate_variable_len(total_length, &len_bytes, encoded_lens);

    // Sanity check for MQTT header
    if (len_bytes + 1 > MQTT5_MAX_FIXED_HEADER_SIZE) {
        return fail_message(connection);
    }

    // Save the header bytes
    connection->outbound_message.length = message_length + len_bytes + 1; // msg len + encoded_size len + type (1 byte)
    int offs = MQTT5_MAX_FIXED_HEADER_SIZE - 1 - len_bytes;
    connection->outbound_message.data = connection->buffer + offs;
    connection->outbound_message.fragmented_msg_data_offset -= offs;
    // type byte
    connection->buffer[offs ++] =  ((type & 0x0f) << 4) | ((dup & 1) << 3) | ((qos & 3) << 1) | (retain & 1);
    // length bytes
    for (int j = 0; j < len_bytes; j ++) {
        connection->buffer[offs ++] = encoded_lens[j];
    }

    return &connection->outbound_message;
}

static esp_err_t mqtt5_msg_set_user_property(mqtt5_user_property_handle_t *user_property, char *key, size_t key_len, char *value, size_t value_len)
{
    if (!*user_property) {
        *user_property = calloc(1, sizeof(struct mqtt5_user_property_list_t));
        ESP_MEM_CHECK(TAG, *user_property, return ESP_FAIL);
        STAILQ_INIT(*user_property);
    }

    mqtt5_user_property_item_t user_property_item = calloc(1, sizeof(mqtt5_user_property_t));
    ESP_MEM_CHECK(TAG, user_property_item, return ESP_FAIL;);
    user_property_item->key = calloc(1, key_len + 1);
    ESP_MEM_CHECK(TAG, user_property_item->key, {
        free(user_property_item);
        return ESP_FAIL;
    });
    memcpy(user_property_item->key, key, key_len);
    user_property_item->key[key_len] = '\0';

    user_property_item->value = calloc(1, value_len + 1);
    ESP_MEM_CHECK(TAG, user_property_item->value, {
        free(user_property_item->key);
        free(user_property_item);
        return ESP_FAIL;
    });
    memcpy(user_property_item->value, value, value_len);
    user_property_item->value[value_len] = '\0';

    STAILQ_INSERT_TAIL(*user_property, user_property_item, next);
    return ESP_OK;
}

static mqtt5_user_property_handle_t mqtt5_msg_get_user_property(uint8_t *buffer, size_t buffer_length)
{
    mqtt5_user_property_handle_t user_porperty = NULL;
    uint8_t *property = buffer;
    uint16_t property_offset = 0, len = 0;
    while (property_offset < buffer_length) {
        uint8_t property_id = property[property_offset ++];
        switch (property_id) {
        case MQTT5_PROPERTY_REASON_STRING: //only print now
            MQTT5_CONVERT_ONE_BYTE_TO_TWO(len, property[property_offset ++], property[property_offset ++])
            ESP_LOGD(TAG, "MQTT5_PROPERTY_REASON_STRING %.*s", len, &property[property_offset]);
            property_offset += len;
            continue;
        case MQTT5_PROPERTY_USER_PROPERTY: {
            uint8_t *key = NULL, *value = NULL;
            size_t key_len = 0, value_len = 0;
            MQTT5_CONVERT_ONE_BYTE_TO_TWO(len, property[property_offset ++], property[property_offset ++])
            key = &property[property_offset];
            key_len = len;
            ESP_LOGD(TAG, "MQTT5_PROPERTY_USER_PROPERTY key: %.*s", key_len, (char *)key);
            property_offset += len;
            MQTT5_CONVERT_ONE_BYTE_TO_TWO(len, property[property_offset ++], property[property_offset ++])
            value = &property[property_offset];
            value_len = len;
            ESP_LOGD(TAG, "MQTT5_PROPERTY_USER_PROPERTY value: %.*s", value_len, (char *)value);
            property_offset += len;
            if (mqtt5_msg_set_user_property(&user_porperty, (char *)key, key_len, (char *)value, value_len) != ESP_OK) {
                ESP_LOGE(TAG, "mqtt5_msg_set_user_property fail");
                goto err;
            }
            continue;
        }
        default:
            ESP_LOGW(TAG, "Unknow property id 0x%02x", property_id);
            goto err;
        }
    }
    return user_porperty;
err:
    esp_mqtt5_client_delete_user_property(user_porperty);
    return NULL;
}

uint16_t mqtt5_get_id(uint8_t *buffer, size_t length)
{
    int topiclen = 0;
    uint8_t len_bytes = 0;
    size_t offset = 1;
    size_t totlen = get_variable_len(buffer, offset, length, &len_bytes);
    offset += len_bytes;
    totlen += offset;

    if (offset + 2 > length) {
        return 0;
    }

    switch (mqtt5_get_type(buffer)) {
    case MQTT_MSG_TYPE_PUBLISH: {
        MQTT5_CONVERT_ONE_BYTE_TO_TWO(topiclen, buffer[offset++], buffer[offset++])
        offset += topiclen;
        if (offset + 2 > length) {
            return 0;
        }
        if (mqtt_get_qos(buffer) == 0) {
            return 0;
        }
        return (buffer[offset] << 8) | buffer[offset + 1];
    }
    case MQTT_MSG_TYPE_PUBACK:
    case MQTT_MSG_TYPE_PUBREC:
    case MQTT_MSG_TYPE_PUBREL:
    case MQTT_MSG_TYPE_PUBCOMP:
    case MQTT_MSG_TYPE_SUBACK:
    case MQTT_MSG_TYPE_UNSUBACK:
    case MQTT_MSG_TYPE_SUBSCRIBE:
    case MQTT_MSG_TYPE_UNSUBSCRIBE: {
        return (buffer[offset] << 8) | buffer[offset + 1];
    }
    default:
        return 0;
    }
}

char *mqtt5_get_publish_property_payload(uint8_t *buffer, size_t buffer_length, char **msg_topic, size_t *msg_topic_len, esp_mqtt5_publish_resp_property_t *resp_property, uint16_t *property_len, size_t *payload_len, mqtt5_user_property_handle_t *user_property)
{
    *user_property = NULL;
    uint8_t len_bytes = 0;
    size_t offset = 1;
    size_t totlen = get_variable_len(buffer, offset, buffer_length, &len_bytes);
    offset += len_bytes;
    totlen += offset;

    size_t topic_len = buffer[offset ++] << 8;
    topic_len |= buffer[offset ++] & 0xff;
    *msg_topic = (char *)(buffer + offset);
    *msg_topic_len = topic_len;
    offset += topic_len;

    if (offset >= buffer_length) {
        return NULL;
    }

    if (mqtt5_get_qos(buffer) > 0) {
        if (offset + 2 >= buffer_length) {
            return NULL;
        }
        offset += 2; // skip the message id
    }

    *property_len = get_variable_len(buffer, offset, buffer_length, &len_bytes);
    offset += len_bytes;

    uint16_t len = 0, property_offset = 0;
    uint8_t *property = (buffer + offset);
    while (property_offset < *property_len) {
        uint8_t property_id = property[property_offset ++];
        switch (property_id) {
        case MQTT5_PROPERTY_PAYLOAD_FORMAT_INDICATOR:
            resp_property->payload_format_indicator = property[property_offset ++];
            ESP_LOGD(TAG, "MQTT5_PROPERTY_PAYLOAD_FORMAT_INDICATOR %d", resp_property->payload_format_indicator);
            continue;
        case MQTT5_PROPERTY_MESSAGE_EXPIRY_INTERVAL:
            MQTT5_CONVERT_ONE_BYTE_TO_FOUR(resp_property->message_expiry_interval, property[property_offset ++], property[property_offset ++], property[property_offset ++], property[property_offset ++])
            ESP_LOGD(TAG, "MQTT5_PROPERTY_MESSAGE_EXPIRY_INTERVAL %"PRIu32, resp_property->message_expiry_interval);
            continue;
        case MQTT5_PROPERTY_TOPIC_ALIAS:
            MQTT5_CONVERT_ONE_BYTE_TO_TWO(resp_property->topic_alias, property[property_offset ++], property[property_offset ++])
            ESP_LOGD(TAG, "MQTT5_PROPERTY_TOPIC_ALIAS %d", resp_property->topic_alias);
            continue;
        case MQTT5_PROPERTY_RESPONSE_TOPIC:
            MQTT5_CONVERT_ONE_BYTE_TO_TWO(resp_property->response_topic_len, property[property_offset ++], property[property_offset ++])
            resp_property->response_topic = (char *)(property + property_offset);
            property_offset += resp_property->response_topic_len;
            ESP_LOGD(TAG, "MQTT5_PROPERTY_RESPONSE_TOPIC %.*s", resp_property->response_topic_len, resp_property->response_topic);
            continue;
        case MQTT5_PROPERTY_CORRELATION_DATA:
            MQTT5_CONVERT_ONE_BYTE_TO_TWO(resp_property->correlation_data_len, property[property_offset ++], property[property_offset ++])
            resp_property->correlation_data = (char *)(property + property_offset);
            property_offset += resp_property->correlation_data_len;
            ESP_LOGD(TAG, "MQTT5_PROPERTY_CORRELATION_DATA length %d", resp_property->correlation_data_len);
            continue;
        case MQTT5_PROPERTY_SUBSCRIBE_IDENTIFIER:
            resp_property->subscribe_id = get_variable_len(property, property_offset, buffer_length, &len_bytes);
            property_offset += len_bytes;
            ESP_LOGD(TAG, "MQTT5_PROPERTY_SUBSCRIBE_IDENTIFIER %d", resp_property->subscribe_id);
            continue;
        case MQTT5_PROPERTY_CONTENT_TYPE:
            MQTT5_CONVERT_ONE_BYTE_TO_TWO(resp_property->content_type_len, property[property_offset ++], property[property_offset ++])
            resp_property->content_type = (char *)(property + property_offset);
            property_offset += resp_property->content_type_len;
            ESP_LOGD(TAG, "MQTT5_PROPERTY_CONTENT_TYPE  %.*s", resp_property->content_type_len, resp_property->content_type);
            continue;
        case MQTT5_PROPERTY_USER_PROPERTY: {
            uint8_t *key = NULL, *value = NULL;
            size_t key_len = 0, value_len = 0;
            MQTT5_CONVERT_ONE_BYTE_TO_TWO(len, property[property_offset ++], property[property_offset ++])
            key = &property[property_offset];
            key_len = len;
            ESP_LOGD(TAG, "MQTT5_PROPERTY_USER_PROPERTY key: %.*s", key_len, (char *)key);
            property_offset += len;
            MQTT5_CONVERT_ONE_BYTE_TO_TWO(len, property[property_offset ++], property[property_offset ++])
            value = &property[property_offset];
            value_len = len;
            ESP_LOGD(TAG, "MQTT5_PROPERTY_USER_PROPERTY value: %.*s", value_len, (char *)value);
            property_offset += len;
            if (mqtt5_msg_set_user_property(user_property, (char *)key, key_len, (char *)value, value_len) != ESP_OK) {
                esp_mqtt5_client_delete_user_property(*user_property);
                *user_property = NULL;
                ESP_LOGE(TAG, "mqtt5_msg_set_user_property fail");
                return NULL;
            }
            continue;
        }
        case MQTT5_PROPERTY_REASON_STRING: //only print now
            MQTT5_CONVERT_ONE_BYTE_TO_TWO(len, property[property_offset ++], property[property_offset ++])
            ESP_LOGD(TAG, "MQTT5_PROPERTY_REASON_STRING %.*s", len, &property[property_offset]);
            property_offset += len;
            continue;
        default:
            ESP_LOGW(TAG, "Unknow publish property id 0x%02x", property_id);
            return NULL;
        }
    }

    offset += property_offset;
    if (totlen <= buffer_length) {
        *payload_len = totlen - offset;
    } else {
        *payload_len = buffer_length - offset;
    }
    return (char *)(buffer + offset);
}

char *mqtt5_get_suback_data(uint8_t *buffer, size_t *length, mqtt5_user_property_handle_t *user_property)
{
    uint8_t len_bytes = 0;
    size_t offset = 1;
    size_t totlen = get_variable_len(buffer, offset, *length, &len_bytes);
    offset += len_bytes;
    totlen += offset;

    if (totlen > *length) {
        goto err;
    }
    offset += 2; // skip the message id
    if (offset < totlen) {
        size_t property_len = get_variable_len(buffer, offset, totlen, &len_bytes);
        offset += len_bytes;
        *user_property = mqtt5_msg_get_user_property(buffer + offset, property_len);
        offset += property_len;
        if (offset < totlen) {
            *length =  totlen - offset;
            return (char *)(buffer + offset);
        }
    }
err:
    *user_property = NULL;
    *length = 0;
    return NULL;
}

char *mqtt5_get_puback_data(uint8_t *buffer, size_t *length, mqtt5_user_property_handle_t *user_property)
{
    uint8_t len_bytes = 0;
    size_t offset = 1;
    size_t totlen = get_variable_len(buffer, offset, *length, &len_bytes);
    offset += len_bytes;
    totlen += offset;

    offset += 2; // skip the message id
    if (offset < totlen) {
        *length = 1;
        char *data = (char *)(buffer + offset);
        offset ++;
        if (offset < totlen) {
            size_t property_len = get_variable_len(buffer, offset, totlen, &len_bytes);
            offset += len_bytes;
            *user_property = mqtt5_msg_get_user_property(buffer + offset, property_len);
        }
        return data;
    } else {
        *length = 0;
        return NULL;
    }
}

mqtt_message_t *mqtt5_msg_connect(mqtt_connection_t *connection, mqtt_connect_info_t *info, esp_mqtt5_connection_property_storage_t *property, esp_mqtt5_connection_will_property_storage_t *will_property)
{
    init_message(connection);
    connection->buffer[connection->outbound_message.length ++] = 0;                         // Variable header length MSB
    /* Defaults to protocol version 5 values */
    connection->buffer[connection->outbound_message.length ++] = 4;                         // Variable header length LSB
    memcpy(&connection->buffer[connection->outbound_message.length], "MQTT", 4);           // Protocol name
    connection->outbound_message.length += 4;
    connection->buffer[connection->outbound_message.length ++] = 5;                         // Protocol version

    int flags_offset = connection->outbound_message.length;
    connection->buffer[connection->outbound_message.length ++] = 0;                         // Flags
    MQTT5_CONVERT_TWO_BYTE(connection->buffer[connection->outbound_message.length ++], info->keepalive) // Keep-alive

    if (info->clean_session) {
        connection->buffer[flags_offset] |= MQTT5_CONNECT_FLAG_CLEAN_SESSION;
    }

    //Add properties
    int properties_offset = connection->outbound_message.length;
    connection->outbound_message.length ++;
    if (property->session_expiry_interval) {
        APPEND_CHECK(append_property(connection, MQTT5_PROPERTY_SESSION_EXPIRY_INTERVAL, 4, NULL, property->session_expiry_interval), fail_message(connection));
    }
    if (property->maximum_packet_size) {
        APPEND_CHECK(append_property(connection, MQTT5_PROPERTY_MAXIMUM_PACKET_SIZE, 4, NULL, property->maximum_packet_size), fail_message(connection));
    }
    if (property->receive_maximum) {
        APPEND_CHECK(append_property(connection, MQTT5_PROPERTY_RECEIVE_MAXIMUM, 2, NULL, property->receive_maximum), fail_message(connection));
    }
    if (property->topic_alias_maximum) {
        APPEND_CHECK(append_property(connection, MQTT5_PROPERTY_TOPIC_ALIAS_MAXIMIM, 2, NULL, property->topic_alias_maximum), fail_message(connection));
    }
    if (property->request_resp_info) {
        APPEND_CHECK(append_property(connection, MQTT5_PROPERTY_REQUEST_RESP_INFO, 1, NULL, 1), fail_message(connection));
    }
    if (property->request_problem_info) {
        APPEND_CHECK(append_property(connection, MQTT5_PROPERTY_REQUEST_PROBLEM_INFO, 1, NULL, 1), fail_message(connection));
    }
    if (property->user_property) {
        mqtt5_user_property_item_t item;
        STAILQ_FOREACH(item, property->user_property, next) {
            APPEND_CHECK(append_property(connection, MQTT5_PROPERTY_USER_PROPERTY, 2, item->key, strlen(item->key)), fail_message(connection));
            APPEND_CHECK(append_property(connection, 0, 2, item->value, strlen(item->value)), fail_message(connection));
        }
    }
    APPEND_CHECK(update_property_len_value(connection, connection->outbound_message.length - properties_offset - 1, properties_offset), fail_message(connection));

    if (info->client_id != NULL && info->client_id[0] != '\0') {
        APPEND_CHECK(append_property(connection, 0, 2, info->client_id, strlen(info->client_id)), fail_message(connection));
    } else {
        APPEND_CHECK(append_property(connection, 0, 2, NULL, 0), fail_message(connection));
    }

    //Add will properties
    if (info->will_topic != NULL && info->will_topic[0] != '\0') {
        properties_offset = connection->outbound_message.length;
        connection->outbound_message.length ++;
        if (will_property->will_delay_interval) {
            APPEND_CHECK(append_property(connection, MQTT5_PROPERTY_WILL_DELAY_INTERVAL, 4, NULL, will_property->will_delay_interval), fail_message(connection));
        }
        if (will_property->payload_format_indicator) {
            APPEND_CHECK(append_property(connection, MQTT5_PROPERTY_PAYLOAD_FORMAT_INDICATOR, 1, NULL, 1), fail_message(connection));
        }
        if (will_property->message_expiry_interval) {
            APPEND_CHECK(append_property(connection, MQTT5_PROPERTY_MESSAGE_EXPIRY_INTERVAL, 4, NULL, will_property->message_expiry_interval), fail_message(connection));
        }
        if (will_property->content_type) {
            APPEND_CHECK(append_property(connection, MQTT5_PROPERTY_CONTENT_TYPE, 2, will_property->content_type, strlen(will_property->content_type)), fail_message(connection));
        }
        if (will_property->response_topic) {
            APPEND_CHECK(append_property(connection, MQTT5_PROPERTY_RESPONSE_TOPIC, 2, will_property->response_topic, strlen(will_property->response_topic)), fail_message(connection));
        }
        if (will_property->correlation_data && will_property->correlation_data_len) {
            APPEND_CHECK(append_property(connection, MQTT5_PROPERTY_CORRELATION_DATA, 2, will_property->correlation_data, will_property->correlation_data_len), fail_message(connection));
        }
        if (will_property->user_property) {
            mqtt5_user_property_item_t item;
            STAILQ_FOREACH(item, will_property->user_property, next) {
                APPEND_CHECK(append_property(connection, MQTT5_PROPERTY_USER_PROPERTY, 2, item->key, strlen(item->key)), fail_message(connection));
                APPEND_CHECK(append_property(connection, 0, 2, item->value, strlen(item->value)), fail_message(connection));
            }
        }
        APPEND_CHECK(update_property_len_value(connection, connection->outbound_message.length - properties_offset - 1, properties_offset), fail_message(connection));

        APPEND_CHECK(append_property(connection, 0, 2, info->will_topic, strlen(info->will_topic)), fail_message(connection));
        APPEND_CHECK(append_property(connection, 0, 2, info->will_message, info->will_length), fail_message(connection));

        connection->buffer[flags_offset] |= MQTT5_CONNECT_FLAG_WILL;
        if (info->will_retain) {
            connection->buffer[flags_offset] |= MQTT5_CONNECT_FLAG_WILL_RETAIN;
        }
        connection->buffer[flags_offset] |= (info->will_qos & 3) << 3;
    }

    if (info->username != NULL && info->username[0] != '\0') {
        APPEND_CHECK(append_property(connection, 0, 2, info->username, strlen(info->username)), fail_message(connection));
        connection->buffer[flags_offset] |= MQTT5_CONNECT_FLAG_USERNAME;
    }

    if (info->password != NULL && info->password[0] != '\0') {
        if (info->username == NULL || info->username[0] == '\0') {
            /* In case if password is set without username, we need to set a zero length username.
             * (otherwise we violate: MQTT-3.1.2-22: If the User Name Flag is set to 0 then the Password Flag MUST be set to 0.)
             */
            APPEND_CHECK(append_property(connection, 0, 2, NULL, 0), fail_message(connection));
            connection->buffer[flags_offset] |= MQTT5_CONNECT_FLAG_USERNAME;
        }
        APPEND_CHECK(append_property(connection, 0, 2, info->password, strlen(info->password)), fail_message(connection));
        connection->buffer[flags_offset] |= MQTT5_CONNECT_FLAG_PASSWORD;
    }

    return fini_message(connection, MQTT_MSG_TYPE_CONNECT, 0, 0, 0);
}

esp_err_t mqtt5_msg_parse_connack_property(uint8_t *buffer, size_t buffer_len, mqtt_connect_info_t *connection_info, esp_mqtt5_connection_property_storage_t *connection_property, esp_mqtt5_connection_server_resp_property_t *resp_property, int *reason_code, uint8_t *ack_flag, mqtt5_user_property_handle_t *user_property)
{
    *reason_code = 0;
    *user_property = NULL;
    uint8_t len_bytes = 0;
    size_t offset = 1;
    size_t totlen = get_variable_len(buffer, offset, buffer_len, &len_bytes);
    offset += len_bytes;
    totlen += offset;

    if (totlen > buffer_len) {
        ESP_LOGE(TAG, "Total length %d is over read len %d", totlen, buffer_len);
        return ESP_FAIL;
    }

    *ack_flag = buffer[offset ++]; //acknowledge flags
    *reason_code = buffer[offset ++]; //reason code
    size_t property_len = get_variable_len(buffer, offset, buffer_len, &len_bytes);
    offset += len_bytes;
    uint16_t property_offset = 0, len = 0;
    uint8_t *property = (buffer + offset);
    while (property_offset < property_len) {
        uint8_t property_id = property[property_offset ++];
        switch (property_id) {
        case MQTT5_PROPERTY_SESSION_EXPIRY_INTERVAL:
            MQTT5_CONVERT_ONE_BYTE_TO_FOUR(connection_property->session_expiry_interval, property[property_offset ++], property[property_offset ++], property[property_offset ++], property[property_offset ++])
            ESP_LOGD(TAG, "MQTT5_PROPERTY_SESSION_EXPIRY_INTERVAL %"PRIu32, connection_property->session_expiry_interval);
            continue;
        case MQTT5_PROPERTY_RECEIVE_MAXIMUM:
            MQTT5_CONVERT_ONE_BYTE_TO_TWO(resp_property->receive_maximum, property[property_offset ++], property[property_offset ++])
            ESP_LOGD(TAG, "MQTT5_PROPERTY_RECEIVE_MAXIMUM %d", resp_property->receive_maximum);
            continue;
        case MQTT5_PROPERTY_MAXIMUM_QOS:
            resp_property->max_qos = property[property_offset ++];
            ESP_LOGD(TAG, "MQTT5_PROPERTY_MAXIMUM_QOS %d", resp_property->max_qos);
            continue;
        case MQTT5_PROPERTY_RETAIN_AVAILABLE:
            resp_property->retain_available = property[property_offset ++];
            ESP_LOGD(TAG, "MQTT5_PROPERTY_RETAIN_AVAILABLE %d", resp_property->retain_available);
            continue;
        case MQTT5_PROPERTY_MAXIMUM_PACKET_SIZE:
            MQTT5_CONVERT_ONE_BYTE_TO_FOUR(resp_property->maximum_packet_size, property[property_offset ++], property[property_offset ++], property[property_offset ++], property[property_offset ++])
            ESP_LOGD(TAG, "MQTT5_PROPERTY_MAXIMUM_PACKET_SIZE %"PRIu32, resp_property->maximum_packet_size);
            continue;
        case MQTT5_PROPERTY_ASSIGNED_CLIENT_IDENTIFIER:
            MQTT5_CONVERT_ONE_BYTE_TO_TWO(len, property[property_offset ++], property[property_offset ++])
            if (connection_info->client_id) {
                free(connection_info->client_id);
            }
            connection_info->client_id = calloc(1, len + 1);
            if (!connection_info->client_id) {
                ESP_LOGE(TAG, "Failed to calloc %d data", len);
                return ESP_FAIL;
            }
            memcpy(connection_info->client_id, &property[property_offset], len);
            connection_info->client_id[len] = '\0';
            property_offset += len;
            ESP_LOGD(TAG, "MQTT5_PROPERTY_ASSIGNED_CLIENT_IDENTIFIER %s", connection_info->client_id);
            continue;
        case MQTT5_PROPERTY_TOPIC_ALIAS_MAXIMIM:
            MQTT5_CONVERT_ONE_BYTE_TO_TWO(resp_property->topic_alias_maximum, property[property_offset ++], property[property_offset ++])
            ESP_LOGD(TAG, "MQTT5_PROPERTY_TOPIC_ALIAS_MAXIMIM %d", resp_property->topic_alias_maximum);
            continue;
        case MQTT5_PROPERTY_REASON_STRING: //only print now
            MQTT5_CONVERT_ONE_BYTE_TO_TWO(len, property[property_offset ++], property[property_offset ++])
            ESP_LOGD(TAG, "MQTT5_PROPERTY_REASON_STRING %.*s", len, &property[property_offset]);
            property_offset += len;
            continue;
        case MQTT5_PROPERTY_USER_PROPERTY: {
            uint8_t *key = NULL, *value = NULL;
            size_t key_len = 0, value_len = 0;
            MQTT5_CONVERT_ONE_BYTE_TO_TWO(len, property[property_offset ++], property[property_offset ++])
            key = &property[property_offset];
            key_len = len;
            ESP_LOGD(TAG, "MQTT5_PROPERTY_USER_PROPERTY key: %.*s", key_len, (char *)key);
            property_offset += len;
            MQTT5_CONVERT_ONE_BYTE_TO_TWO(len, property[property_offset ++], property[property_offset ++])
            value = &property[property_offset];
            value_len = len;
            ESP_LOGD(TAG, "MQTT5_PROPERTY_USER_PROPERTY value: %.*s", value_len, (char *)value);
            property_offset += len;
            if (mqtt5_msg_set_user_property(user_property, (char *)key, key_len, (char *)value, value_len) != ESP_OK) {
                esp_mqtt5_client_delete_user_property(*user_property);
                *user_property = NULL;
                ESP_LOGE(TAG, "mqtt5_msg_set_user_property fail");
                return ESP_FAIL;
            }
            continue;
        }
        case MQTT5_PROPERTY_WILDCARD_SUBSCR_AVAILABLE:
            resp_property->wildcard_subscribe_available = property[property_offset++];
            ESP_LOGD(TAG, "MQTT5_PROPERTY_WILDCARD_SUBSCR_AVAILABLE %d", resp_property->wildcard_subscribe_available);
            continue;
        case MQTT5_PROPERTY_SUBSCR_IDENTIFIER_AVAILABLE:
            resp_property->subscribe_identifiers_available = property[property_offset++];
            ESP_LOGD(TAG, "MQTT5_PROPERTY_SUBSCR_IDENTIFIER_AVAILABLE %d", resp_property->subscribe_identifiers_available);
            continue;
        case MQTT5_PROPERTY_SHARED_SUBSCR_AVAILABLE:
            resp_property->shared_subscribe_available = property[property_offset++];
            ESP_LOGD(TAG, "MQTT5_PROPERTY_SHARED_SUBSCR_AVAILABLE %d", resp_property->shared_subscribe_available);
            continue;
        case MQTT5_PROPERTY_SERVER_KEEP_ALIVE:
            MQTT5_CONVERT_ONE_BYTE_TO_TWO(connection_info->keepalive, property[property_offset ++], property[property_offset ++])
            ESP_LOGD(TAG, "MQTT5_PROPERTY_SERVER_KEEP_ALIVE %lld", connection_info->keepalive);
            continue;
        case MQTT5_PROPERTY_RESP_INFO:
            if (resp_property->response_info) {
                free(resp_property->response_info);
            }
            MQTT5_CONVERT_ONE_BYTE_TO_TWO(len, property[property_offset ++], property[property_offset ++])
            resp_property->response_info = calloc(1, len + 1);
            if (!resp_property->response_info) {
                ESP_LOGE(TAG, "Failed to calloc %d data", len);
                return ESP_FAIL;
            }
            memcpy(resp_property->response_info, &property[property_offset], len);
            resp_property->response_info[len] = '\0';
            property_offset += len;
            ESP_LOGD(TAG, "MQTT5_PROPERTY_RESP_INFO %s", resp_property->response_info);
            continue;
        case MQTT5_PROPERTY_SERVER_REFERENCE: //only print now
            MQTT5_CONVERT_ONE_BYTE_TO_TWO(len, property[property_offset ++], property[property_offset ++])
            ESP_LOGD(TAG, "MQTT5_PROPERTY_SERVER_REFERENCE %.*s", len, &property[property_offset]);
            property_offset += len;
            continue;
        case MQTT5_PROPERTY_AUTHENTICATION_METHOD: //only print now
            MQTT5_CONVERT_ONE_BYTE_TO_TWO(len, property[property_offset ++], property[property_offset ++])
            ESP_LOGD(TAG, "MQTT5_PROPERTY_AUTHENTICATION_METHOD %.*s", len, &property[property_offset]);
            property_offset += len;
            continue;
        case MQTT5_PROPERTY_AUTHENTICATION_DATA: //only print now
            MQTT5_CONVERT_ONE_BYTE_TO_TWO(len, property[property_offset ++], property[property_offset ++])
            ESP_LOGD(TAG, "MQTT5_PROPERTY_AUTHENTICATION_DATA length %d", len);
            property_offset += len;
            continue;
        default:
            ESP_LOGW(TAG, "Unknow connack property id 0x%02x", property_id);
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

mqtt_message_t *mqtt5_msg_publish(mqtt_connection_t *connection, const char *topic, const char *data, int data_length, int qos, int retain, uint16_t *message_id, const esp_mqtt5_publish_property_config_t *property, const char *resp_info)
{
    init_message(connection);

    if (topic == NULL || topic[0] == '\0') {
        return fail_message(connection);
    }

    APPEND_CHECK(append_property(connection, 0, 2, topic, strlen(topic)), fail_message(connection));

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

    int properties_offset = connection->outbound_message.length;
    connection->outbound_message.length ++;

    if (property) {
        if (property->payload_format_indicator) {
            APPEND_CHECK(append_property(connection, MQTT5_PROPERTY_PAYLOAD_FORMAT_INDICATOR, 1, NULL, 1), fail_message(connection));
        }
        if (property->message_expiry_interval) {
            APPEND_CHECK(append_property(connection, MQTT5_PROPERTY_MESSAGE_EXPIRY_INTERVAL, 4, NULL, property->message_expiry_interval), fail_message(connection));
        }
        if (property->topic_alias) {
            APPEND_CHECK(append_property(connection, MQTT5_PROPERTY_TOPIC_ALIAS, 2, NULL, property->topic_alias), fail_message(connection));
        }
        if (property->response_topic) {
            if (resp_info && strlen(resp_info)) {
                uint16_t response_topic_size = strlen(property->response_topic) + strlen(resp_info) + 1;
                char *response_topic = calloc(1, response_topic_size);
                if (!response_topic) {
                    ESP_LOGE(TAG, "Failed to calloc %d memory", response_topic_size);
                    fail_message(connection);
                }
                snprintf(response_topic, response_topic_size, "%s/%s", property->response_topic, resp_info);
                if (append_property(connection, MQTT5_PROPERTY_RESPONSE_TOPIC, 2, response_topic, response_topic_size) == -1) {
                    ESP_LOGE(TAG, "%s(%d) fail", __FUNCTION__, __LINE__);
                    free(response_topic);
                    return fail_message(connection);
                }
                free(response_topic);
            } else {
                APPEND_CHECK(append_property(connection, MQTT5_PROPERTY_RESPONSE_TOPIC, 2, property->response_topic, strlen(property->response_topic)), fail_message(connection));
            }
        }
        if (property->correlation_data && property->correlation_data_len) {
            APPEND_CHECK(append_property(connection, MQTT5_PROPERTY_CORRELATION_DATA, 2, property->correlation_data, property->correlation_data_len), fail_message(connection));
        }
        if (property->user_property) {
            mqtt5_user_property_item_t item;
            STAILQ_FOREACH(item, property->user_property, next) {
                APPEND_CHECK(append_property(connection, MQTT5_PROPERTY_USER_PROPERTY, 2, item->key, strlen(item->key)), fail_message(connection));
                APPEND_CHECK(append_property(connection, 0, 2, item->value, strlen(item->value)), fail_message(connection));
            }
        }
        if (property->content_type) {
            APPEND_CHECK(append_property(connection, MQTT5_PROPERTY_CONTENT_TYPE, 2, property->content_type, strlen(property->content_type)), fail_message(connection));
        }
    }
    APPEND_CHECK(update_property_len_value(connection, connection->outbound_message.length - properties_offset - 1, properties_offset), fail_message(connection));

    if (connection->outbound_message.length + data_length > connection->buffer_length) {
        // Not enough size in buffer -> fragment this message
        connection->outbound_message.fragmented_msg_data_offset = connection->outbound_message.length;
        memcpy(connection->buffer + connection->outbound_message.length, data, connection->buffer_length - connection->outbound_message.length);
        connection->outbound_message.length = connection->buffer_length;
        connection->outbound_message.fragmented_msg_total_length = data_length + connection->outbound_message.fragmented_msg_data_offset;
    } else {
        if (data != NULL) {
            memcpy(connection->buffer + connection->outbound_message.length, data, data_length);
            connection->outbound_message.length += data_length;
        }
        connection->outbound_message.fragmented_msg_total_length = 0;
    }
    return fini_message(connection, MQTT_MSG_TYPE_PUBLISH, 0, qos, retain);
}

int mqtt5_msg_get_reason_code(uint8_t *buffer, size_t length)
{
    uint8_t len_bytes = 0;
    size_t offset = 1;
    size_t totlen = get_variable_len(buffer, offset, length, &len_bytes);
    offset += len_bytes;
    totlen += offset;

    switch (mqtt5_get_type(buffer)) {
    case MQTT_MSG_TYPE_PUBACK:
    case MQTT_MSG_TYPE_PUBREC:
    case MQTT_MSG_TYPE_PUBREL:
    case MQTT_MSG_TYPE_PUBCOMP:
        offset += 2; //skip the message id
        if (offset >= length) {
            return -1;
        }
        return buffer[offset];
    case MQTT_MSG_TYPE_SUBACK:
    case MQTT_MSG_TYPE_UNSUBACK: {
        offset += 2; //skip the message id
        if (offset >= length) {
            return -1;
        }
        size_t property_len = get_variable_len(buffer, offset, length, &len_bytes);
        offset = offset + len_bytes + property_len;
        if (offset >= length) {
            return -1;
        } else {
            return buffer[offset];
        }
    }
    case MQTT_MSG_TYPE_DISCONNECT:
        if (offset >= length) {
            return -1;
        } else {
            return buffer[offset];
        }
    default:
        break;
    }
    return -1;
}

mqtt_message_t *mqtt5_msg_subscribe(mqtt_connection_t *connection, const esp_mqtt_topic_t *topic_list, int size, uint16_t *message_id, const esp_mqtt5_subscribe_property_config_t *property)
{
    init_message(connection);

    if ((*message_id = append_message_id(connection, 0)) == 0) {
        return fail_message(connection);
    }

    int properties_offset = connection->outbound_message.length;
    connection->outbound_message.length ++;

    if (property) {
        if (property->subscribe_id) {
            APPEND_CHECK(append_property(connection, MQTT5_PROPERTY_SUBSCRIBE_IDENTIFIER, 0, NULL, property->subscribe_id), fail_message(connection));
        }
        if (property->user_property) {
            mqtt5_user_property_item_t item;
            STAILQ_FOREACH(item, property->user_property, next) {
                APPEND_CHECK(append_property(connection, MQTT5_PROPERTY_USER_PROPERTY, 2, item->key, strlen(item->key)), fail_message(connection));
                APPEND_CHECK(append_property(connection, 0, 2, item->value, strlen(item->value)), fail_message(connection));
            }
        }
    }
    APPEND_CHECK(update_property_len_value(connection, connection->outbound_message.length - properties_offset - 1, properties_offset), fail_message(connection));

    for (int topic_number = 0; topic_number < size; ++topic_number) {
        if (topic_list[topic_number].filter[0] == '\0') {
            return fail_message(connection);
        }
        if (property && property->is_share_subscribe) {
            uint16_t shared_topic_size = strlen(topic_list[topic_number].filter) + strlen(MQTT5_SHARED_SUB) + strlen(property->share_name);
            char *shared_topic = calloc(1, shared_topic_size);
            if (!shared_topic) {
                ESP_LOGE(TAG, "Failed to calloc %d memory", shared_topic_size);
                fail_message(connection);
            }
            snprintf(shared_topic, shared_topic_size, MQTT5_SHARED_SUB, property->share_name, topic_list[topic_number].filter);
            if (append_property(connection, 0, 2, shared_topic, strlen(shared_topic)) == -1) {
                ESP_LOGE(TAG, "%s(%d) fail", __FUNCTION__, __LINE__);
                free(shared_topic);
                return fail_message(connection);
            }
            free(shared_topic);
        } else {
            APPEND_CHECK(append_property(connection, 0, 2, topic_list[topic_number].filter, strlen(topic_list[topic_number].filter)), fail_message(connection));
        }

        if (connection->outbound_message.length + 1 > connection->buffer_length) {
            return fail_message(connection);
        }
        connection->buffer[connection->outbound_message.length] = 0;
        if (property) {
            if (property->retain_handle > 0 && property->retain_handle < 3) {
                connection->buffer[connection->outbound_message.length] |= (property->retain_handle & 3) << 4;
            }
            if (property->no_local_flag) {
                connection->buffer[connection->outbound_message.length] |= (property->no_local_flag << 2);
            }
            if (property->retain_as_published_flag) {
                connection->buffer[connection->outbound_message.length] |= (property->retain_as_published_flag << 3);
            }
        }
        connection->buffer[connection->outbound_message.length] |= (topic_list[topic_number].qos & 3);
        connection->outbound_message.length ++;
    }
    return fini_message(connection, MQTT_MSG_TYPE_SUBSCRIBE, 0, 1, 0);
}

mqtt_message_t *mqtt5_msg_disconnect(mqtt_connection_t *connection, esp_mqtt5_disconnect_property_config_t *disconnect_property_info)
{
    init_message(connection);
    int reason_offset = connection->outbound_message.length;
    connection->buffer[connection->outbound_message.length ++] = 0;
    int properties_offset = connection->outbound_message.length;
    connection->outbound_message.length ++;
    if (disconnect_property_info) {
        if (disconnect_property_info->session_expiry_interval) {
            APPEND_CHECK(append_property(connection, MQTT5_PROPERTY_SESSION_EXPIRY_INTERVAL, 4, NULL, disconnect_property_info->session_expiry_interval), fail_message(connection));
        }
        if (disconnect_property_info->user_property) {
            mqtt5_user_property_item_t item;
            STAILQ_FOREACH(item, disconnect_property_info->user_property, next) {
                APPEND_CHECK(append_property(connection, MQTT5_PROPERTY_USER_PROPERTY, 2, item->key, strlen(item->key)), fail_message(connection));
                APPEND_CHECK(append_property(connection, 0, 2, item->value, strlen(item->value)), fail_message(connection));
            }
        }
        if (disconnect_property_info->disconnect_reason) {
            connection->buffer[reason_offset] = disconnect_property_info->disconnect_reason;
        }
    }
    APPEND_CHECK(update_property_len_value(connection, connection->outbound_message.length - properties_offset - 1, properties_offset), fail_message(connection));
    return fini_message(connection, MQTT_MSG_TYPE_DISCONNECT, 0, 0, 0);
}

mqtt_message_t *mqtt5_msg_unsubscribe(mqtt_connection_t *connection, const char *topic, uint16_t *message_id, const esp_mqtt5_unsubscribe_property_config_t *property)
{
    init_message(connection);

    if (topic == NULL || topic[0] == '\0') {
        return fail_message(connection);
    }

    if ((*message_id = append_message_id(connection, 0)) == 0) {
        return fail_message(connection);
    }

    int properties_offset = connection->outbound_message.length;
    connection->outbound_message.length ++;
    if (property) {
        if (property->user_property) {
            mqtt5_user_property_item_t item;
            STAILQ_FOREACH(item, property->user_property, next) {
                APPEND_CHECK(append_property(connection, MQTT5_PROPERTY_USER_PROPERTY, 2, item->key, strlen(item->key)), fail_message(connection));
                APPEND_CHECK(append_property(connection, 0, 2, item->value, strlen(item->value)), fail_message(connection));
            }
        }
    }

    APPEND_CHECK(update_property_len_value(connection, connection->outbound_message.length - properties_offset - 1, properties_offset), fail_message(connection));
    if (property && property->is_share_subscribe) {
        uint16_t shared_topic_size = strlen(topic) + strlen(MQTT5_SHARED_SUB) + strlen(property->share_name);
        char *shared_topic = calloc(1, shared_topic_size);
        if (!shared_topic) {
            ESP_LOGE(TAG, "Failed to calloc %d memory", shared_topic_size);
            fail_message(connection);
        }
        snprintf(shared_topic, shared_topic_size, MQTT5_SHARED_SUB, property->share_name, topic);
        if (append_property(connection, 0, 2, shared_topic, strlen(shared_topic)) == -1) {
            ESP_LOGE(TAG, "%s(%d) fail", __FUNCTION__, __LINE__);
            free(shared_topic);
            return fail_message(connection);
        }
        free(shared_topic);
    } else {
        APPEND_CHECK(append_property(connection, 0, 2, topic, strlen(topic)), fail_message(connection));
    }

    return fini_message(connection, MQTT_MSG_TYPE_UNSUBSCRIBE, 0, 1, 0);
}

mqtt_message_t *mqtt5_msg_puback(mqtt_connection_t *connection, uint16_t message_id)
{
    init_message(connection);
    if (append_message_id(connection, message_id) == 0) {
        return fail_message(connection);
    }
    connection->buffer[connection->outbound_message.length ++] = 0; // Regard it is success
    int properties_offset = connection->outbound_message.length;
    connection->outbound_message.length ++;
    APPEND_CHECK(update_property_len_value(connection, connection->outbound_message.length - properties_offset - 1, properties_offset), fail_message(connection));
    return fini_message(connection, MQTT_MSG_TYPE_PUBACK, 0, 0, 0);
}

mqtt_message_t *mqtt5_msg_pubrec(mqtt_connection_t *connection, uint16_t message_id)
{
    init_message(connection);
    if (append_message_id(connection, message_id) == 0) {
        return fail_message(connection);
    }
    connection->buffer[connection->outbound_message.length ++] = 0; // Regard it is success
    int properties_offset = connection->outbound_message.length;
    connection->outbound_message.length ++;
    APPEND_CHECK(update_property_len_value(connection, connection->outbound_message.length - properties_offset - 1, properties_offset), fail_message(connection));
    return fini_message(connection, MQTT_MSG_TYPE_PUBREC, 0, 0, 0);
}

mqtt_message_t *mqtt5_msg_pubrel(mqtt_connection_t *connection, uint16_t message_id)
{
    init_message(connection);
    if (append_message_id(connection, message_id) == 0) {
        return fail_message(connection);
    }
    connection->buffer[connection->outbound_message.length ++] = 0; // Regard it is success
    int properties_offset = connection->outbound_message.length;
    connection->outbound_message.length ++;
    APPEND_CHECK(update_property_len_value(connection, connection->outbound_message.length - properties_offset - 1, properties_offset), fail_message(connection));
    return fini_message(connection, MQTT_MSG_TYPE_PUBREL, 0, 1, 0);
}

mqtt_message_t *mqtt5_msg_pubcomp(mqtt_connection_t *connection, uint16_t message_id)
{
    init_message(connection);
    if (append_message_id(connection, message_id) == 0) {
        return fail_message(connection);
    }
    connection->buffer[connection->outbound_message.length ++] = 0; // Regard it is success
    int properties_offset = connection->outbound_message.length;
    connection->outbound_message.length ++;
    APPEND_CHECK(update_property_len_value(connection, connection->outbound_message.length - properties_offset - 1, properties_offset), fail_message(connection));
    return fini_message(connection, MQTT_MSG_TYPE_PUBCOMP, 0, 0, 0);
}
