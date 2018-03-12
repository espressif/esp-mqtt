/*
 * This file is subject to the terms and conditions defined in
 * file 'LICENSE', which is part of this source code package.
 * Tuan PM <tuanpm at live dot com>
 */

#ifndef _WEBSOCKET_CLIENT_H_
#define _WEBSOCKET_CLIENT_H_

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "mqtt_config.h"

typedef struct esp_websocket_client* esp_websocket_client_handle_t;

typedef enum {
    WEBSOCKET_EVENT_ERROR = 0,
    WEBSOCKET_EVENT_CONNECTED,
    WEBSOCKET_EVENT_DISCONNECTED,
    WEBSOCKET_EVENT_DATA,
} esp_websocket_event_id_t;

typedef enum {
    WEBSOCKET_TRANSPORT_UNKNOWN = 0x0,
    WEBSOCKET_TRANSPORT_OVER_TCP,
    WEBSOCKET_TRANSPORT_OVER_SSL,
} esp_websocket_transport_t;

typedef struct {
    esp_websocket_event_id_t event_id;
    esp_websocket_client_handle_t client;
    void *user_context;
    char *data;
    int data_len;
} esp_websocket_event_t;

typedef esp_websocket_event_t* esp_websocket_event_handle_t;

typedef esp_err_t (* websocket_event_callback_t)(esp_websocket_event_handle_t event);


typedef struct {
    websocket_event_callback_t event_handle;
    const char *uri;
    const char *scheme;
    const char *host;
    int port;
    const char *username;
    const char *password;
    const char *path;
    bool disable_auto_reconnect;
    void *user_context;
    int task_prio;
    int task_stack;
    int buffer_size;
    const char *cert_pem;
    esp_websocket_transport_t transport;
} esp_websocket_client_config_t;

esp_websocket_client_handle_t esp_websocket_client_init(const esp_websocket_client_config_t *config);
esp_err_t esp_websocket_client_set_uri(esp_websocket_client_handle_t client, const char *uri);
esp_err_t esp_websocket_client_start(esp_websocket_client_handle_t client);
esp_err_t esp_websocket_client_stop(esp_websocket_client_handle_t client);
esp_err_t esp_websocket_client_destroy(esp_websocket_client_handle_t client);
int esp_websocket_client_send(esp_websocket_client_handle_t client, const char *data, int len);
bool esp_websocket_client_is_connected(esp_websocket_client_handle_t client);

#endif
