#include <stdio.h>
#include <errno.h>
#include "platform.h"

#include "freertos/queue.h"
#include "mqtt_client.h"
#include "mqtt_msg.h"
#include "transport.h"
#include "transport_tcp.h"
#include "transport_ssl.h"
#include "transport_ws.h"
#include "platform.h"
#include "mqtt_outbox.h"

/* using uri parser */
#include "http_parser.h"

#if MQTT_TASK_USE_WATCHDOG == 1
#include "esp_task_wdt.h"
#define WATCHDOG_REGISTER    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
#define WATCHDOG_RESET       ESP_ERROR_CHECK(esp_task_wdt_reset());
#define WATCHDOG_UNREGISTER  ESP_ERROR_CHECK(esp_task_wdt_delete(NULL));
#else
#define WATCHDOG_REGISTER    /* nothing */
#define WATCHDOG_RESET       /* nothing */
#define WATCHDOG_UNREGISTER  /* nothing */
#endif

static const char *TAG = "MQTT_CLIENT";

typedef struct mqtt_state
{
    mqtt_connect_info_t *connect_info;
    uint8_t *in_buffer;
    uint8_t *out_buffer;
    int in_buffer_length;
    int out_buffer_length;
    uint16_t message_length;
    uint16_t in_buffer_read_len;
    mqtt_message_t *outbound_message;
    mqtt_connection_t mqtt_connection;
} mqtt_state_t;

typedef struct {
    mqtt_event_callback_t event_handle;
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
} mqtt_config_storage_t;

typedef enum {
    MQTT_STATE_ERROR = -1,
    MQTT_STATE_UNKNOWN = 0,
    MQTT_STATE_INIT,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_WAIT_TIMEOUT,
} mqtt_client_state_t;

struct esp_mqtt_client {
    transport_list_handle_t transport_list;
    transport_handle_t transport;
    mqtt_config_storage_t *config;
    mqtt_state_t  mqtt_state;
    mqtt_connect_info_t connect_info;
    mqtt_client_state_t state;
    long long keepalive_tick;
    int auto_reconnect;
    esp_mqtt_event_t event;
    bool run;
    outbox_handle_t outbox;
    QueueHandle_t out_msgs_queue;
    EventGroupHandle_t status_bits;
};

const static int STOPPED_BIT = BIT0;

static esp_err_t esp_mqtt_dispatch_event(esp_mqtt_client_handle_t client);
static esp_err_t esp_mqtt_set_config(esp_mqtt_client_handle_t client, const esp_mqtt_client_config_t *config);
static esp_err_t esp_mqtt_destroy_config(esp_mqtt_client_handle_t client);
static esp_err_t esp_mqtt_connect(esp_mqtt_client_handle_t client);
static esp_err_t esp_mqtt_abort_connection(esp_mqtt_client_handle_t client);
static esp_err_t esp_mqtt_client_ping(esp_mqtt_client_handle_t client);
static esp_err_t mqtt_write_data(esp_mqtt_client_handle_t client);
static void deliver_publish(esp_mqtt_client_handle_t client);
static int mqtt_message_receive(esp_mqtt_client_handle_t client, int read_poll_timeout_ms);
static char *create_string(const char *ptr, int len);
static mqtt_connection_t *mqtt_connection_alloc(esp_mqtt_client_handle_t client);
static void mqtt_connection_free(mqtt_connection_t *connection);

static esp_err_t esp_mqtt_set_config(esp_mqtt_client_handle_t client, const esp_mqtt_client_config_t *config)
{
    //Copy user configurations to client context
    mqtt_config_storage_t *cfg = calloc(1, sizeof(mqtt_config_storage_t));
    mem_assert(cfg);

    cfg->task_prio = config->task_prio;
    if (cfg->task_prio <= 0) {
        cfg->task_prio = MQTT_TASK_PRIORITY;
    }

    cfg->task_stack = config->task_stack;
    if (cfg->task_stack == 0) {
        cfg->task_stack = MQTT_TASK_STACK;
    }

    if (config->host[0]) {
        cfg->host = strdup(config->host);
    }
    cfg->port = config->port;

    if (config->username[0]) {
        client->connect_info.username = strdup(config->username);
    }

    if (config->password[0]) {
        client->connect_info.password = strdup(config->password);
    }

    if (config->client_id[0]) {
        client->connect_info.client_id = strdup(config->client_id);
    } else {
        client->connect_info.client_id = platform_create_id_string();
    }
    ESP_LOGD(TAG, "MQTT client_id=%s", client->connect_info.client_id);

    if (config->uri[0]) {
        cfg->uri = strdup(config->uri);
    }

    if (config->lwt_topic[0]) {
        client->connect_info.will_topic = strdup(config->lwt_topic);
    }

    if (config->lwt_msg_len) {
        client->connect_info.will_message = malloc(config->lwt_msg_len);
        mem_assert(client->connect_info.will_message);
        memcpy(client->connect_info.will_message, config->lwt_msg, config->lwt_msg_len);
        client->connect_info.will_length = config->lwt_msg_len;
    } else if (config->lwt_msg[0]) {
        client->connect_info.will_message = strdup(config->lwt_msg);
        client->connect_info.will_length = strlen(config->lwt_msg);
    }

    client->connect_info.will_qos = config->lwt_qos;
    client->connect_info.will_retain = config->lwt_retain;

    client->connect_info.clean_session = 1;
    if (config->disable_clean_session) {
        client->connect_info.clean_session = false;
    }
    client->connect_info.keepalive = config->keepalive;
    if (client->connect_info.keepalive == 0) {
        client->connect_info.keepalive = MQTT_KEEPALIVE_TICK;
    }
    cfg->network_timeout_ms = MQTT_NETWORK_TIMEOUT_MS;
    cfg->user_context = config->user_context;
    cfg->event_handle = config->event_handle;
    cfg->auto_reconnect = true;
    if (config->disable_auto_reconnect) {
        cfg->auto_reconnect = false;
    }

    client->config = cfg;
    return ESP_OK;
}

static esp_err_t esp_mqtt_destroy_config(esp_mqtt_client_handle_t client)
{
    mqtt_config_storage_t *cfg = client->config;
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
    if (client->connect_info.will_topic) {
        free(client->connect_info.will_topic);
    }
    if (client->connect_info.will_message) {
        free(client->connect_info.will_message);
    }
    if (client->connect_info.client_id) {
        free(client->connect_info.client_id);
    }
    if (client->connect_info.username) {
        free(client->connect_info.username);
    }
    if (client->connect_info.password) {
        free(client->connect_info.password);
    }
    free(client->config);
    return ESP_OK;
}

static esp_err_t esp_mqtt_connect(esp_mqtt_client_handle_t client)
{
    uint8_t msg_type;
    int received, connect_rsp_code;

    mqtt_msg_init(&client->mqtt_state.mqtt_connection,
                  client->mqtt_state.out_buffer,
                  client->mqtt_state.out_buffer_length);
    client->mqtt_state.outbound_message = mqtt_msg_connect(&client->mqtt_state.mqtt_connection,
                                          client->mqtt_state.connect_info);
    ESP_LOGI(TAG, "Sending MQTT CONNECT message (type: %d)", MQTT_MSG_TYPE_CONNECT);
    if (mqtt_write_data(client) != ESP_OK) {
        ESP_LOGE(TAG, "%s: error sending CONNECT message", __func__);
        return ESP_FAIL;
    }
    client->mqtt_state.in_buffer_read_len = 0;
    client->mqtt_state.message_length = 0;
    /* give the peer 10s to respond */
    received = mqtt_message_receive(client, 10000);
    if (received <= 0) {
        ESP_LOGE(TAG, "%s: mqtt_message_receive() returned %d", __func__, received);
        return ESP_FAIL;
    }
    msg_type = mqtt_get_type(client->mqtt_state.in_buffer);
    if (msg_type != MQTT_MSG_TYPE_CONNACK) {
        ESP_LOGE(TAG, "%s: invalid MSG_TYPE in response to CONNECT: %u", __func__, msg_type);
        return ESP_FAIL;
    }
    connect_rsp_code = mqtt_get_connect_return_code(client->mqtt_state.in_buffer);
    switch (connect_rsp_code) {
        case CONNECTION_ACCEPTED:
            ESP_LOGD(TAG, "Connected");
            return ESP_OK;
        case CONNECTION_REFUSE_PROTOCOL:
            ESP_LOGW(TAG, "Connection refused, bad protocol");
            return ESP_FAIL;
        case CONNECTION_REFUSE_SERVER_UNAVAILABLE:
            ESP_LOGW(TAG, "Connection refused, server unavailable");
            return ESP_FAIL;
        case CONNECTION_REFUSE_BAD_USERNAME:
            ESP_LOGW(TAG, "Connection refused, bad username or password");
            return ESP_FAIL;
        case CONNECTION_REFUSE_NOT_AUTHORIZED:
            ESP_LOGW(TAG, "Connection refused, not authorized");
            return ESP_FAIL;
        default:
            ESP_LOGW(TAG, "Connection refused, Unknow reason");
            return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t esp_mqtt_abort_connection(esp_mqtt_client_handle_t client)
{
    ESP_LOGI(TAG, "%s: closing connection", __func__);
    transport_close(client->transport);
    client->state = MQTT_STATE_WAIT_TIMEOUT;
    client->event.event_id = MQTT_EVENT_DISCONNECTED;
    esp_mqtt_dispatch_event(client);
    return ESP_OK;
}

esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *config)
{
    esp_mqtt_client_handle_t client = calloc(1, sizeof(struct esp_mqtt_client));
    mem_assert(client);

    esp_mqtt_set_config(client, config);
    
    client->transport_list = transport_list_init();

    transport_handle_t tcp = transport_tcp_init();
    transport_set_default_port(tcp, MQTT_TCP_DEFAULT_PORT);
    transport_list_add(client->transport_list, tcp, "mqtt");
    if (config->transport == MQTT_TRANSPORT_OVER_TCP) {
        client->config->scheme = create_string("mqtt", 4);
    }

#if MQTT_ENABLE_WS
    transport_handle_t ws = transport_ws_init(tcp);
    transport_set_default_port(ws, MQTT_WS_DEFAULT_PORT);
    transport_list_add(client->transport_list, ws, "ws");
    if (config->transport == MQTT_TRANSPORT_OVER_WS) {
        client->config->scheme = create_string("ws", 2);
    }
#endif

#if MQTT_ENABLE_SSL
    transport_handle_t ssl = transport_ssl_init();
    transport_set_default_port(ssl, MQTT_SSL_DEFAULT_PORT);
    if (config->cert_pem) {
        transport_ssl_set_cert_data(ssl, config->cert_pem, strlen(config->cert_pem));
    }
    transport_list_add(client->transport_list, ssl, "mqtts");
    if (config->transport == MQTT_TRANSPORT_OVER_SSL) {
        client->config->scheme = create_string("mqtts", 5);
    }
#endif

#if MQTT_ENABLE_WSS
    transport_handle_t wss = transport_ws_init(ssl);
    transport_set_default_port(wss, MQTT_WSS_DEFAULT_PORT);
    transport_list_add(client->transport_list, wss, "wss");
    if (config->transport == MQTT_TRANSPORT_OVER_WSS) {
        client->config->scheme = create_string("wss", 3);
    }
#endif
    if (client->config->uri) {
        if (esp_mqtt_client_set_uri(client, client->config->uri) != ESP_OK) {
            return NULL;
        }
    }

    if (client->config->scheme == NULL) {
        client->config->scheme = create_string("mqtt", 4);
    }

    client->keepalive_tick = platform_tick_get_ms();

    int buffer_size = config->buffer_size;
    if (buffer_size <= 0) {
        buffer_size = MQTT_BUFFER_SIZE_BYTE;
    }
    /*
     * minimal length of a MQTT CONNECT packet is 14 bytes:
     * fixed header: 2 B
     * variable header: 10 B
     * payload (empty ClientId string - [MQTT-3.1.3-6]): 2 B
     */
    assert(buffer_size >= 14);

    client->mqtt_state.in_buffer = (uint8_t *)malloc(buffer_size);
    mem_assert(client->mqtt_state.in_buffer);
    client->mqtt_state.in_buffer_length = buffer_size;
    client->mqtt_state.in_buffer_read_len = 0;
    client->mqtt_state.message_length = 0;
    client->mqtt_state.out_buffer = (uint8_t *)malloc(buffer_size);
    mem_assert(client->mqtt_state.out_buffer);
    client->mqtt_state.out_buffer_length = buffer_size;
    client->mqtt_state.connect_info = &client->connect_info;
    client->outbox = outbox_init();
    client->out_msgs_queue = xQueueCreate(
        config->out_msgs_queue_len > 0 ? config->out_msgs_queue_len : MQTT_OUT_MSGS_QUEUE_LEN,
        sizeof(mqtt_connection_t));
    assert(client->out_msgs_queue != NULL);
    client->status_bits = xEventGroupCreate();
    return client;
}

esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client)
{
    esp_mqtt_client_stop(client);
    esp_mqtt_destroy_config(client);
    transport_list_destroy(client->transport_list);
    outbox_destroy(client->outbox);
    vQueueDelete(client->out_msgs_queue);
    vEventGroupDelete(client->status_bits);
    free(client->mqtt_state.in_buffer);
    free(client->mqtt_state.out_buffer);
    free(client);
    return ESP_OK;
}

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

