/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _MQTT_CLIENT_PRIV_H_
#define _MQTT_CLIENT_PRIV_H_

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include "esp_err.h"
#include "platform.h"

#include "esp_event.h"
#include "mqtt_client.h"
#include "mqtt_msg.h"
#ifdef MQTT_PROTOCOL_5
#include "mqtt5_client_priv.h"
#endif
#include "esp_transport.h"
#include "esp_transport_tcp.h"
#include "esp_transport_ssl.h"
#include "esp_transport_ws.h"
#include "esp_log.h"
#include "mqtt_outbox.h"
#include "freertos/event_groups.h"
#include <errno.h>
#include <string.h>

#include "mqtt_supported_features.h"

/* using uri parser */
#include "http_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_NEWLIB_NANO_FORMAT
#define NEWLIB_NANO_COMPAT_FORMAT            PRIu32
#define NEWLIB_NANO_COMPAT_CAST(size_t_var)  (uint32_t)size_t_var
#else
#define NEWLIB_NANO_COMPAT_FORMAT            "zu"
#define NEWLIB_NANO_COMPAT_CAST(size_t_var)  size_t_var
#endif

#ifdef MQTT_DISABLE_API_LOCKS
# define MQTT_API_LOCK(c)
# define MQTT_API_UNLOCK(c)
#else
# define MQTT_API_LOCK(c)          xSemaphoreTakeRecursive(c->api_lock, portMAX_DELAY)
# define MQTT_API_UNLOCK(c)        xSemaphoreGiveRecursive(c->api_lock)
#endif /* MQTT_USE_API_LOCKS */

typedef struct mqtt_state {
    uint8_t *in_buffer;
    int in_buffer_length;
    size_t message_length;
    size_t in_buffer_read_len;
    mqtt_connection_t connection;
    uint16_t pending_msg_id;
    int pending_msg_type;
    int pending_publish_qos;
} mqtt_state_t;

typedef struct {
    esp_event_loop_handle_t event_loop_handle;
    int task_stack;
    int task_prio;
    char *uri;
    char *host;
    char *path;
    char *scheme;
    int port;
    bool auto_reconnect;
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
    const char *common_name;
    bool use_secure_element;
    void *ds_data;
    int message_retransmit_timeout;
    uint64_t outbox_limit;
    esp_transport_handle_t transport;
    struct ifreq * if_name;
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
    mqtt_client_state_t state;
    uint64_t refresh_connection_tick;
    int64_t keepalive_tick;
    uint64_t reconnect_tick;
#ifdef MQTT_PROTOCOL_5
    mqtt5_config_storage_t *mqtt5_config;
    uint16_t send_publish_packet_count; // This is for MQTT v5.0 flow control
#endif
    int wait_timeout_ms;
    int auto_reconnect;
    esp_mqtt_event_t event;
    bool run;
    bool wait_for_ping_resp;
    outbox_handle_t outbox;
    EventGroupHandle_t status_bits;
    SemaphoreHandle_t  api_lock;
    TaskHandle_t       task_handle;
#if MQTT_EVENT_QUEUE_SIZE > 1
    atomic_int         queued_events;
#endif
};

bool esp_mqtt_set_if_config(char const *const new_config, char **old_config);
void esp_mqtt_destroy_config(esp_mqtt_client_handle_t client);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif
