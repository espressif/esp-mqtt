/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mqtt_client_priv.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "mqtt5_client";

static void esp_mqtt5_print_error_code(esp_mqtt5_client_handle_t client, int code);
static esp_err_t esp_mqtt5_client_update_topic_alias(mqtt5_topic_alias_handle_t topic_alias_handle, uint16_t topic_alias, char *topic, size_t topic_len);
static char *esp_mqtt5_client_get_topic_alias(mqtt5_topic_alias_handle_t topic_alias_handle, uint16_t topic_alias, size_t *topic_length);
static void esp_mqtt5_client_delete_topic_alias(mqtt5_topic_alias_handle_t topic_alias_handle);
static esp_err_t esp_mqtt5_user_property_copy(mqtt5_user_property_handle_t user_property_new, const mqtt5_user_property_handle_t user_property_old);

void esp_mqtt5_increment_packet_counter(esp_mqtt5_client_handle_t client)
{
    bool msg_dup = mqtt5_get_dup(client->mqtt_state.connection.outbound_message.data);
    if (msg_dup == false) {
        client->send_publish_packet_count ++;
        ESP_LOGD(TAG, "Sent (%d) qos > 0 publish packet without ack", client->send_publish_packet_count);
    }
}

void esp_mqtt5_decrement_packet_counter(esp_mqtt5_client_handle_t client)
{
    if (client->send_publish_packet_count > 0) {
        client->send_publish_packet_count --;
        ESP_LOGD(TAG, "Receive (%d) qos > 0 publish packet with ack", client->send_publish_packet_count);
    }
}

void esp_mqtt5_parse_pubcomp(esp_mqtt5_client_handle_t client)
{
    if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
        ESP_LOGI(TAG, "MQTT_MSG_TYPE_PUBCOMP return code is %d", mqtt5_msg_get_reason_code(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_read_len));
        size_t msg_data_len = client->mqtt_state.in_buffer_read_len;
        client->event.data = mqtt5_get_pubcomp_data(client->mqtt_state.in_buffer, &msg_data_len, &client->event.property->user_property);
        client->event.data_len = msg_data_len;
        client->event.total_data_len = msg_data_len;
        client->event.current_data_offset = 0;
    }
}

void esp_mqtt5_parse_puback(esp_mqtt5_client_handle_t client)
{
    if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
        ESP_LOGI(TAG, "MQTT_MSG_TYPE_PUBACK return code is %d", mqtt5_msg_get_reason_code(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_read_len));
        size_t msg_data_len = client->mqtt_state.in_buffer_read_len;
        client->event.data = mqtt5_get_puback_data(client->mqtt_state.in_buffer, &msg_data_len, &client->event.property->user_property);
        client->event.data_len = msg_data_len;
        client->event.total_data_len = msg_data_len;
        client->event.current_data_offset = 0;
    }
}

void esp_mqtt5_parse_unsuback(esp_mqtt5_client_handle_t client)
{
    if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
        ESP_LOGI(TAG, "MQTT_MSG_TYPE_UNSUBACK return code is %d", mqtt5_msg_get_reason_code(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_read_len));
        size_t msg_data_len = client->mqtt_state.in_buffer_read_len;
        client->event.data = mqtt5_get_unsuback_data(client->mqtt_state.in_buffer, &msg_data_len, &client->event.property->user_property);
        client->event.data_len = msg_data_len;
        client->event.total_data_len = msg_data_len;
        client->event.current_data_offset = 0;
    }
}

void esp_mqtt5_parse_suback(esp_mqtt5_client_handle_t client)
{
    if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
        ESP_LOGI(TAG, "MQTT_MSG_TYPE_SUBACK return code is %d", mqtt5_msg_get_reason_code(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_read_len));
    }
}

esp_err_t esp_mqtt5_parse_connack(esp_mqtt5_client_handle_t client, int *connect_rsp_code)
{
    size_t len = client->mqtt_state.in_buffer_read_len;
    client->mqtt_state.in_buffer_read_len = 0;
    uint8_t ack_flag = 0;
    if (mqtt5_msg_parse_connack_property(client->mqtt_state.in_buffer, len, &client->mqtt_state.
                                         connection.information, &client->mqtt5_config->connect_property_info, &client->mqtt5_config->server_resp_property_info, connect_rsp_code, &ack_flag, &client->event.property->user_property) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse CONNACK packet");
        return ESP_FAIL;
    }
    if (*connect_rsp_code == MQTT_CONNECTION_ACCEPTED) {
        ESP_LOGD(TAG, "Connected");
        client->event.session_present = ack_flag & 0x01;
        return ESP_OK;
    }
    esp_mqtt5_print_error_code(client, *connect_rsp_code);
    return ESP_FAIL;
}

