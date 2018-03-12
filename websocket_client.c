#include <stdio.h>
#include "platform.h"

#include "websocket_client.h"
#include "transport.h"
#include "transport_tcp.h"
#include "transport_ssl.h"
#include "transport_ws.h"
#include "platform.h"

/* using uri parser */
#include "http_parser.h"

static const char *TAG = "WEBSOCKET_CLIENT";

#define WEBSOCKET_TCP_DEFAULT_PORT (80)
#define WEBSOCKET_SSL_DEFAULT_PORT (443)
#define WEBSOCKET_BUFFER_SIZE_BYTE (1024)
#define WEBSOCKET_RECONNECT_TIMEOUT_MS (10*1000)
#define WEBSOCKET_TASK_PRIORITY (5)
#define WEBSOCKET_TASK_STACK (4*1024)
#define WEBSOCKET_NETWORK_TIMEOUT_MS (10*1000)

const static int STOPPED_BIT = BIT0;

typedef struct {
    websocket_event_callback_t event_handle;
    int task_stack;
    int task_prio;
    char *uri;
    char *host;
    char *path;
    char *scheme;
    char *username;
    char *password;
    int port;
    bool auto_reconnect;
    void *user_context;
    int network_timeout_ms;
} websockeet_config_storage_t;

typedef enum {
    WEBSOCKET_STATE_ERROR = -1,
    WEBSOCKET_STATE_UNKNOW = 0,
    WEBSOCKET_STATE_INIT,
    WEBSOCKET_STATE_CONNECTED,
    WEBSOCKET_STATE_WAIT_TIMEOUT,
} websocket_client_state_t;

struct esp_websocket_client {
    transport_list_handle_t transport_list;
    transport_handle_t transport;
    websockeet_config_storage_t *config;
    websocket_client_state_t state;
    long long keepalive_tick;
    long long reconnect_tick;
    int wait_timeout_ms;
    int auto_reconnect;
    esp_websocket_event_t event;
    bool run;
    EventGroupHandle_t status_bits;
    char *buffer;
    int buffer_size;
};

static char *create_string(const char *ptr, int len)
{
    char *ret;
    if (len <= 0) {
        return NULL;
    }
    ret = calloc(1, len + 1);
    mem_assert(ret);
    memcpy(ret, ptr, len);
    return ret;
}

static esp_err_t esp_websocket_client_dispatch_event(esp_websocket_client_handle_t client)
{
    client->event.user_context = client->config->user_context;
    client->event.client = client;

    if (client->config->event_handle) {
        return client->config->event_handle(&client->event);
    }
    return ESP_FAIL;
}

static esp_err_t esp_websocket_client_abort_connection(esp_websocket_client_handle_t client)
{
    transport_close(client->transport);
    client->wait_timeout_ms = WEBSOCKET_RECONNECT_TIMEOUT_MS;
    client->reconnect_tick = platform_tick_get_ms();
    client->state = WEBSOCKET_STATE_WAIT_TIMEOUT;
    ESP_LOGI(TAG, "Reconnect after %d ms", client->wait_timeout_ms);
    client->event.event_id = WEBSOCKET_EVENT_DISCONNECTED;
    esp_websocket_client_dispatch_event(client);
    return ESP_OK;
}

static esp_err_t esp_websocket_client_set_config(esp_websocket_client_handle_t client, const esp_websocket_client_config_t *config)
{
    //Copy user configurations to client context
    websockeet_config_storage_t *cfg = calloc(1, sizeof(websockeet_config_storage_t));
    mem_assert(cfg);

    cfg->task_prio = config->task_prio;
    if (cfg->task_prio <= 0) {
        cfg->task_prio = WEBSOCKET_TASK_PRIORITY;
    }

    cfg->task_stack = config->task_stack;
    if (cfg->task_stack == 0) {
        cfg->task_stack = WEBSOCKET_TASK_STACK;
    }

    if (config->host) {
        cfg->host = strdup(config->host);
    }

    cfg->port = config->port;

    if (config->username) {
        cfg->username = strdup(config->username);
    }

    if (config->password) {
        cfg->password = strdup(config->password);
    }

    if (config->uri) {
        cfg->uri = strdup(config->uri);
    }

    cfg->network_timeout_ms = WEBSOCKET_NETWORK_TIMEOUT_MS;
    cfg->user_context = config->user_context;
    cfg->event_handle = config->event_handle;
    cfg->auto_reconnect = true;
    if (config->disable_auto_reconnect) {
        cfg->auto_reconnect = false;
    }

    client->config = cfg;
    return ESP_OK;
}