esp_err_t esp_mqtt_client_set_uri(esp_mqtt_client_handle_t client, const char *uri)
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
            client->connect_info.password = strdup(pass);
        }
        client->connect_info.username = strdup(user_info);

        free(user_info);
    }

    return ESP_OK;
}

static esp_err_t mqtt_write_data(esp_mqtt_client_handle_t client)
{
    int write_len = transport_write(client->transport,
                                    (char *)client->mqtt_state.outbound_message->data,
                                    client->mqtt_state.outbound_message->length,
                                    client->config->network_timeout_ms);
    if (write_len < 0) {
        ESP_LOGE(TAG, "%s: transport_write() failed, errno=%d", __func__, errno);
        return ESP_FAIL;
    }
    if (write_len == 0) {
        ESP_LOGE(TAG, "%s: transport_write() timeout", __func__);
        return ESP_FAIL;
    }
    /* we've just sent a mqtt control packet, update keepalive counter
     * [MQTT-3.1.2-23]
     */
    client->keepalive_tick = platform_tick_get_ms();
    return ESP_OK;
}

static esp_err_t esp_mqtt_dispatch_event(esp_mqtt_client_handle_t client)
{
    client->event.msg_id = mqtt_get_id(client->mqtt_state.in_buffer, client->mqtt_state.message_length);
    client->event.user_context = client->config->user_context;
    client->event.client = client;

    if (client->config->event_handle) {
        return client->config->event_handle(&client->event);
    }
    return ESP_FAIL;
}