esp_err_t esp_mqtt5_get_publish_data(esp_mqtt5_client_handle_t client, uint8_t *msg_buf, size_t msg_read_len, char **msg_topic, size_t *msg_topic_len, char **msg_data, size_t *msg_data_len)
{
    // get property
    uint16_t property_len = 0;
    esp_mqtt5_publish_resp_property_t property = {0};
    *msg_data = mqtt5_get_publish_property_payload(msg_buf, msg_read_len, msg_topic, msg_topic_len, &property, &property_len, msg_data_len, &client->event.property->user_property);
     if (*msg_data == NULL) {
        ESP_LOGE(TAG, "%s: mqtt5_get_publish_property_payload() failed", __func__);
        return ESP_FAIL;
    }

    if (property.topic_alias > client->mqtt5_config->connect_property_info.topic_alias_maximum) {
        ESP_LOGE(TAG, "%s: Broker response topic alias %d is over the max topic alias %d", __func__, property.topic_alias, client->mqtt5_config->connect_property_info.topic_alias_maximum);
        return ESP_FAIL;
    }

    if (property.topic_alias) {
        if (*msg_topic_len == 0) {
            ESP_LOGI(TAG, "Publish topic is empty, use topic alias");
            *msg_topic = esp_mqtt5_client_get_topic_alias(client->mqtt5_config->peer_topic_alias, property.topic_alias, msg_topic_len);
            if (!*msg_topic) {
                ESP_LOGE(TAG, "%s: esp_mqtt5_client_get_topic_alias() failed", __func__);
                return ESP_FAIL;
            }
        } else {
            if (esp_mqtt5_client_update_topic_alias(client->mqtt5_config->peer_topic_alias, property.topic_alias, *msg_topic, *msg_topic_len) != ESP_OK) {
                ESP_LOGE(TAG, "%s: esp_mqtt5_client_update_topic_alias() failed", __func__);
                return ESP_FAIL;
            }
        }
    }

    client->event.property->payload_format_indicator = property.payload_format_indicator;
    client->event.property->response_topic = property.response_topic;
    client->event.property->response_topic_len = property.response_topic_len;
    client->event.property->correlation_data = property.correlation_data;
    client->event.property->correlation_data_len = property.correlation_data_len;
    client->event.property->content_type = property.content_type;
    client->event.property->content_type_len = property.content_type_len;
    client->event.property->subscribe_id = property.subscribe_id;
    return ESP_OK;
}

esp_err_t esp_mqtt5_create_default_config(esp_mqtt5_client_handle_t client)
{
    if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
        client->event.property = calloc(1, sizeof(esp_mqtt5_event_property_t));
        ESP_MEM_CHECK(TAG, client->event.property, return ESP_FAIL)
        client->mqtt5_config = calloc(1, sizeof(mqtt5_config_storage_t));
        ESP_MEM_CHECK(TAG, client->mqtt5_config, return ESP_FAIL)
        client->mqtt5_config->server_resp_property_info.max_qos = 2;
        client->mqtt5_config->server_resp_property_info.retain_available = true;
        client->mqtt5_config->server_resp_property_info.wildcard_subscribe_available = true;
        client->mqtt5_config->server_resp_property_info.subscribe_identifiers_available = true;
        client->mqtt5_config->server_resp_property_info.shared_subscribe_available = true;
        client->mqtt5_config->server_resp_property_info.receive_maximum = 65535;
    }
    return ESP_OK;
}

