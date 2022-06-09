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
#include "mqtt_client_internal.h"

#include "esp_log.h"

static const char *TAG = "mqtt_client_handler";

#ifdef CONFIG_MQTT_PROTOCOL_50
static esp_err_t handle_common_properties(esp_mqtt_client_handle_t client, mqtt_properties_t* props)
{
    int prop_count;
    uint8_t* prop_buffer;
    int prop_buffer_len;

    // Reason string
    prop_count = 0;
    mqtt_property_get_buffer(props, REASON_STRING, &prop_buffer, &prop_buffer_len, &prop_count);
    if (prop_count) {
        if ((prop_count > 1)) {
            send_disconnect_msg(client, PROTOCOL_ERROR);
            esp_mqtt_abort_connection(client);
            return ESP_FAIL;
        }
        client->event.reason_string = (const char*)prop_buffer;
        client->event.reason_string_len = prop_buffer_len;
    } else {
        // Put default value
        client->event.reason_string = NULL;
        client->event.reason_string_len = 0;
    }

    // User props
    client->event.user_props_count = 0;
    do {
        char* prop_key;
        int prop_key_len;

        // Skip already read properties
        prop_count = client->event.user_props_count;

        mqtt_property_get_pair(props, USER_PROPERTY, &prop_key, &prop_key_len, (char**)&prop_buffer, &prop_buffer_len, &prop_count);
        if(prop_count > CONFIG_MQTT_PROPERTIES_MAX) {
            return ESP_FAIL;
        }

        if (prop_count) {
            client->event.user_props[client->event.user_props_count].key = (char*)prop_key;
            client->event.user_props[client->event.user_props_count].key_len = prop_key_len;
            client->event.user_props[client->event.user_props_count].value = (char*)prop_buffer;
            client->event.user_props[client->event.user_props_count].value_len = prop_buffer_len;
            client->event.user_props_count++;
        }
    } while (prop_count > 1);
    return ESP_OK;
}
#endif