static void deliver_publish(esp_mqtt_client_handle_t client)
{
    const char *mqtt_topic, *mqtt_data;
    uint16_t mqtt_topic_len, mqtt_data_len;
    uint8_t *msg_buf = client->mqtt_state.in_buffer;
    uint16_t msg_len = client->mqtt_state.message_length;

    mqtt_topic_len = msg_len;  /* also provides message length to the function */
    mqtt_topic = mqtt_get_publish_topic(msg_buf, &mqtt_topic_len);
    if (mqtt_topic == NULL) {
        ESP_LOGE(TAG, "%s: mqtt_get_publish_topic() failed", __func__);
        return;
    }
    ESP_LOGD(TAG, "%s: mqtt_topic_len=%u", __func__, mqtt_topic_len);

    mqtt_data_len = msg_len;  /* also provides message length to the function */
    mqtt_data = mqtt_get_publish_data(msg_buf, &mqtt_data_len);
    if (mqtt_data == NULL) {
        ESP_LOGE(TAG, "%s: mqtt_get_publish_data() failed", __func__);
        return;
    }
    ESP_LOGD(TAG, "%s: mqtt_data_len=%u", __func__, mqtt_data_len);

    client->event.event_id = MQTT_EVENT_DATA;
    client->event.data = (char *)mqtt_data;
    client->event.data_len = mqtt_data_len;
    client->event.topic = (char *)mqtt_topic;
    client->event.topic_len = mqtt_topic_len;
    esp_mqtt_dispatch_event(client);
}

static bool is_recent_mqtt_msg(esp_mqtt_client_handle_t client, int msg_type, int msg_id)
{
    if (outbox_delete(client->outbox, msg_id, msg_type) == ESP_OK) {
        ESP_LOGD(TAG, "%s: msg_id=%d, msg_type=%d found", __func__, msg_id, msg_type);
        return true;
    }
    ESP_LOGW(TAG, "%s: msg_id=%d, msg_type=%d not found", __func__, msg_id, msg_type);
    return false;
}


/*
 * Copy client->mqtt_state.outbound_message to the Singly-linked Tail queue
 * of sent messages for it's later identification as being "recent" in
 * further communication with the broker.
 */
static void mqtt_enqueue(esp_mqtt_client_handle_t client, int msg_type, int msg_id)
{
    outbox_enqueue(client->outbox,
                   client->mqtt_state.outbound_message->data,
                   client->mqtt_state.outbound_message->length,
                   msg_id,
                   msg_type,
                   platform_tick_get_ms());
}

/*
 * Returns:
 *     -1 in case of failure
 *      0 if no message has been received
 *      1 if a message has been received and placed to client->mqtt_state:
 *           message length:  client->mqtt_state.message_length
 *           message content: client->mqtt_state.in_buffer
 */