static void esp_mqtt5_print_error_code(esp_mqtt5_client_handle_t client, int code)
{
    switch (code) {
    case MQTT5_UNSPECIFIED_ERROR:
        ESP_LOGW(TAG, "Unspecified error");
        break;
    case MQTT5_MALFORMED_PACKET:
        ESP_LOGW(TAG, "Malformed Packet");
        break;
    case MQTT5_PROTOCOL_ERROR:
        ESP_LOGW(TAG, "Protocol Error");
        break;
    case MQTT5_IMPLEMENT_SPECIFIC_ERROR:
        ESP_LOGW(TAG, "Implementation specific error");
        break;
    case MQTT5_UNSUPPORTED_PROTOCOL_VER:
        ESP_LOGW(TAG, "Unsupported Protocol Version");
        break;
    case MQTT5_INVALID_CLIENT_ID:
        ESP_LOGW(TAG, "Client Identifier not valid");
        break;
    case MQTT5_BAD_USERNAME_OR_PWD:
        ESP_LOGW(TAG, "Bad User Name or Password");
        break;
    case MQTT5_NOT_AUTHORIZED:
        ESP_LOGW(TAG, "Not authorized");
        break;
    case MQTT5_SERVER_UNAVAILABLE:
        ESP_LOGW(TAG, "Server unavailable");
        break;
    case MQTT5_SERVER_BUSY:
        ESP_LOGW(TAG, "Server busy");
        break;
    case MQTT5_BANNED:
        ESP_LOGW(TAG, "Banned");
        break;
    case MQTT5_SERVER_SHUTTING_DOWN:
        ESP_LOGW(TAG, "Server shutting down");
        break;
    case MQTT5_BAD_AUTH_METHOD:
        ESP_LOGW(TAG, "Bad authentication method");
        break;
    case MQTT5_KEEP_ALIVE_TIMEOUT:
        ESP_LOGW(TAG, "Keep Alive timeout");
        break;
    case MQTT5_SESSION_TAKEN_OVER:
        ESP_LOGW(TAG, "Session taken over");
        break;
    case MQTT5_TOPIC_FILTER_INVALID:
        ESP_LOGW(TAG, "Topic Filter invalid");
        break;
    case MQTT5_TOPIC_NAME_INVALID:
        ESP_LOGW(TAG, "Topic Name invalid");
        break;
    case MQTT5_PACKET_IDENTIFIER_IN_USE:
        ESP_LOGW(TAG, "Packet Identifier in use");
        break;
    case MQTT5_PACKET_IDENTIFIER_NOT_FOUND:
        ESP_LOGW(TAG, "Packet Identifier not found");
        break;
    case MQTT5_RECEIVE_MAXIMUM_EXCEEDED:
        ESP_LOGW(TAG, "Receive Maximum exceeded");
        break;
    case MQTT5_TOPIC_ALIAS_INVALID:
        ESP_LOGW(TAG, "Topic Alias invalid");
        break;
    case MQTT5_PACKET_TOO_LARGE:
        ESP_LOGW(TAG, "Packet too large");
        break;
    case MQTT5_MESSAGE_RATE_TOO_HIGH:
        ESP_LOGW(TAG, "Message rate too high");
        break;
    case MQTT5_QUOTA_EXCEEDED:
        ESP_LOGW(TAG, "Quota exceeded");
        break;
    case MQTT5_ADMINISTRATIVE_ACTION:
        ESP_LOGW(TAG, "Administrative action");
        break;
    case MQTT5_PAYLOAD_FORMAT_INVALID:
        ESP_LOGW(TAG, "Payload format invalid");
        break;
    case MQTT5_RETAIN_NOT_SUPPORT:
        ESP_LOGW(TAG, "Retain not supported");
        break;
    case MQTT5_QOS_NOT_SUPPORT:
        ESP_LOGW(TAG, "QoS not supported");
        break;
    case MQTT5_USE_ANOTHER_SERVER:
        ESP_LOGW(TAG, "Use another server");
        break;
    case MQTT5_SERVER_MOVED:
        ESP_LOGW(TAG, "Server moved");
        break;
    case MQTT5_SHARED_SUBSCR_NOT_SUPPORTED:
        ESP_LOGW(TAG, "Shared Subscriptions not supported");
        break;
    case MQTT5_CONNECTION_RATE_EXCEEDED:
        ESP_LOGW(TAG, "Connection rate exceeded");
        break;
    case MQTT5_MAXIMUM_CONNECT_TIME:
        ESP_LOGW(TAG, "Maximum connect time");
        break;
    case MQTT5_SUBSCRIBE_IDENTIFIER_NOT_SUPPORT:
        ESP_LOGW(TAG, "Subscription Identifiers not supported");
        break;
    case MQTT5_WILDCARD_SUBSCRIBE_NOT_SUPPORT:
        ESP_LOGW(TAG, "Wildcard Subscriptions not supported");
        break;
    default:
        ESP_LOGW(TAG, "Connection refused, Unknow reason");
        break;
    }
}