esp_err_t handle_connack(esp_mqtt_client_handle_t client, int read_len)
{
    esp_err_t err;
    bool session_present;

#ifdef CONFIG_MQTT_PROTOCOL_50
    mqtt_properties_t props = { 0 };
    err = mqtt_msg_parse_connack(client->mqtt_state.in_buffer, read_len, 
        &session_present, 
        (int*)&client->event.reason_code, 
        &props
    );

    if (err == ESP_OK) {
        int prop_count;
        int value;
        uint8_t* prop_buffer;
        int prop_buffer_len;

        // Session expiring interval
        prop_count = 0;
        value = mqtt_property_get_number(&props, SESSION_EXPIRY_INTERVAL, &prop_count);
        if (prop_count) {
            if (prop_count > 1) {
                send_disconnect_msg(client, PROTOCOL_ERROR);
                esp_mqtt_abort_connection(client);
                return ESP_FAIL;
            }
            // If present, overrides the value sent on connect
            client->event.connected.session_expiring_interval = value;
        }

        // Receive maximum.
        // Maximum number of QOS 1 2 to server will accept
        prop_count = 0;
        value = mqtt_property_get_number(&props, RECEIVE_MAXIMUM, &prop_count);
        if (prop_count) {
            if ((prop_count > 1) || !value) {
                send_disconnect_msg(client, PROTOCOL_ERROR);
                esp_mqtt_abort_connection(client);
                return ESP_FAIL;
            }
            client->event.connected.server_receive_max = (uint16_t)value;
        } else {
            client->event.connected.server_receive_max = 65535;
        }

        // Maximum QOS. Restrict client to send up QOS up to 0 or 1
        prop_count = 0;
        value = mqtt_property_get_number(&props, MAXIMUM_QOS, &prop_count);
        if (prop_count) {
            if ((prop_count > 1) || (value > 1)) {
                send_disconnect_msg(client, PROTOCOL_ERROR);
                esp_mqtt_abort_connection(client);
                return ESP_FAIL;
            }
            client->event.connected.qos_max = value;
        } else {
            // Put default value
            client->event.connected.qos_max = 2;
        }

        // Retain available. Whether the server supports retain
        prop_count = 0;
        value = mqtt_property_get_number(&props, RETAIN_AVAILABLE, &prop_count);
        if (prop_count) {
            if ((prop_count > 1) || (value > 1)) {
                send_disconnect_msg(client, PROTOCOL_ERROR);
                esp_mqtt_abort_connection(client);
                return ESP_FAIL;
            }
            client->event.connected.has_retain = (value != 0);
        } else {
            client->event.connected.has_retain = true;
        }

        // Maximum allowed packet size
        prop_count = 0;
        value = mqtt_property_get_number(&props, MAXIMUM_PACKET_SIZE, &prop_count);
        if (prop_count) {
            if ((prop_count > 1) || (!value)) {
                send_disconnect_msg(client, PROTOCOL_ERROR);
                esp_mqtt_abort_connection(client);
                return ESP_FAIL;
            }
            client->event.connected.packet_size_max = value;
        } else {
            // Put default value
            client->event.connected.packet_size_max = 0; // No limit
        }

        // Assigned client identifier. Assigned by the server when no client id is provided
        prop_count = 0;
        mqtt_property_get_buffer(&props, ASSIGNED_CLIENT_IDENTIFER, &prop_buffer, &prop_buffer_len, &prop_count);
        if (prop_count) {
            if ((prop_count > 1)) {
                send_disconnect_msg(client, PROTOCOL_ERROR);
                esp_mqtt_abort_connection(client);
                return ESP_FAIL;
            }
            client->event.connected.assigned_client_id = (const char*)prop_buffer;
            client->event.connected.assigned_client_id_len = prop_buffer_len;
        } else {
            // Put default value
            client->event.connected.assigned_client_id = NULL;
            client->event.connected.assigned_client_id_len = 0;
        }

        // Topic alias maximum. Highest value accepted by server
        prop_count = 0;
        value = mqtt_property_get_number(&props, TOPIC_ALIAS_MAXIMUM, &prop_count);
        if (prop_count) {
            if ((prop_count > 1)) {
                send_disconnect_msg(client, PROTOCOL_ERROR);
                esp_mqtt_abort_connection(client);
                return ESP_FAIL;
            }
            client->event.connected.topic_alias_max = value;
        } else {
            // Put default value
            client->event.connected.topic_alias_max = 0;
        }

        // Reason string and user props
        err = handle_common_properties(client, &props);
        if(err) {
            return err;
        }

        // Wildcard subscription available
        prop_count = 0;
        value = mqtt_property_get_number(&props, WILDCARD_SUBSCRIPTION_AVAILABLE, &prop_count);
        if (prop_count) {
            if ((prop_count > 1) || (value > 1)) {
                send_disconnect_msg(client, PROTOCOL_ERROR);
                esp_mqtt_abort_connection(client);
                return ESP_FAIL;
            }
            client->event.connected.has_wildcard_subscription = (value != 0);
        } else {
            client->event.connected.has_wildcard_subscription = true;
        }

        // Subscription identifiers available
        prop_count = 0;
        value = mqtt_property_get_number(&props, SUBSCRIPTION_IDENTIFIER_AVAILABLE, &prop_count);
        if (prop_count) {
            if ((prop_count > 1) || (value > 1)) {
                send_disconnect_msg(client, PROTOCOL_ERROR);
                esp_mqtt_abort_connection(client);
                return ESP_FAIL;
            }
            client->event.connected.has_subscription_identifier = (value != 0);
        } else {
            client->event.connected.has_subscription_identifier = true;
        }

        // Shared subscription available
        prop_count = 0;
        value = mqtt_property_get_number(&props, SHARED_SUBSCRIPTION_AVAILABLE, &prop_count);
        if (prop_count) {
            if ((prop_count > 1) || (value > 1)) {
                send_disconnect_msg(client, PROTOCOL_ERROR);
                esp_mqtt_abort_connection(client);
                return ESP_FAIL;
            }
            client->event.connected.has_shared_subscription = (value != 0);
        } else {
            client->event.connected.has_shared_subscription = true;
        }

        // Server keep alive. Overwrites client keepalive
        prop_count = 0;
        value = mqtt_property_get_number(&props, SERVER_KEEP_ALIVE, &prop_count);
        if (prop_count) {
            if ((prop_count > 1)) {
                send_disconnect_msg(client, PROTOCOL_ERROR);
                esp_mqtt_abort_connection(client);
                return ESP_FAIL;
            }
            client->event.connected.server_keep_alive = value;
        } else {
            // Put default value
            client->event.connected.server_keep_alive = client->connect_info.keepalive;
        }

        // Response information
        prop_count = 0;
        mqtt_property_get_buffer(&props, RESPONSE_INFORMATION, &prop_buffer, &prop_buffer_len, &prop_count);
        if (prop_count) {
            if ((prop_count > 1)) {
                send_disconnect_msg(client, PROTOCOL_ERROR);
                esp_mqtt_abort_connection(client);
                return ESP_FAIL;
            }
            client->event.connected.response_information = (const char*)prop_buffer;
            client->event.connected.response_information_len = prop_buffer_len;
        } else {
            // Put default value
            client->event.connected.response_information = NULL;
            client->event.connected.response_information_len = 0;
        }

        // Server reference. String to redirect client
        prop_count = 0;
        mqtt_property_get_buffer(&props, SERVER_REFERENCE, &prop_buffer, &prop_buffer_len, &prop_count);
        if (prop_count) {
            if ((prop_count > 1)) {
                send_disconnect_msg(client, PROTOCOL_ERROR);
                esp_mqtt_abort_connection(client);
                return ESP_FAIL;
            }
            client->event.connected.server_reference = (const char*)prop_buffer;
            client->event.connected.server_reference_len = prop_buffer_len;
        } else {
            // Put default value
            client->event.connected.server_reference = NULL;
            client->event.connected.server_reference_len = 0;
        }

        // Authentication method
        prop_count = 0;
        mqtt_property_get_buffer(&props, AUTHENTICATION_METHOD, &prop_buffer, &prop_buffer_len, &prop_count);
        if (prop_count) {
            if ((prop_count > 1)) {
                send_disconnect_msg(client, PROTOCOL_ERROR);
                esp_mqtt_abort_connection(client);
                return ESP_FAIL;
            }
            client->event.connected.authentication_method = (const char*)prop_buffer;
            client->event.connected.authentication_method_len = prop_buffer_len;
        } else {
            // Put default value
            client->event.connected.authentication_method = NULL;
            client->event.connected.authentication_method_len = 0;
        }

        // Authentication data
        prop_count = 0;
        mqtt_property_get_buffer(&props, AUTHENTICATION_DATA, &prop_buffer, &prop_buffer_len, &prop_count);
        if (prop_count) {
            if ((prop_count > 1)) {
                send_disconnect_msg(client, PROTOCOL_ERROR);
                esp_mqtt_abort_connection(client);
                return ESP_FAIL;
            }
            client->event.connected.authentication_data = prop_buffer;
            client->event.connected.authentication_data_len = prop_buffer_len;
        } else {
            // Put default value
            client->event.connected.authentication_data = NULL;
            client->event.connected.authentication_data_len = 0;
        }
    }
#else
    err = mqtt_msg_parse_connack(client->mqtt_state.in_buffer, read_len, &session_present, (int*)&client->event.reason_code);
#endif
    client->mqtt_state.in_buffer_read_len = 0;

    if (err == ESP_OK) {
        client->event.event_id = MQTT_EVENT_CONNECTED;
        client->event.connected.session_present = session_present;
        client->state = MQTT_STATE_CONNECTED;
        esp_mqtt_dispatch_event(client);
        client->refresh_connection_tick = platform_tick_get_ms();
        client->keepalive_tick = platform_tick_get_ms();
    } else {
        /* propagate event with connection refused error */
        client->event.event_id = MQTT_EVENT_ERROR;
        client->event.error_handle.error_type = MQTT_ERROR_TYPE_CONNECTION_REFUSED;
        client->event.error_handle.esp_tls_stack_err = 0;
        client->event.error_handle.esp_tls_last_esp_err = 0;
        client->event.error_handle.esp_tls_cert_verify_flags = 0;
        esp_mqtt_dispatch_event(client);
    }
    return err;
}