static esp_err_t esp_websocket_client_destroy_config(esp_websocket_client_handle_t client)
{
    websockeet_config_storage_t *cfg = client->config;
    if (cfg->host) {
        free(cfg->host);
    }
    if (cfg->uri) {
        free(cfg->uri);
    }
    if (cfg->path) {
        free(cfg->path);
    }
    if (cfg->scheme) {
        free(cfg->scheme);
    }
    if (cfg->username) {
        free(cfg->username);
    }
    if (cfg->password) {
        free(cfg->password);
    }

    free(client->config);
    return ESP_OK;
}

esp_websocket_client_handle_t esp_websocket_client_init(const esp_websocket_client_config_t *config)
{
    esp_websocket_client_handle_t client = calloc(1, sizeof(struct esp_websocket_client));
    mem_assert(client);

    client->transport_list = transport_list_init();

    transport_handle_t tcp = transport_tcp_init();
    transport_set_default_port(tcp, WEBSOCKET_TCP_DEFAULT_PORT);
    transport_list_add(client->transport_list, tcp, "_tcp");


    transport_handle_t ws = transport_ws_init(tcp);
    transport_set_default_port(ws, WEBSOCKET_TCP_DEFAULT_PORT);
    transport_list_add(client->transport_list, ws, "ws");
    if (config->transport == WEBSOCKET_TRANSPORT_OVER_TCP) {
        client->config->scheme = create_string("ws", 2);
    }

    transport_handle_t ssl = transport_ssl_init();
    transport_set_default_port(ssl, WEBSOCKET_SSL_DEFAULT_PORT);
    if (config->cert_pem) {
        transport_ssl_set_cert_data(ssl, config->cert_pem, strlen(config->cert_pem));
    }
    transport_list_add(client->transport_list, ssl, "_ssl");

    transport_handle_t wss = transport_ws_init(ssl);
    transport_set_default_port(wss, WEBSOCKET_SSL_DEFAULT_PORT);
    transport_list_add(client->transport_list, wss, "wss");
    if (config->transport == WEBSOCKET_TRANSPORT_OVER_TCP) {
        client->config->scheme = create_string("wss", 3);
    }

    esp_websocket_client_set_config(client, config);

    if (client->config->uri) {
        if (esp_websocket_client_set_uri(client, client->config->uri) != ESP_OK) {
            return NULL;
        }
    }

    if (client->config->scheme == NULL) {
        client->config->scheme = create_string("ws", 2);
    }

    client->keepalive_tick = platform_tick_get_ms();
    client->reconnect_tick = platform_tick_get_ms();

    int buffer_size = config->buffer_size;
    if (buffer_size <= 0) {
        buffer_size = WEBSOCKET_BUFFER_SIZE_BYTE;
    }

    client->buffer = malloc(buffer_size);
    assert(client->buffer);
    client->buffer_size = buffer_size;

    esp_websocket_client_set_config(client, config);
    if (config->uri) {
        esp_websocket_client_set_uri(client, config->uri);
    }

    client->status_bits = xEventGroupCreate();
    return client;
}

esp_err_t esp_websocket_client_destroy(esp_websocket_client_handle_t client)
{
    esp_websocket_client_stop(client);
    esp_websocket_client_destroy_config(client);
    transport_list_destroy(client->transport_list);
    free(client->buffer);
    vEventGroupDelete(client->status_bits);
    free(client);
    return ESP_OK;
}

esp_err_t esp_websocket_client_set_uri(esp_websocket_client_handle_t client, const char *uri)
{
    struct http_parser_url puri;
    http_parser_url_init(&puri);
    int parser_status = http_parser_parse_url(uri, strlen(uri), 0, &puri);
    if (parser_status != 0) {
        ESP_LOGE(TAG, "Error parse uri = %s", uri);
        return ESP_FAIL;
    }

    if (client->config->scheme == NULL) {
        client->config->scheme = create_string(uri + puri.field_data[UF_SCHEMA].off, puri.field_data[UF_SCHEMA].len);
    }

    if (client->config->host == NULL) {
        client->config->host = create_string(uri + puri.field_data[UF_HOST].off, puri.field_data[UF_HOST].len);
    }

    if (client->config->path == NULL) {
        client->config->path = create_string(uri + puri.field_data[UF_PATH].off, puri.field_data[UF_PATH].len);
    }

    if (client->config->path) {
        transport_handle_t trans = transport_list_get_transport(client->transport_list, "ws");
        if (trans) {
            transport_ws_set_path(trans, client->config->path);
        }
        trans = transport_list_get_transport(client->transport_list, "wss");
        if (trans) {
            transport_ws_set_path(trans, client->config->path);
        }
    }

    char *port = create_string(uri + puri.field_data[UF_PORT].off, puri.field_data[UF_PORT].len);
    if (port) {
        client->config->port = atoi(port);
        free(port);
    }

    char *user_info = create_string(uri + puri.field_data[UF_USERINFO].off, puri.field_data[UF_USERINFO].len);
    if (user_info) {
        char *pass = strchr(user_info, ':');
        if (pass) {
            pass[0] = 0; //terminal username
            pass ++;
            client->config->password = strdup(pass);
        }
        client->config->username = strdup(user_info);

        free(user_info);
    }

    return ESP_OK;
}