esp_err_t esp_mqtt5_client_subscribe_check(esp_mqtt5_client_handle_t client, int qos)
{
    /* Check Server support QoS level */
    if (client->mqtt5_config->server_resp_property_info.max_qos < qos) {
        ESP_LOGE(TAG, "Server only support max QoS level %d", client->mqtt5_config->server_resp_property_info.max_qos);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t esp_mqtt5_client_publish_check(esp_mqtt5_client_handle_t client, int qos, int retain)
{
    /* Check Server support QoS level */
    if (client->mqtt5_config->server_resp_property_info.max_qos < qos) {
        ESP_LOGE(TAG, "Server only support max QoS level %d", client->mqtt5_config->server_resp_property_info.max_qos);
        return ESP_FAIL;
    }

    /* Check Server support RETAIN */
    if (!client->mqtt5_config->server_resp_property_info.retain_available && retain) {
        ESP_LOGE(TAG, "Server not support retain");
        return ESP_FAIL;
    }

    /* Flow control to check PUBLISH(No PUBACK or PUBCOMP received) packet sent count(Only record QoS1 and QoS2)*/
    if (client->send_publish_packet_count > client->mqtt5_config->server_resp_property_info.receive_maximum) {
        ESP_LOGE(TAG, "Client send more than %d QoS1 and QoS2 PUBLISH packet without no ack", client->mqtt5_config->server_resp_property_info.receive_maximum);
        return ESP_FAIL;
    }

    return ESP_OK;
}

void esp_mqtt5_client_destory(esp_mqtt5_client_handle_t client)
{
    if (client->mqtt_state.connection.information.protocol_ver == MQTT_PROTOCOL_V_5) {
        if (client->mqtt5_config) {
            free(client->mqtt5_config->will_property_info.content_type);
            free(client->mqtt5_config->will_property_info.response_topic);
            free(client->mqtt5_config->will_property_info.correlation_data);
            free(client->mqtt5_config->server_resp_property_info.response_info);
            esp_mqtt5_client_delete_topic_alias(client->mqtt5_config->peer_topic_alias);
            esp_mqtt5_client_delete_user_property(client->mqtt5_config->connect_property_info.user_property);
            esp_mqtt5_client_delete_user_property(client->mqtt5_config->will_property_info.user_property);
            esp_mqtt5_client_delete_user_property(client->mqtt5_config->disconnect_property_info.user_property);
            free(client->mqtt5_config);
        }
        free(client->event.property);
    }
}

static void esp_mqtt5_client_delete_topic_alias(mqtt5_topic_alias_handle_t topic_alias_handle)
{
    if (topic_alias_handle) {
        mqtt5_topic_alias_item_t item, tmp;
        STAILQ_FOREACH_SAFE(item, topic_alias_handle, next, tmp) {
            STAILQ_REMOVE(topic_alias_handle, item, mqtt5_topic_alias, next);
            free(item->topic);
            free(item);
        }
        free(topic_alias_handle);
    }
}

static esp_err_t esp_mqtt5_client_update_topic_alias(mqtt5_topic_alias_handle_t topic_alias_handle, uint16_t topic_alias, char *topic, size_t topic_len)
{
    mqtt5_topic_alias_item_t item;
    bool found = false;
    STAILQ_FOREACH(item, topic_alias_handle, next) {
        if (item->topic_alias == topic_alias) {
            found = true;
            break;
        }
    }
    if (found) {
        if ((item->topic_len != topic_len) || strncmp(topic, item->topic, topic_len)) {
            free(item->topic);
            item->topic = calloc(1, topic_len);
            ESP_MEM_CHECK(TAG, item->topic, return ESP_FAIL);
            memcpy(item->topic, topic, topic_len);
            item->topic_len = topic_len;
        }
    } else {
        item = calloc(1, sizeof(mqtt5_topic_alias_t));
        ESP_MEM_CHECK(TAG, item, return ESP_FAIL);
        item->topic_alias = topic_alias;
        item->topic_len = topic_len;
        item->topic = calloc(1, topic_len);
        ESP_MEM_CHECK(TAG, item->topic, {
            free(item);
            return ESP_FAIL;
        });
        memcpy(item->topic, topic, topic_len);
        STAILQ_INSERT_TAIL(topic_alias_handle, item, next);
    }
    return ESP_OK;
}

static char *esp_mqtt5_client_get_topic_alias(mqtt5_topic_alias_handle_t topic_alias_handle, uint16_t topic_alias, size_t *topic_length)
{
    mqtt5_topic_alias_item_t item;
    STAILQ_FOREACH(item, topic_alias_handle, next) {
        if (item->topic_alias == topic_alias) {
            *topic_length = item->topic_len;
            return item->topic;
        }
    }
    *topic_length = 0;
    return NULL;
}

static esp_err_t esp_mqtt5_user_property_copy(mqtt5_user_property_handle_t user_property_new, const mqtt5_user_property_handle_t user_property_old)
{
    if (!user_property_new || !user_property_old) {
        ESP_LOGE(TAG, "Input is NULL");
        return ESP_FAIL;
    }

    mqtt5_user_property_item_t old_item, new_item;
    STAILQ_FOREACH(old_item, user_property_old, next) {
        new_item = calloc(1, sizeof(mqtt5_user_property_t));
        ESP_MEM_CHECK(TAG, new_item, return ESP_FAIL);
        new_item->key = strdup(old_item->key);
        ESP_MEM_CHECK(TAG, new_item->key, {
            free(new_item);
            return ESP_FAIL;
        });
        new_item->value = strdup(old_item->value);
        ESP_MEM_CHECK(TAG, new_item->value, {
            free(new_item->key);
            free(new_item);
            return ESP_FAIL;
        });
        STAILQ_INSERT_TAIL(user_property_new, new_item, next);
    }
    return ESP_OK;
}

esp_err_t esp_mqtt5_client_set_publish_property(esp_mqtt5_client_handle_t client, const esp_mqtt5_publish_property_config_t *property)
{
    if (!client) {
        ESP_LOGE(TAG, "Client was not initialized");
        return ESP_ERR_INVALID_ARG;
    }
    MQTT_API_LOCK(client);

    /* Check protocol version */
    if (client->mqtt_state.connection.information.protocol_ver != MQTT_PROTOCOL_V_5) {
        ESP_LOGE(TAG, "MQTT protocol version is not v5");
        MQTT_API_UNLOCK(client);
        return ESP_FAIL;
    }
    /* Check topic alias less than server maximum topic alias */
    if (property->topic_alias > client->mqtt5_config->server_resp_property_info.topic_alias_maximum) {
        ESP_LOGE(TAG, "Topic alias %d is bigger than server support %d", property->topic_alias, client->mqtt5_config->server_resp_property_info.topic_alias_maximum);
        MQTT_API_UNLOCK(client);
        return ESP_FAIL;
    }
    client->mqtt5_config->publish_property_info = property;
    MQTT_API_UNLOCK(client);
    return ESP_OK;
}

esp_err_t esp_mqtt5_client_set_subscribe_property(esp_mqtt5_client_handle_t client, const esp_mqtt5_subscribe_property_config_t *property)
{
    if (!client) {
        ESP_LOGE(TAG, "Client was not initialized");
        return ESP_ERR_INVALID_ARG;
    }
    if (property->retain_handle > 2) {
        ESP_LOGE(TAG, "retain_handle only support 0, 1, 2");
        return -1;
    }
    MQTT_API_LOCK(client);

    /* Check protocol version */
    if (client->mqtt_state.connection.information.protocol_ver != MQTT_PROTOCOL_V_5) {
        ESP_LOGE(TAG, "MQTT protocol version is not v5");
        MQTT_API_UNLOCK(client);
        return ESP_FAIL;
    }
    if (property->is_share_subscribe) {
        if (property->no_local_flag) {
            // MQTT-3.8.3-4 not allow that No Local bit to 1 on a Shared Subscription
            ESP_LOGE(TAG, "Protocol error that no local flag set on shared subscription");
            MQTT_API_UNLOCK(client);
            return ESP_FAIL;
        }
        if (!client->mqtt5_config->server_resp_property_info.shared_subscribe_available) {
            ESP_LOGE(TAG, "MQTT broker not support shared subscribe");
            MQTT_API_UNLOCK(client);
            return ESP_FAIL;
        }
        if (!property->share_name || !strlen(property->share_name)) {
            ESP_LOGE(TAG, "Share name can't be empty for shared subscribe");
            MQTT_API_UNLOCK(client);
            return ESP_FAIL;
        }
    }
    client->mqtt5_config->subscribe_property_info = property;
    MQTT_API_UNLOCK(client);
    return ESP_OK;
}

esp_err_t esp_mqtt5_client_set_unsubscribe_property(esp_mqtt5_client_handle_t client, const esp_mqtt5_unsubscribe_property_config_t *property)
{
    if (!client) {
        ESP_LOGE(TAG, "Client was not initialized");
        return ESP_ERR_INVALID_ARG;
    }
    MQTT_API_LOCK(client);

    /* Check protocol version */
    if (client->mqtt_state.connection.information.protocol_ver != MQTT_PROTOCOL_V_5) {
        ESP_LOGE(TAG, "MQTT protocol version is not v5");
        MQTT_API_UNLOCK(client);
        return ESP_FAIL;
    }
    if (property->is_share_subscribe) {
        if (!client->mqtt5_config->server_resp_property_info.shared_subscribe_available) {
            ESP_LOGE(TAG, "MQTT broker not support shared subscribe");
            MQTT_API_UNLOCK(client);
            return ESP_FAIL;
        }
        if (!property->share_name || !strlen(property->share_name)) {
            ESP_LOGE(TAG, "Share name can't be empty for shared subscribe");
            MQTT_API_UNLOCK(client);
            return ESP_FAIL;
        }
    }
    client->mqtt5_config->unsubscribe_property_info = property;
    MQTT_API_UNLOCK(client);
    return ESP_OK;
}

esp_err_t esp_mqtt5_client_set_disconnect_property(esp_mqtt5_client_handle_t client, const esp_mqtt5_disconnect_property_config_t *property)
{
    if (!client) {
        ESP_LOGE(TAG, "Client was not initialized");
        return ESP_ERR_INVALID_ARG;
    }
    MQTT_API_LOCK(client);

    /* Check protocol version */
    if (client->mqtt_state.connection.information.protocol_ver != MQTT_PROTOCOL_V_5) {
        ESP_LOGE(TAG, "MQTT protocol version is not v5");
        MQTT_API_UNLOCK(client);
        return ESP_FAIL;
    }
    if (property) {
        if (property->session_expiry_interval) {
            client->mqtt5_config->disconnect_property_info.session_expiry_interval = property->session_expiry_interval;
        }
        if (property->disconnect_reason) {
            client->mqtt5_config->disconnect_property_info.disconnect_reason = property->disconnect_reason;
        }
        if (property->user_property) {
            esp_mqtt5_client_delete_user_property(client->mqtt5_config->disconnect_property_info.user_property);
            client->mqtt5_config->disconnect_property_info.user_property = calloc(1, sizeof(struct mqtt5_user_property_list_t));
            ESP_MEM_CHECK(TAG, client->mqtt5_config->disconnect_property_info.user_property, {
                MQTT_API_UNLOCK(client);
                return ESP_ERR_NO_MEM;
            });
            STAILQ_INIT(client->mqtt5_config->disconnect_property_info.user_property);
            if (esp_mqtt5_user_property_copy(client->mqtt5_config->disconnect_property_info.user_property, property->user_property) != ESP_OK) {
                ESP_LOGE(TAG, "esp_mqtt5_user_property_copy fail");
                free(client->mqtt5_config->disconnect_property_info.user_property);
                client->mqtt5_config->disconnect_property_info.user_property = NULL;
                MQTT_API_UNLOCK(client);
                return ESP_FAIL;
            }
        }
    }

    MQTT_API_UNLOCK(client);
    return ESP_OK;
}

esp_err_t esp_mqtt5_client_set_connect_property(esp_mqtt5_client_handle_t client, const esp_mqtt5_connection_property_config_t *connect_property)
{
    if (!client) {
        ESP_LOGE(TAG, "Client was not initialized");
        return ESP_ERR_INVALID_ARG;
    }
    MQTT_API_LOCK(client);

    /* Check protocol version */
    if (client->mqtt_state.connection.information.protocol_ver != MQTT_PROTOCOL_V_5) {
        ESP_LOGE(TAG, "MQTT protocol version is not v5");
        MQTT_API_UNLOCK(client);
        return ESP_FAIL;
    }
    if (connect_property) {
        if (connect_property->session_expiry_interval) {
            client->mqtt5_config->connect_property_info.session_expiry_interval = connect_property->session_expiry_interval;
        }
        if (connect_property->maximum_packet_size) {
            if (connect_property->maximum_packet_size > client->mqtt_state.in_buffer_length) {
                ESP_LOGW(TAG, "Connect maximum_packet_size property is over buffer_size(%d), Please first change it", client->mqtt_state.in_buffer_length);
                MQTT_API_UNLOCK(client);
                return ESP_FAIL;
            } else {
                client->mqtt5_config->connect_property_info.maximum_packet_size = connect_property->maximum_packet_size;
            }
        } else {
            client->mqtt5_config->connect_property_info.maximum_packet_size = client->mqtt_state.in_buffer_length;
        }
        if (connect_property->receive_maximum) {
            client->mqtt5_config->connect_property_info.receive_maximum = connect_property->receive_maximum;
        }
        if (connect_property->topic_alias_maximum) {
            client->mqtt5_config->connect_property_info.topic_alias_maximum = connect_property->topic_alias_maximum;
            if (!client->mqtt5_config->peer_topic_alias) {
                client->mqtt5_config->peer_topic_alias = calloc(1, sizeof(struct mqtt5_topic_alias_list_t));
                ESP_MEM_CHECK(TAG, client->mqtt5_config->peer_topic_alias, goto _mqtt_set_config_failed);
                STAILQ_INIT(client->mqtt5_config->peer_topic_alias);
            }
        }
        if (connect_property->request_resp_info) {
            client->mqtt5_config->connect_property_info.request_resp_info = connect_property->request_resp_info;
        }
        if (connect_property->request_problem_info) {
            client->mqtt5_config->connect_property_info.request_problem_info = connect_property->request_problem_info;
        }
        if (connect_property->user_property) {
            esp_mqtt5_client_delete_user_property(client->mqtt5_config->connect_property_info.user_property);
            client->mqtt5_config->connect_property_info.user_property = calloc(1, sizeof(struct mqtt5_user_property_list_t));
            ESP_MEM_CHECK(TAG, client->mqtt5_config->connect_property_info.user_property, goto _mqtt_set_config_failed);
            STAILQ_INIT(client->mqtt5_config->connect_property_info.user_property);
            if (esp_mqtt5_user_property_copy(client->mqtt5_config->connect_property_info.user_property, connect_property->user_property) != ESP_OK) {
                ESP_LOGE(TAG, "esp_mqtt5_user_property_copy fail");
                goto _mqtt_set_config_failed;
            }
        }
        if (connect_property->payload_format_indicator) {
            client->mqtt5_config->will_property_info.payload_format_indicator = connect_property->payload_format_indicator;
        }
        if (connect_property->will_delay_interval) {
            client->mqtt5_config->will_property_info.will_delay_interval = connect_property->will_delay_interval;
        }
        if (connect_property->message_expiry_interval) {
            client->mqtt5_config->will_property_info.message_expiry_interval = connect_property->message_expiry_interval;
        }
        ESP_MEM_CHECK(TAG, esp_mqtt_set_if_config(connect_property->content_type, &client->mqtt5_config->will_property_info.content_type), goto _mqtt_set_config_failed);
        ESP_MEM_CHECK(TAG, esp_mqtt_set_if_config(connect_property->response_topic, &client->mqtt5_config->will_property_info.response_topic), goto _mqtt_set_config_failed);
        if (connect_property->correlation_data && connect_property->correlation_data_len) {
            free(client->mqtt5_config->will_property_info.correlation_data);
            client->mqtt5_config->will_property_info.correlation_data = malloc(connect_property->correlation_data_len);
            ESP_MEM_CHECK(TAG, client->mqtt5_config->will_property_info.correlation_data, goto _mqtt_set_config_failed);
            memcpy(client->mqtt5_config->will_property_info.correlation_data, connect_property->correlation_data, connect_property->correlation_data_len);
            client->mqtt5_config->will_property_info.correlation_data_len = connect_property->correlation_data_len;
        }
        if (connect_property->will_user_property) {
            esp_mqtt5_client_delete_user_property(client->mqtt5_config->will_property_info.user_property);
            client->mqtt5_config->will_property_info.user_property = calloc(1, sizeof(struct mqtt5_user_property_list_t));
            ESP_MEM_CHECK(TAG, client->mqtt5_config->will_property_info.user_property, goto _mqtt_set_config_failed);
            STAILQ_INIT(client->mqtt5_config->will_property_info.user_property);
            if (esp_mqtt5_user_property_copy(client->mqtt5_config->will_property_info.user_property, connect_property->will_user_property) != ESP_OK) {
                ESP_LOGE(TAG, "esp_mqtt5_user_property_copy fail");
                goto _mqtt_set_config_failed;
            }
        }
    }
    MQTT_API_UNLOCK(client);
    return ESP_OK;
_mqtt_set_config_failed:
    esp_mqtt_destroy_config(client);
    MQTT_API_UNLOCK(client);
    return ESP_ERR_NO_MEM;
}

esp_err_t esp_mqtt5_client_set_user_property(mqtt5_user_property_handle_t *user_property, esp_mqtt5_user_property_item_t item[], uint8_t item_num)
{
    if (!item_num || !item) {
        ESP_LOGE(TAG, "Input value is NULL");
        return ESP_FAIL;
    }

    if (!*user_property) {
        *user_property = calloc(1, sizeof(struct mqtt5_user_property_list_t));
        ESP_MEM_CHECK(TAG, *user_property, return ESP_ERR_NO_MEM);
        STAILQ_INIT(*user_property);
    }

    for (int i = 0; i < item_num; i ++) {
        if (item[i].key && item[i].value) {
            mqtt5_user_property_item_t user_property_item = calloc(1, sizeof(mqtt5_user_property_t));
            ESP_MEM_CHECK(TAG, user_property_item, goto err);
            size_t key_len = strlen(item[i].key);
            size_t value_len = strlen(item[i].value);

            user_property_item->key = calloc(1, key_len + 1);
            ESP_MEM_CHECK(TAG, user_property_item->key, {
                free(user_property_item);
                goto err;
            });
            memcpy(user_property_item->key, item[i].key, key_len);
            user_property_item->key[key_len] = '\0';

            user_property_item->value = calloc(1, value_len + 1);
            ESP_MEM_CHECK(TAG, user_property_item->value, {
                free(user_property_item->key);
                free(user_property_item);
                goto err;
            });
            memcpy(user_property_item->value, item[i].value, value_len);
            user_property_item->value[value_len] = '\0';

            STAILQ_INSERT_TAIL(*user_property, user_property_item, next);
        }
    }
    return ESP_OK;
err:
    esp_mqtt5_client_delete_user_property(*user_property);
    *user_property = NULL;
    return ESP_ERR_NO_MEM;
}

esp_err_t esp_mqtt5_client_get_user_property(mqtt5_user_property_handle_t user_property, esp_mqtt5_user_property_item_t *item, uint8_t *item_num)
{
    int i = 0, j = 0;
    if (user_property && item && *item_num) {
        mqtt5_user_property_item_t user_property_item;
        uint8_t num = *item_num;
        STAILQ_FOREACH(user_property_item, user_property, next) {
            if (i < num) {
                size_t item_key_len = strlen(user_property_item->key);
                size_t item_value_len = strlen(user_property_item->value);
                char *key = calloc(1, item_key_len + 1);
                ESP_MEM_CHECK(TAG, key, goto err);
                memcpy(key, user_property_item->key, item_key_len);
                key[item_key_len] = '\0';
                char *value = calloc(1, item_value_len + 1);
                ESP_MEM_CHECK(TAG, value, {
                    free(key);
                    goto err;
                });
                memcpy(value, user_property_item->value, item_value_len);
                value[item_value_len] = '\0';
                item[i].key = key;
                item[i].value = value;
                i ++;
            } else {
                break;
            }
        }
        *item_num = i;
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Input value is NULL or item_num is 0");
        return ESP_FAIL;
    }
err:
    for (j = 0; j < i; j ++) {
        if (item[j].key) {
            free((char *)item[j].key);
        }
        if (item[j].value) {
            free((char *)item[j].value);
        }
    }
    return ESP_ERR_NO_MEM;
}

uint8_t esp_mqtt5_client_get_user_property_count(mqtt5_user_property_handle_t user_property)
{
    uint8_t count = 0;
    if (user_property) {
        mqtt5_user_property_item_t item;
        STAILQ_FOREACH(item, user_property, next) {
            count ++;
        }
    }
    return count;
}

void esp_mqtt5_client_delete_user_property(mqtt5_user_property_handle_t user_property)
{
    if (user_property) {
        mqtt5_user_property_item_t item, tmp;
        STAILQ_FOREACH_SAFE(item, user_property, next, tmp) {
            STAILQ_REMOVE(user_property, item, mqtt5_user_property, next);
            free(item->key);
            free(item->value);
            free(item);
        }
    }
    free(user_property);
}