static int mqtt_message_receive(esp_mqtt_client_handle_t client, int read_poll_timeout_ms)
{
    int read_len, total_len;
    /*
     * where to store next read bytes
     * the mqtt_state.in_buffer is guaranteed to accommodate at least 14
     * bytes
     */
    uint8_t *buf = client->mqtt_state.in_buffer + client->mqtt_state.in_buffer_read_len;

    client->mqtt_state.message_length = 0;
    if (client->mqtt_state.in_buffer_read_len == 0) {
        /*
         * Read first byte of the mqtt packet fixed header, it contains packet
         * type and flags.
         */
        read_len = transport_read(client->transport, (char *)buf, 1, read_poll_timeout_ms);
        if (read_len < 0) {
            ESP_LOGE(TAG, "%s: transport_read() error: errno=%d", __func__, errno);
            goto err;
        }
        if (read_len == 0) {
            ESP_LOGD(TAG, "%s: transport_read(): no data or EOF", __func__);
            return 0;
        }
        ESP_LOGD(TAG, "%s: first byte: 0x%x", __func__, *buf);
        buf++;
        client->mqtt_state.in_buffer_read_len++;
    }
    if ((client->mqtt_state.in_buffer_read_len == 1) ||
        ((client->mqtt_state.in_buffer_read_len == 2) && (*(buf - 1) & 0x80))) {
        do {
            /*
             * Read the "remaining length" part of mqtt packet fixed header.  It
             * starts at second byte and spans up to 4 bytes, but we accept here
             * only up to 2 bytes of remaining length, i.e. messages with
             * maximal remaining length value = 16383 (maximal total message
             * size of 16386 bytes).
             */
            read_len = transport_read(client->transport, (char *)buf, 1, read_poll_timeout_ms);
            if (read_len < 0) {
                ESP_LOGE(TAG, "%s: transport_read() error: errno=%d", __func__, errno);
                goto err;
            }
            if (read_len == 0) {
                ESP_LOGD(TAG, "%s: transport_read(): no data or EOF", __func__);
                return 0;
            }
            ESP_LOGD(TAG, "%s: read \"remaining length\" byte: 0x%x", __func__, *buf);
            buf++;
            client->mqtt_state.in_buffer_read_len++;
        } while ((client->mqtt_state.in_buffer_read_len < 3) && (*(buf - 1) & 0x80));
        if (*(buf - 1) & 0x80) {
            /* this message will be too big for us */
            ESP_LOGE(TAG, "%s: message is too big", __func__);
            goto err;
        }
    }
    total_len = mqtt_get_total_length(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_read_len);
    ESP_LOGD(TAG, "%s: total message length: %d (already read: %u)", __func__, total_len, client->mqtt_state.in_buffer_read_len);
    if (client->mqtt_state.in_buffer_length < total_len) {
        ESP_LOGE(TAG, "%s: message is too big, insufficient buffer size", __func__);
        goto err;
    }
    if (client->mqtt_state.in_buffer_read_len < total_len) {
        /* read the rest of the mqtt message */
        read_len = transport_read(client->transport, (char *)buf, total_len - client->mqtt_state.in_buffer_read_len, read_poll_timeout_ms);
        ESP_LOGD(TAG, "%s: read_len=%d", __func__, read_len);
        if (read_len < 0) {
            ESP_LOGE(TAG, "%s: transport_read() error: errno=%d", __func__, errno);
            goto err;
        }
        if (read_len == 0) {
            ESP_LOGD(TAG, "%s: transport_read(): no data or EOF", __func__);
            return 0;
        }
        client->mqtt_state.in_buffer_read_len += read_len;
        if (client->mqtt_state.in_buffer_read_len < total_len) {
            ESP_LOGD(TAG, "%s: transport_read(): message reading left in progress :: total message length: %d (already read: %u)",
              __func__, total_len, client->mqtt_state.in_buffer_read_len);
            return 0;
        }
    }
    client->mqtt_state.in_buffer_read_len = 0;
    client->mqtt_state.message_length = total_len;
    return 1;
err:
    client->mqtt_state.in_buffer_read_len = 0;
    return -1;
}

