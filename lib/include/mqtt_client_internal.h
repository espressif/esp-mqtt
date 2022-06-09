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

#ifndef _MQTT_CLIENT_INTERNAL_H_
#define _MQTT_CLIENT_INTERNAL_H_

#include <stdio.h>
#include <stdlib.h>
#include "esp_err.h"
#include "platform.h"

#include "mqtt_client.h"
#include "mqtt_msg.h"
#include "mqtt_outbox.h"

#include "freertos/event_groups.h"
#include "esp_transport.h"

#include "mqtt_supported_features.h"


#ifdef __cplusplus
extern "C" {
#endif

typedef struct mqtt_state {
    uint8_t *in_buffer;
    uint8_t *out_buffer;
    int in_buffer_length;
    int out_buffer_length;
    size_t message_length;
    size_t in_buffer_read_len;
    mqtt_message_t *outbound_message;
    mqtt_connection_t mqtt_connection;
    uint16_t pending_msg_id;
    int pending_msg_type;
    int pending_publish_qos;
    int pending_msg_count;
} mqtt_state_t;

typedef struct {
    mqtt_event_callback_t event_handle;
    esp_event_loop_handle_t event_loop_handle;
    int task_stack;
    int task_prio;
    char *uri;
    char *host;
    char *path;
    char *scheme;
    int port;
    bool auto_reconnect;
    void *user_context;
    int network_timeout_ms;
    int refresh_connection_after_ms;
    int reconnect_timeout_ms;
    char **alpn_protos;
    int num_alpn_protos;
    char *clientkey_password;
    int clientkey_password_len;
    bool use_global_ca_store;
    esp_err_t ((*crt_bundle_attach)(void *conf));
    const char *cacert_buf;
    size_t cacert_bytes;
    const char *clientcert_buf;
    size_t clientcert_bytes;
    const char *clientkey_buf;
    size_t clientkey_bytes;
    const struct psk_key_hint *psk_hint_key;
    bool skip_cert_common_name_check;
    bool use_secure_element;
    void *ds_data;
    int message_retransmit_timeout;
#ifdef CONFIG_MQTT_PROTOCOL_50
    bool send_will_on_disconnect;
#endif
} mqtt_config_storage_t;

typedef enum {
    MQTT_STATE_INIT = 0,
    MQTT_STATE_DISCONNECTED,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_WAIT_RECONNECT,
} mqtt_client_state_t;

struct esp_mqtt_client {
    esp_transport_list_handle_t transport_list;
    esp_transport_handle_t transport;
    mqtt_config_storage_t *config;
    mqtt_state_t  mqtt_state;
    mqtt_connect_info_t connect_info;
    mqtt_client_state_t state;
    uint64_t refresh_connection_tick;
    int64_t keepalive_tick;
    uint64_t reconnect_tick;
    int wait_timeout_ms;
    int auto_reconnect;
    esp_mqtt_event_t event;
    bool run;
    bool wait_for_ping_resp;
    outbox_handle_t outbox;
    EventGroupHandle_t status_bits;
    SemaphoreHandle_t  api_lock;
    TaskHandle_t       task_handle;
};

esp_err_t handle_connack(esp_mqtt_client_handle_t client, int read_len);
esp_err_t handle_suback(esp_mqtt_client_handle_t client, uint8_t* recv_buf, int recv_len);
esp_err_t handle_unsuback(esp_mqtt_client_handle_t client, uint8_t* recv_buf, int recv_len);
esp_err_t handle_puback(esp_mqtt_client_handle_t client, uint8_t* recv_buf, int recv_len);
esp_err_t handle_pubrec(esp_mqtt_client_handle_t client, uint8_t* recv_buf, int recv_len);
esp_err_t handle_pubrel(esp_mqtt_client_handle_t client, uint8_t* recv_buf, int recv_len);
esp_err_t handle_pubcomp(esp_mqtt_client_handle_t client, uint8_t* recv_buf, int recv_len);
esp_err_t handle_publish(esp_mqtt_client_handle_t client);

esp_err_t esp_mqtt_dispatch_event(esp_mqtt_client_handle_t client);
esp_err_t mqtt_write_data(esp_mqtt_client_handle_t client);
#ifdef CONFIG_MQTT_PROTOCOL_50
esp_err_t send_disconnect_msg(esp_mqtt_client_handle_t client, int reason);
#else
esp_err_t send_disconnect_msg(esp_mqtt_client_handle_t client);
#endif
void esp_mqtt_abort_connection(esp_mqtt_client_handle_t client);

#ifdef __cplusplus
}
#endif //__cplusplus


#endif