static bool is_valid_mqtt_msg(esp_mqtt_client_handle_t client, int msg_type, int msg_id)
{
    ESP_LOGD(TAG, "pending_id=%d, pending_msg_count = %d", client->mqtt_state.pending_msg_id, client->mqtt_state.pending_msg_count);
    if (client->mqtt_state.pending_msg_count == 0) {
        return false;
    }
    if (outbox_delete(client->outbox, msg_id, msg_type) == ESP_OK) {
        client->mqtt_state.pending_msg_count --;
        return true;
    }

    return false;
}

esp_err_t handle_suback(esp_mqtt_client_handle_t client, uint8_t* recv_buf, int recv_len)
{
    esp_err_t err;
    int msg_id;
#ifdef CONFIG_MQTT_PROTOCOL_50
    mqtt_properties_t props = { 0 };
    err = mqtt_msg_parse_suback(recv_buf, recv_len,
        &msg_id,
        &props,
        CONFIG_MQTT_SIMUL_TOPICS_MAX,
        &client->event.subunsubscribed.reason_codes_count,
        client->event.subunsubscribed.reason_codes_list);

    if(err == ESP_OK) {
        // Reason string and user props
        err = handle_common_properties(client, &props);
    }
#else
    err = mqtt_msg_parse_suback(recv_buf, recv_len,
        &msg_id,
        CONFIG_MQTT_SIMUL_TOPICS_MAX,
        &client->event.subunsubscribed.qos_count,
        client->event.subunsubscribed.qos_list);
#endif

    if (err) {
        ESP_LOGE(TAG, "Failed to acquire suback data");
        return ESP_FAIL;
    }

    if (is_valid_mqtt_msg(client, MQTT_MSG_TYPE_SUBSCRIBE, msg_id)) {
        ESP_LOGD(TAG, "handle_suback, message_length_read=%zu, message_length=%zu", client->mqtt_state.in_buffer_read_len, client->mqtt_state.message_length);
        // post data event
        client->event.event_id = MQTT_EVENT_SUBSCRIBED;
        // Reason codes and properties already set above
        client->event.msg_id = msg_id;
        esp_mqtt_dispatch_event(client);
    }
    return ESP_OK;
}