static esp_err_t mqtt_process_receive(esp_mqtt_client_handle_t client)
{
    int received;
    uint8_t msg_type;
    uint8_t msg_qos;
    uint16_t msg_id;

    /* wait at most 100 ms for the data */
    received = mqtt_message_receive(client, 100);
    if (received < 0) {
        ESP_LOGE(TAG, "%s: mqtt_message_receive() returned %d", __func__, received);
        return ESP_FAIL;
    }
    if (received == 0) {
        return ESP_OK;
    }

    msg_type = mqtt_get_type(client->mqtt_state.in_buffer);
    msg_qos = mqtt_get_qos(client->mqtt_state.in_buffer);
    msg_id = mqtt_get_id(client->mqtt_state.in_buffer, client->mqtt_state.message_length);

    ESP_LOGD(TAG, "%s: received msg_type=%d, msg_id=%d", __func__, msg_type, msg_id);
    switch (msg_type)
    {
        case MQTT_MSG_TYPE_SUBACK:
            ESP_LOGD(TAG, "%s: received MQTT_MSG_TYPE_SUBACK", __func__);
            if (is_recent_mqtt_msg(client, MQTT_MSG_TYPE_SUBSCRIBE, msg_id)) {
                ESP_LOGD(TAG, "%s: subscribe successful", __func__);
                client->event.event_id = MQTT_EVENT_SUBSCRIBED;
                esp_mqtt_dispatch_event(client);
            } else {
                ESP_LOGW(TAG, "%s: received SUBACK for unknown msg_id: %d", __func__, msg_id);
            }
            break;
        case MQTT_MSG_TYPE_UNSUBACK:
            ESP_LOGD(TAG, "%s: received MQTT_MSG_TYPE_UNSUBACK", __func__);
            if (is_recent_mqtt_msg(client, MQTT_MSG_TYPE_UNSUBSCRIBE, msg_id)) {
                ESP_LOGD(TAG, "%s: unsubscribe successful", __func__);
                client->event.event_id = MQTT_EVENT_UNSUBSCRIBED;
                esp_mqtt_dispatch_event(client);
            } else {
                ESP_LOGW(TAG, "%s: received UNSUBACK for unknown msg_id: %d", __func__, msg_id);
            }
            break;
        case MQTT_MSG_TYPE_PUBLISH:
            ESP_LOGD(TAG, "%s: received MQTT_MSG_TYPE_PUBLISH, QoS=%d", __func__, msg_qos);
            deliver_publish(client);
            if (msg_qos > 0) {
                if (msg_qos == 1) {
                    client->mqtt_state.outbound_message = mqtt_msg_puback(&client->mqtt_state.mqtt_connection, msg_id);
                } else {  /* msg_qos == 2 */
                    client->mqtt_state.outbound_message = mqtt_msg_pubrec(&client->mqtt_state.mqtt_connection, msg_id);
                }
                if (mqtt_write_data(client) != ESP_OK) {
                    ESP_LOGE(TAG, "%s: error sending repsonse to PUBLISH, msg_id=%d, QoS=%d", __func__, msg_id, msg_qos);
                    return ESP_FAIL;
                }
                if (msg_qos == 2) {
                    mqtt_enqueue(client, MQTT_MSG_TYPE_PUBREC, msg_id);
                }
            }
            break;
        case MQTT_MSG_TYPE_PUBACK:
            ESP_LOGD(TAG, "%s: received MQTT_MSG_TYPE_PUBACK", __func__);
            if (is_recent_mqtt_msg(client, MQTT_MSG_TYPE_PUBLISH, msg_id)) {
                ESP_LOGD(TAG, "%s: publishing of msg_id=%d, QoS=1 succeeded", __func__, msg_id);
                client->event.event_id = MQTT_EVENT_PUBLISHED;
                esp_mqtt_dispatch_event(client);
            } else {
                ESP_LOGW(TAG, "%s: received PUBACK for unknown msg_id: %d", __func__, msg_id);
            }
            break;
        case MQTT_MSG_TYPE_PUBREC:
            ESP_LOGD(TAG, "%s: received MQTT_MSG_TYPE_PUBREC", __func__);
            if (is_recent_mqtt_msg(client, MQTT_MSG_TYPE_PUBLISH, msg_id)) {
                client->mqtt_state.outbound_message = mqtt_msg_pubrel(&client->mqtt_state.mqtt_connection, msg_id);
                if (mqtt_write_data(client) != ESP_OK) {
                    ESP_LOGE(TAG, "%s: error sending repsonse to PUBREC, msg_id=%d", __func__, msg_id);
                    return ESP_FAIL;
                }
                mqtt_enqueue(client, MQTT_MSG_TYPE_PUBREL, msg_id);
            } else {
                ESP_LOGW(TAG, "%s: received PUBREC for unknown msg_id: %d", __func__, msg_id);
            }
            break;
        case MQTT_MSG_TYPE_PUBREL:
            ESP_LOGD(TAG, "%s: received MQTT_MSG_TYPE_PUBREL", __func__);
            if (is_recent_mqtt_msg(client, MQTT_MSG_TYPE_PUBREC, msg_id)) {
                client->mqtt_state.outbound_message = mqtt_msg_pubcomp(&client->mqtt_state.mqtt_connection, msg_id);
                if (mqtt_write_data(client) != ESP_OK) {
                    ESP_LOGE(TAG, "%s: error sending repsonse to PUBREL, msg_id=%d", __func__, msg_id);
                    return ESP_FAIL;
                }
            } else {
                ESP_LOGW(TAG, "%s: received PUBREL for unknown msg_id: %d", __func__, msg_id);
            }
            break;
        case MQTT_MSG_TYPE_PUBCOMP:
            ESP_LOGD(TAG, "%s: received MQTT_MSG_TYPE_PUBCOMP", __func__);
            if (is_recent_mqtt_msg(client, MQTT_MSG_TYPE_PUBREL, msg_id)) {
                ESP_LOGD(TAG, "%s: publishing of msg_id=%d, QoS=2 succeeded", __func__, msg_id);
                client->event.event_id = MQTT_EVENT_PUBLISHED;
                esp_mqtt_dispatch_event(client);
            } else {
                ESP_LOGW(TAG, "%s: received PUBCOMP for unknown msg_id: %d", __func__, msg_id);
            }
            break;
        case MQTT_MSG_TYPE_PINGRESP:
            ESP_LOGD(TAG, "%s: received MQTT_MSG_TYPE_PINGRESP", __func__);
            // Ignore
            break;
    }

    return ESP_OK;
}

static esp_err_t mqtt_process_send(esp_mqtt_client_handle_t client)
{
    mqtt_connection_t *out_msg;

    /* process all out_msgs from the queue (does not block if queue is empty) */
    while (xQueueReceive(client->out_msgs_queue, &out_msg, 0) == pdPASS) {
        ESP_LOGD(TAG, "%s: sending out_msg->message.data=%p, out_msg->message.length=%d",
            __func__, (void *)out_msg->message.data, out_msg->message.length);
        int msg_type = mqtt_get_type(out_msg->message.data);
        int msg_id = mqtt_get_id(out_msg->message.data, out_msg->message.length);
        client->mqtt_state.outbound_message = &out_msg->message;
        ESP_LOGD(TAG, "%s: launching mqtt_write_data()", __func__);
        if (mqtt_write_data(client) != ESP_OK) {
            ESP_LOGE(TAG, "%s: failed to send a message: type=%d, id=%d", __func__, msg_type, msg_id);
            mqtt_connection_free(out_msg);
            return ESP_FAIL;
        }
        if ((msg_type != MQTT_MSG_TYPE_PUBLISH) || (mqtt_get_qos(out_msg->message.data) > 0)) {
            mqtt_enqueue(client, msg_type, msg_id);
        }
        mqtt_connection_free(out_msg);
    }

    /* send a ping if keepalive counter says so [MQTT-3.1.2-23] */
    if (platform_tick_get_ms() - client->keepalive_tick > client->connect_info.keepalive * 1000 / 2) {
        return esp_mqtt_client_ping(client);
    }
    return ESP_OK;
}