static void esp_websocket_client_task(void *pv)
{
    int rlen;
    esp_websocket_client_handle_t client = (esp_websocket_client_handle_t) pv;
    client->run = true;

    //get transport by scheme
    client->transport = transport_list_get_transport(client->transport_list, client->config->scheme);

    if (client->transport == NULL) {
        ESP_LOGE(TAG, "There are no transports valid, stop websocket client");
        client->run = false;
    }
    //default port
    if (client->config->port == 0) {
        client->config->port = transport_get_default_port(client->transport);
    }

    client->state = WEBSOCKET_STATE_INIT;
    xEventGroupClearBits(client->status_bits, STOPPED_BIT);

    while (client->run) {
        switch ((int)client->state) {
            case WEBSOCKET_STATE_INIT:
                if (client->transport == NULL) {
                    ESP_LOGE(TAG, "There are no transport");
                    client->run = false;
                    break;
                }

                if (transport_connect(client->transport,
                                      client->config->host,
                                      client->config->port,
                                      client->config->network_timeout_ms) < 0) {
                    ESP_LOGE(TAG, "Error transport connect");
                    esp_websocket_client_abort_connection(client);
                    break;
                }
                ESP_LOGD(TAG, "Transport connected to %s://%s:%d", client->config->scheme, client->config->host, client->config->port);

                client->event.event_id = WEBSOCKET_EVENT_CONNECTED;
                client->state = WEBSOCKET_STATE_CONNECTED;
                esp_websocket_client_dispatch_event(client);

                break;
            case WEBSOCKET_STATE_CONNECTED:
                rlen = transport_read(client->transport, client->buffer, client->buffer_size, client->config->network_timeout_ms);
                if (rlen < 0) {
                    ESP_LOGE(TAG, "Error read data");
                    esp_websocket_client_abort_connection(client);
                    break;
                }
                if (rlen > 0) {
                    client->event.event_id = WEBSOCKET_EVENT_DATA;
                    client->event.data = client->buffer;
                    client->event.data_len = rlen;
                    esp_websocket_client_dispatch_event(client);
                }
                break;
            case WEBSOCKET_STATE_WAIT_TIMEOUT:

                if (!client->config->auto_reconnect) {
                    client->run = false;
                    break;
                }
                if (platform_tick_get_ms() - client->reconnect_tick > client->wait_timeout_ms) {
                    client->state = WEBSOCKET_STATE_INIT;
                    client->reconnect_tick = platform_tick_get_ms();
                    ESP_LOGD(TAG, "Reconnecting...");
                }
                vTaskDelay(client->wait_timeout_ms / 2 / portTICK_RATE_MS);
                break;
        }
    }

    transport_close(client->transport);
    xEventGroupSetBits(client->status_bits, STOPPED_BIT);
    vTaskDelete(NULL);
}

esp_err_t esp_websocket_client_start(esp_websocket_client_handle_t client)
{
    if (client->state >= WEBSOCKET_STATE_INIT) {
        ESP_LOGE(TAG, "Client has started");
        return ESP_FAIL;
    }
    if (xTaskCreate(esp_websocket_client_task, "websocket_task", client->config->task_stack, client, client->config->task_prio, NULL) != pdTRUE) {
        ESP_LOGE(TAG, "Error create websocket task");
        return ESP_FAIL;
    }
    xEventGroupClearBits(client->status_bits, STOPPED_BIT);
    return ESP_OK;
}


esp_err_t esp_websocket_client_stop(esp_websocket_client_handle_t client)
{
    client->run = false;
    xEventGroupWaitBits(client->status_bits, STOPPED_BIT, false, true, portMAX_DELAY);
    client->state = WEBSOCKET_STATE_UNKNOW;
    return ESP_OK;
}


int esp_websocket_client_send(esp_websocket_client_handle_t client, const char *data, int len)
{
    int need_write = len;
    int wlen = 0, widx = 0;
    while (widx < len) {
        if (need_write > client->buffer_size) {
            need_write = client->buffer_size;
        }
        memcpy(client->buffer, data + widx, need_write);
        wlen = transport_write(client->transport,
                                   (char *)client->buffer,
                                    need_write,
                                    client->config->network_timeout_ms);
        if (wlen <= 0) {
            return wlen;
        }
        widx += wlen;
        need_write = len - widx;
    }
    return widx;
}

bool esp_websocket_client_is_connected(esp_websocket_client_handle_t client)
{
    return client->state == WEBSOCKET_STATE_CONNECTED;
}