esp_err_t handle_unsuback(esp_mqtt_client_handle_t client, uint8_t* recv_buf, int recv_len)
{
    esp_err_t err;
    int msg_id;
#ifdef CONFIG_MQTT_PROTOCOL_50
    mqtt_properties_t props = { 0 };
    err = mqtt_msg_parse_unsuback(recv_buf, recv_len,
        &msg_id, 
        &props, 
        CONFIG_MQTT_SIMUL_TOPICS_MAX,
        &client->event.subunsubscribed.reason_codes_count, 
        client->event.subunsubscribed.reason_codes_list);

    if(err == ESP_OK) {
        // Reason string and user props
        err = handle_common_properties(client, &props);
    }
#else
    err = mqtt_msg_parse_unsuback(recv_buf, recv_len, &msg_id);
#endif
    if (err) {
        ESP_LOGE(TAG, "Failed to acquire unsuback data");
        return ESP_FAIL;
    }

    if (is_valid_mqtt_msg(client, MQTT_MSG_TYPE_UNSUBSCRIBE, msg_id)) {
        ESP_LOGD(TAG, "UnSubscribe successful");

        // Reason codes and properties already filled
        client->event.event_id = MQTT_EVENT_UNSUBSCRIBED;
        client->event.msg_id = msg_id;
        esp_mqtt_dispatch_event(client);
    }
    return ESP_OK;
}