static void esp_mqtt_task(void *pv)
{
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t) pv;
    client->run = true;

    //get transport by scheme
    client->transport = transport_list_get_transport(client->transport_list, client->config->scheme);

    if (client->transport == NULL) {
        ESP_LOGE(TAG, "There are no transports valid, stop mqtt client");
        client->run = false;
    }
    //default port
    if (client->config->port == 0) {
        client->config->port = transport_get_default_port(client->transport);
    }

    client->state = MQTT_STATE_INIT;
    xEventGroupClearBits(client->status_bits, STOPPED_BIT);
    WATCHDOG_REGISTER;  /* register this task to watchdog */
    while (client->run) {
        WATCHDOG_RESET;  /* reset watchdog */
        switch ((int)client->state) {
            case MQTT_STATE_INIT:
                if (client->transport == NULL) {
                    ESP_LOGE(TAG, "There are no transport");
                    client->run = false;
                }
                ESP_LOGI(TAG, "%s: connecting to %s://%s:%d", __func__,
                    client->config->scheme, client->config->host, client->config->port);
                if (transport_connect(client->transport,
                                      client->config->host,
                                      client->config->port,
                                      client->config->network_timeout_ms) < 0) {
                    ESP_LOGE(TAG, "Error transport connect");
                    esp_mqtt_abort_connection(client);
                    break;
                }
                ESP_LOGD(TAG, "Transport connected to %s://%s:%d", client->config->scheme, client->config->host, client->config->port);
                if (esp_mqtt_connect(client) != ESP_OK) {
                    ESP_LOGI(TAG, "Error MQTT Connected");
                    esp_mqtt_abort_connection(client);
                    break;
                }
                client->event.event_id = MQTT_EVENT_CONNECTED;
                client->state = MQTT_STATE_CONNECTED;
                esp_mqtt_dispatch_event(client);

                break;
            case MQTT_STATE_CONNECTED:
                // receive and process data
                ESP_LOGD(TAG, "%s: launching mqtt_process_receive()", __func__);
                if (mqtt_process_receive(client) == ESP_FAIL) {
                    ESP_LOGW(TAG, "%s: mqtt_process_receive() failed, aborting connection", __func__);
                    esp_mqtt_abort_connection(client);
                    break;
                }
                /* send data */
                ESP_LOGD(TAG, "%s: launching mqtt_process_send()", __func__);
                if (mqtt_process_send(client) == ESP_FAIL) {
                    ESP_LOGW(TAG, "%s: mqtt_process_send() failed, aborting connection", __func__);
                    esp_mqtt_abort_connection(client);
                    break;
                }

                //Delete mesaage after 30 senconds
                outbox_delete_expired(client->outbox, platform_tick_get_ms(), OUTBOX_EXPIRED_TIMEOUT_MS);
                //
                outbox_cleanup(client->outbox, OUTBOX_MAX_SIZE);
                break;
            case MQTT_STATE_WAIT_TIMEOUT:
                if (!client->config->auto_reconnect) {
                    client->run = false;
                    break;
                }
                ESP_LOGI(TAG, "%s: reconnect after %d ms", __func__, MQTT_RECONNECT_TIMEOUT_MS);
                vTaskDelay(pdMS_TO_TICKS(MQTT_RECONNECT_TIMEOUT_MS));
                client->state = MQTT_STATE_INIT;
                break;
        }
    }
    WATCHDOG_UNREGISTER;  /* unregister this task from watchdog */
    transport_close(client->transport);
    xEventGroupSetBits(client->status_bits, STOPPED_BIT);

    vTaskDelete(NULL);
}

esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client)
{
    if (client->state >= MQTT_STATE_INIT) {
        ESP_LOGE(TAG, "Client has started");
        return ESP_FAIL;
    }
    if (xTaskCreate(esp_mqtt_task, "mqtt_task", client->config->task_stack, client, client->config->task_prio, NULL) != pdTRUE) {
        ESP_LOGE(TAG, "Error create mqtt task");
        return ESP_FAIL;
    }
    return ESP_OK;
}


esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t client)
{
    client->run = false;
    xEventGroupWaitBits(client->status_bits, STOPPED_BIT, false, true, portMAX_DELAY);
    client->state = MQTT_STATE_UNKNOWN;
    return ESP_OK;
}