esp_err_t handle_puback(esp_mqtt_client_handle_t client, uint8_t* recv_buf, int recv_len)
{
    esp_err_t err;
    int msg_id;
    int unused_type; // was already checked outside

#ifdef CONFIG_MQTT_PROTOCOL_50
    mqtt_properties_t props = { 0 };
    err = mqtt_msg_parse_ack(recv_buf, recv_len,
        &unused_type, 
        &client->event.dup, 
        &msg_id,
        (int*)&client->event.reason_code, 
        &props);

    if(err == ESP_OK) {
        // Reason string and user props
        err = handle_common_properties(client, &props);
    }

#else
    err = mqtt_msg_parse_ack(recv_buf, recv_len,
        &unused_type, &client->event.dup, &msg_id);
#endif

    if (err) {
        ESP_LOGE(TAG, "Failed to acquire puback data");
        return ESP_FAIL;
    }

    if (is_valid_mqtt_msg(client, MQTT_MSG_TYPE_PUBLISH, msg_id)) {
        ESP_LOGD(TAG, "received MQTT_MSG_TYPE_PUBACK, finish QoS1 publish");

        // Reason codes and properties already filled
        client->event.event_id = MQTT_EVENT_PUBLISHED;
        client->event.msg_id = msg_id;
        esp_mqtt_dispatch_event(client);
    }
    return ESP_OK;
}

esp_err_t handle_pubrec(esp_mqtt_client_handle_t client, uint8_t* recv_buf, int recv_len)
{
    esp_err_t err;
    int msg_id;
    int unused_type; // was already checked outside

    ESP_LOGD(TAG, "received MQTT_MSG_TYPE_PUBREC");

#ifdef CONFIG_MQTT_PROTOCOL_50
    mqtt_properties_t props = { 0 };
    err = mqtt_msg_parse_ack(recv_buf, recv_len,
        &unused_type, 
        &client->event.dup, 
        &msg_id,
        (int*)&client->event.reason_code, 
        &props);

    if(err == ESP_OK) {
        // Reason string and user props
        err = handle_common_properties(client, &props);
        if(err) {
            return err;
        }
    }
#else
    err = mqtt_msg_parse_ack(recv_buf, recv_len,
        &unused_type, &client->event.dup, &msg_id);
#endif

    if (err) {
        ESP_LOGE(TAG, "Failed to acquire pubrec data");
        return ESP_FAIL;
    }

#ifdef CONFIG_MQTT_PROTOCOL_50
    // TODO
    // TODO: Check msg_id and report reason code if not found!
    // TODO
    // Reason string
    // User props
    client->mqtt_state.outbound_message = mqtt_msg_pubrel(&client->mqtt_state.mqtt_connection, 0, msg_id, SUCCESS, NULL);
#else
    client->mqtt_state.outbound_message = mqtt_msg_pubrel(&client->mqtt_state.mqtt_connection, 0, msg_id);
#endif
    if (client->mqtt_state.outbound_message->length == 0) {
        ESP_LOGE(TAG, "Publish response message PUBREL cannot be created");
        return ESP_FAIL;
    }

    outbox_set_pending(client->outbox, msg_id, ACKNOWLEDGED);
    mqtt_write_data(client);
    return ESP_OK;
}

esp_err_t handle_pubrel(esp_mqtt_client_handle_t client, uint8_t* recv_buf, int recv_len)
{
    esp_err_t err;
    int msg_id;
    int unused_type; // was already checked outside

    ESP_LOGD(TAG, "received MQTT_MSG_TYPE_PUBREL");

#ifdef CONFIG_MQTT_PROTOCOL_50
    mqtt_properties_t props = { 0 };
    err = mqtt_msg_parse_ack(recv_buf, recv_len,
        &unused_type, 
        &client->event.dup, 
        &msg_id,
        (int*)&client->event.reason_code, 
        &props);

    if(err == ESP_OK) {
        // Reason string and user props
        err = handle_common_properties(client, &props);
    }
#else
    err = mqtt_msg_parse_ack(recv_buf, recv_len,
        &unused_type, &client->event.dup, &msg_id);
#endif

    if (err) {
        ESP_LOGE(TAG, "Failed to acquire pubrel data");
        return ESP_FAIL;
    }

#ifdef CONFIG_MQTT_PROTOCOL_50
    // TODO
    // TODO: Check msg_id and report reason code if not found!
    // TODO
    // Properties are used only in case reason code is not success.
    client->mqtt_state.outbound_message = mqtt_msg_pubcomp(&client->mqtt_state.mqtt_connection, msg_id, SUCCESS, NULL);
#else
    client->mqtt_state.outbound_message = mqtt_msg_pubcomp(&client->mqtt_state.mqtt_connection, msg_id);
#endif
    if (client->mqtt_state.outbound_message->length == 0) {
        ESP_LOGE(TAG, "Publish response message PUBCOMP cannot be created");
        return ESP_FAIL;
    }

    mqtt_write_data(client);
    return ESP_OK;
}

esp_err_t handle_pubcomp(esp_mqtt_client_handle_t client, uint8_t* recv_buf, int recv_len)
{
    esp_err_t err;
    int msg_id;
    int unused_type; // was already checked outside

    ESP_LOGD(TAG, "received MQTT_MSG_TYPE_PUBCOMP");

#ifdef CONFIG_MQTT_PROTOCOL_50   
    mqtt_properties_t props = { 0 };
    err = mqtt_msg_parse_ack(recv_buf, recv_len,
        &unused_type,
        &client->event.dup,
        &msg_id,
        (int*)&client->event.reason_code,
        &props);

    if(err == ESP_OK) {
        // Reason string and user props
        err = handle_common_properties(client, &props);
    }

#else
    err = mqtt_msg_parse_ack(recv_buf, recv_len,
        &unused_type, &client->event.dup, &msg_id);
#endif

    if (err) {
        ESP_LOGE(TAG, "Failed to acquire pubcomp data");
        return ESP_FAIL;
    }

    if (is_valid_mqtt_msg(client, MQTT_MSG_TYPE_PUBLISH, msg_id)) {
        ESP_LOGD(TAG, "Receive MQTT_MSG_TYPE_PUBCOMP, finish QoS2 publish");

        // Reason codes and properties already filled
        client->event.event_id = MQTT_EVENT_PUBLISHED;
        client->event.msg_id = msg_id;
        esp_mqtt_dispatch_event(client);
    }
    return ESP_OK;
}