static esp_err_t esp_mqtt_client_ping(esp_mqtt_client_handle_t client)
{
    client->mqtt_state.outbound_message = mqtt_msg_pingreq(&client->mqtt_state.mqtt_connection);

    if (mqtt_write_data(client) != ESP_OK) {
        ESP_LOGE(TAG, "Error sending ping");
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "Sent PING successful");
    return ESP_OK;
}

/* returns -1 in case of failure */
int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t client, const char *topic, int qos)
{
    uint16_t message_id = 0;
    mqtt_connection_t *out_msg;

    if ((out_msg = mqtt_connection_alloc(client)) == NULL) {
        ESP_LOGE(TAG, "%s: mqtt_connection_alloc() failed", __func__);
        return -1;
    }
    mqtt_msg_subscribe(out_msg, topic, qos, &message_id);
    ESP_LOGD(TAG, "%s: out_msg->message.data=%p, out_msg->message.length=%d",
        __func__, (void *)out_msg->message.data, out_msg->message.length);
    if (out_msg->message.length == 0) {
        ESP_LOGE(TAG, "%s: mqtt_msg_subscribe() failed", __func__);
        mqtt_connection_free(out_msg);
        return -1;
    }
    /* does not block */
    if (xQueueSendToBack(client->out_msgs_queue, &out_msg, 0) != pdPASS) {
        ESP_LOGE(TAG, "%s: failed to enqueue a message with topic=%s", __func__, topic);
        mqtt_connection_free(out_msg);
        return -1;
    }
    return message_id;
}

/* returns -1 in case of failure */
int esp_mqtt_client_unsubscribe(esp_mqtt_client_handle_t client, const char *topic)
{
    uint16_t message_id = 0;
    mqtt_connection_t *out_msg;

    if ((out_msg = mqtt_connection_alloc(client)) == NULL) {
        ESP_LOGE(TAG, "%s: mqtt_connection_alloc() failed", __func__);
        return -1;
    }
    mqtt_msg_unsubscribe(out_msg, topic, &message_id);
    ESP_LOGD(TAG, "%s: out_msg->message.data=%p, out_msg->message.length=%d",
        __func__, (void *)out_msg->message.data, out_msg->message.length);
    if (out_msg->message.length == 0) {
        ESP_LOGE(TAG, "%s: mqtt_msg_unsubscribe() failed", __func__);
        mqtt_connection_free(out_msg);
        return -1;
    }
    /* does not block */
    if (xQueueSendToBack(client->out_msgs_queue, &out_msg, 0) != pdPASS) {
        ESP_LOGE(TAG, "%s: failed to enqueue a message with topic=%s", __func__, topic);
        mqtt_connection_free(out_msg);
        return -1;
    }
    return message_id;
}

/* returns -1 in case of failure */
int esp_mqtt_client_publish(esp_mqtt_client_handle_t client, const char *topic, const char *data, int len, int qos, int retain)
{
    uint16_t message_id = 0;
    mqtt_connection_t *out_msg;

    if ((out_msg = mqtt_connection_alloc(client)) == NULL) {
        ESP_LOGE(TAG, "%s: mqtt_connection_alloc() failed", __func__);
        return -1;
    }
    if (len <= 0) {
        len = strlen(data);
    }
    mqtt_msg_publish(out_msg, topic, data, len, qos, retain, &message_id);
    ESP_LOGD(TAG, "%s: out_msg->message.data=%p, out_msg->message.length=%d", __func__, (void *)out_msg->message.data, out_msg->message.length);
    if (out_msg->message.length == 0) {
        ESP_LOGE(TAG, "%s: mqtt_msg_publish() failed", __func__);
        mqtt_connection_free(out_msg);
        return -1;
    }
    /* does not block */
    if (xQueueSendToBack(client->out_msgs_queue, &out_msg, 0) != pdPASS) {
        ESP_LOGE(TAG, "%s: failed to enqueue a message with topic=%s", __func__, topic);
        mqtt_connection_free(out_msg);
        return -1;
    }
    return message_id;
}

static mqtt_connection_t *mqtt_connection_alloc(esp_mqtt_client_handle_t client)
{
    uint8_t *buffer;
    int buffer_size = client->mqtt_state.out_buffer_length;
    mqtt_connection_t *connection;

    buffer = (uint8_t *)malloc(buffer_size);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "%s: failed to allocate a buffer", __func__);
        return NULL;
    }
    connection = (mqtt_connection_t *)malloc(sizeof(mqtt_connection_t));
    if (connection == NULL) {
        ESP_LOGE(TAG, "%s: failed to allocate a connection", __func__);
        free(buffer);
        return NULL;
    }
    mqtt_msg_init(connection, buffer, buffer_size);
    return connection;
}

static void mqtt_connection_free(mqtt_connection_t *connection)
{
    free(connection->buffer);
    free(connection);
}