esp_err_t handle_publish(esp_mqtt_client_handle_t client)
{
    esp_err_t err;
    uint8_t *recv_buf = client->mqtt_state.in_buffer;
    size_t recv_len = client->mqtt_state.in_buffer_length;  // Size of the fixed buffer
    size_t total_len = client->mqtt_state.message_length;   // Total length of the received message

    ESP_LOGD(TAG, "handle_publish, message_length_read=%zu, message_length=%zu", 
        client->mqtt_state.in_buffer_read_len, total_len);
    
    if( total_len < recv_len ) {
        // If the packet is shorter than the maximum size of the fixed buffer, we resize the recv_len
        recv_len = total_len;
    }

#ifdef CONFIG_MQTT_PROTOCOL_50
    mqtt_properties_t props = { 0 };
    err = mqtt_msg_parse_publish(recv_buf, recv_len,
        &client->event.dup,
        &client->event.publish_data.qos,
        &client->event.publish_data.retain,
        &client->event.msg_id,
        &client->event.publish_data.topic,
        &client->event.publish_data.topic_len,
        &props,
        (uint8_t **)&client->event.publish_data.data,
        &client->event.publish_data.data_len
        );

    if(err == ESP_OK) {
        int prop_count;
        int value;
        uint8_t* prop_buffer;
        int prop_buffer_len;

        // Payload format indicator. Optional
        prop_count = 0;
        value = mqtt_property_get_number(&props, PAYLOAD_FORMAT_INDICATOR, &prop_count);
        if (prop_count) {
            if ((prop_count > 1) || (value > 1)) {
                send_disconnect_msg(client, PROTOCOL_ERROR);
                esp_mqtt_abort_connection(client);
                return ESP_FAIL;
            }
            client->event.publish_data.payload_format_indicator = value;
        } else {
            // Put default value
            client->event.publish_data.payload_format_indicator = 0;
        }

        // Message expiry interval. Validity before the packet is dropped by the broker.
        // Ignored by receiver
        prop_count = 0;
        value = mqtt_property_get_number(&props, MESSAGE_EXPIRY_INTERVAL, &prop_count);
        if (prop_count) {
            if ((prop_count > 1)) {
                send_disconnect_msg(client, PROTOCOL_ERROR);
                esp_mqtt_abort_connection(client);
                return ESP_FAIL;
            }
            client->event.publish_data.message_expiry_interval = value;
        } else {
            // Put default value
            client->event.publish_data.message_expiry_interval = -1; // Do not expire
        }

        // Topic alias. Maps topic name to topic alias value
        prop_count = 0;
        value = mqtt_property_get_number(&props, TOPIC_ALIAS, &prop_count);
        if (prop_count) {
            if ((prop_count > 1) || !value) {
                send_disconnect_msg(client, PROTOCOL_ERROR);
                esp_mqtt_abort_connection(client);
                return ESP_FAIL;
            }
            client->event.publish_data.topic_alias = value;
        } else {
            // Put default value
            client->event.publish_data.topic_alias = 0; // No alias
        }

        // Response topic.
        prop_count = 0;
        mqtt_property_get_buffer(&props, RESPONSE_TOPIC, &prop_buffer, &prop_buffer_len, &prop_count);
        if (prop_count) {
            if ((prop_count > 1)) {
                send_disconnect_msg(client, PROTOCOL_ERROR);
                esp_mqtt_abort_connection(client);
                return ESP_FAIL;
            }
            client->event.publish_data.response_topic = (const char*)prop_buffer;
            client->event.publish_data.response_topic_len = prop_buffer_len;
        } else {
            // Put default value
            client->event.publish_data.response_topic = NULL;
            client->event.publish_data.response_topic_len = 0;
        }

        // Correlation data
        prop_count = 0;
        mqtt_property_get_buffer(&props, CORRELATION_DATA, &prop_buffer, &prop_buffer_len, &prop_count);
        if (prop_count) {
            if ((prop_count > 1)) {
                send_disconnect_msg(client, PROTOCOL_ERROR);
                esp_mqtt_abort_connection(client);
                return ESP_FAIL;
            }
            client->event.publish_data.correlation_data = prop_buffer;
            client->event.publish_data.correlation_data_len = prop_buffer_len;
        } else {
            // Put default value
            client->event.publish_data.correlation_data = NULL;
            client->event.publish_data.correlation_data_len = 0;
        }

        // Reason string and user props
        err = handle_common_properties(client, &props);
        if(err) {
            return err;
        }

        // Subscription identifier
        client->event.publish_data.subscription_ids_count = 0;
        do {
            prop_count = client->event.publish_data.subscription_ids_count;
            value = mqtt_property_get_number(&props, SUBSCRIPTION_IDENTIFIER, &prop_count);
            if (prop_count) {
                if (!value) {
                    send_disconnect_msg(client, PROTOCOL_ERROR);
                    esp_mqtt_abort_connection(client);
                    return ESP_FAIL;
                }
                client->event.publish_data.subscription_ids[client->event.publish_data.subscription_ids_count] = value;
                client->event.publish_data.subscription_ids_count++;
            }
        } while (prop_count > 1);

        // Content type
        prop_count = 0;
        mqtt_property_get_buffer(&props, CONTENT_TYPE, &prop_buffer, &prop_buffer_len, &prop_count);
        if (prop_count) {
            if ((prop_count > 1)) {
                send_disconnect_msg(client, PROTOCOL_ERROR);
                esp_mqtt_abort_connection(client);
                return ESP_FAIL;
            }
            client->event.publish_data.content_type = (const char*)prop_buffer;
            client->event.publish_data.content_type_len = prop_buffer_len;
        } else {
            // Put default value
            client->event.publish_data.content_type = NULL;
            client->event.publish_data.content_type_len = 0;
        }
    }

#else
    err = mqtt_msg_parse_publish(recv_buf, recv_len,
        &client->event.dup,
        &client->event.publish_data.qos,
        &client->event.publish_data.retain,
        &client->event.msg_id,
        &client->event.publish_data.topic,
        &client->event.publish_data.topic_len,
        (uint8_t **)&client->event.publish_data.data,
        &client->event.publish_data.data_len
        );
#endif

    if (err) {
        return ESP_FAIL;
    }

    int msg_id = client->event.msg_id;
    int msg_qos = client->event.publish_data.qos;

    // Compute the total data len => full packet - the header with other data 
    client->event.publish_data.total_data_len = total_len - recv_len + client->event.publish_data.data_len;

    // Offset in the fixed size buffer that was reached when reading the packet
    // size_t recv_buf_offset = client->mqtt_state.in_buffer_read_len;
    size_t msg_chunk_offset = 0;  // Offset in the chunk of data received in the event

    ESP_LOGD(TAG, "Get data len= %zu, topic len=%zu, total_data: %d offset: %zu", 
        client->event.publish_data.data_len, 
        client->event.publish_data.topic_len,
        client->event.publish_data.total_data_len,
        msg_chunk_offset);
    client->event.event_id = MQTT_EVENT_DATA;
    client->event.publish_data.current_data_offset = msg_chunk_offset;

    esp_mqtt_dispatch_event(client);

    int remaining_bytes = total_len - recv_len;

    if (remaining_bytes) {
        // Change the event to match data only packet
        client->event.publish_data.topic = NULL;
        client->event.publish_data.topic_len = 0;
    }

    while (remaining_bytes) {
        // We generate multiple data events while reading directly from the socket

        size_t chunk_size = client->mqtt_state.in_buffer_length;
        if(remaining_bytes < chunk_size) {
            chunk_size = remaining_bytes;
        }

        int ret = esp_transport_read(client->transport, 
                                    (char *)client->mqtt_state.in_buffer,
                                    chunk_size,
                                    client->config->network_timeout_ms);
        if (ret <= 0) {
            ESP_LOGE(TAG, "Read error or timeout: len_read=%d, errno=%d", ret, errno);
            return ESP_FAIL;
        }

        // Dispatch data only event
        client->event.publish_data.data_len = chunk_size;
        client->event.publish_data.current_data_offset += chunk_size;
        esp_mqtt_dispatch_event(client);

        remaining_bytes -= chunk_size;
        if(remaining_bytes < 0) {
            ESP_LOGE(TAG, "Wrong total len! Remaining bytes %d", remaining_bytes);
            return ESP_FAIL;
        }
    }

    // The entire message was sent out to events, let's go on and handle the possible answer
#ifdef CONFIG_MQTT_PROTOCOL_50
    // TODO: Go through reason codes and fill properties 
    // Reason string
    // User props
    if (msg_qos == 1) {
        client->mqtt_state.outbound_message = mqtt_msg_puback(&client->mqtt_state.mqtt_connection, msg_id, SUCCESS, NULL);
    } else if (msg_qos == 2) {
        client->mqtt_state.outbound_message = mqtt_msg_pubrec(&client->mqtt_state.mqtt_connection, msg_id, SUCCESS, NULL);
    }
#else
    if (msg_qos == 1) {
        client->mqtt_state.outbound_message = mqtt_msg_puback(&client->mqtt_state.mqtt_connection, msg_id);
    } else if (msg_qos == 2) {
        client->mqtt_state.outbound_message = mqtt_msg_pubrec(&client->mqtt_state.mqtt_connection, msg_id);
    }
#endif
    if (client->mqtt_state.outbound_message->length == 0) {
        ESP_LOGE(TAG, "Publish response message PUBACK or PUBREC cannot be created");
        return ESP_FAIL;
    }

    if (msg_qos == 1 || msg_qos == 2) {
        ESP_LOGD(TAG, "Queue response QoS: %d", msg_qos);

        if (mqtt_write_data(client) != ESP_OK) {
            ESP_LOGE(TAG, "Error write qos msg response, qos = %d", msg_qos);
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

