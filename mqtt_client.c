#include <stdio.h>
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

static const char *TAG = "MQTT_CLIENT";

typedef struct mqtt_state
{
    mqtt_connect_info_t *connect_info;
    uint8_t *in_buffer;
    uint8_t *out_buffer;
    int in_buffer_length;
    int out_buffer_length;
    uint16_t message_length;
    uint16_t message_length_read;
    mqtt_message_t *outbound_message;
    mqtt_connection_t mqtt_connection;
    int pending_publish_qos;
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
    long long reconnect_tick;
    int wait_timeout_ms;
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
static esp_err_t esp_mqtt_connect(esp_mqtt_client_handle_t client, int timeout_ms);
static esp_err_t esp_mqtt_abort_connection(esp_mqtt_client_handle_t client);
static esp_err_t esp_mqtt_client_ping(esp_mqtt_client_handle_t client);
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

static esp_err_t esp_mqtt_connect(esp_mqtt_client_handle_t client, int timeout_ms)
{
    int write_len, read_len, connect_rsp_code;
    mqtt_msg_init(&client->mqtt_state.mqtt_connection,
                  client->mqtt_state.out_buffer,
                  client->mqtt_state.out_buffer_length);
    client->mqtt_state.outbound_message = mqtt_msg_connect(&client->mqtt_state.mqtt_connection,
                                          client->mqtt_state.connect_info);
    ESP_LOGI(TAG, "Sending MQTT CONNECT message (type: %d)", MQTT_MSG_TYPE_CONNECT);
    write_len = transport_write(client->transport,
                                (char *)client->mqtt_state.outbound_message->data,
                                client->mqtt_state.outbound_message->length,
                                client->config->network_timeout_ms);
    if (write_len < 0) {
        ESP_LOGE(TAG, "Writing failed, errno= %d", errno);
        return ESP_FAIL;
    }
    read_len = transport_read(client->transport,
                              (char *)client->mqtt_state.in_buffer,
                              client->mqtt_state.outbound_message->length,
                              client->config->network_timeout_ms);
    if (read_len < 0) {
        ESP_LOGE(TAG, "Error network response");
        return ESP_FAIL;
    }

    if (mqtt_get_type(client->mqtt_state.in_buffer) != MQTT_MSG_TYPE_CONNACK) {
        ESP_LOGE(TAG, "Invalid MSG_TYPE response: %d, read_len: %d", mqtt_get_type(client->mqtt_state.in_buffer), read_len);
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
    transport_close(client->transport);
    client->wait_timeout_ms = MQTT_RECONNECT_TIMEOUT_MS;
    client->reconnect_tick = platform_tick_get_ms();
    client->state = MQTT_STATE_WAIT_TIMEOUT;
    ESP_LOGI(TAG, "Reconnect after %d ms", client->wait_timeout_ms);
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
    client->reconnect_tick = platform_tick_get_ms();

    int buffer_size = config->buffer_size;
    if (buffer_size <= 0) {
        buffer_size = MQTT_BUFFER_SIZE_BYTE;
    }

    client->mqtt_state.in_buffer = (uint8_t *)malloc(buffer_size);
    mem_assert(client->mqtt_state.in_buffer);
    client->mqtt_state.in_buffer_length = buffer_size;
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
    if (write_len <= 0) {
        ESP_LOGE(TAG, "Error write data or timeout, written len = %d", write_len);
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
    client->event.msg_id = mqtt_get_id(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_length);
    client->event.user_context = client->config->user_context;
    client->event.client = client;

    if (client->config->event_handle) {
        return client->config->event_handle(&client->event);
    }
    return ESP_FAIL;
}



static void deliver_publish(esp_mqtt_client_handle_t client, uint8_t *message, int length)
{
    const char *mqtt_topic, *mqtt_data;
    uint16_t mqtt_topic_length, mqtt_data_length, total_mqtt_len = 0;
    uint16_t mqtt_len, mqtt_offset = 0;
    int len_read;

    do
    {
        mqtt_topic_length = length;
        mqtt_topic = mqtt_get_publish_topic(message, &mqtt_topic_length);
        mqtt_data_length = length;
        mqtt_data = mqtt_get_publish_data(message, &mqtt_data_length);

        if(total_mqtt_len == 0){
            mqtt_topic_length = length;
            mqtt_topic = mqtt_get_publish_topic(message, &mqtt_topic_length);
            mqtt_data_length = length;
            mqtt_data = mqtt_get_publish_data(message, &mqtt_data_length);
            total_mqtt_len = client->mqtt_state.message_length - client->mqtt_state.message_length_read + mqtt_data_length;
            mqtt_len = mqtt_data_length;
        } else {
            mqtt_len = len_read;
            mqtt_data = (const char*)client->mqtt_state.in_buffer;
        }

        ESP_LOGD(TAG, "Get data len= %d, topic len=%d", mqtt_data_length, mqtt_topic_length);
        client->event.event_id = MQTT_EVENT_DATA;
        client->event.data = (char *)mqtt_data;
        client->event.data_len = mqtt_len;
        client->event.total_data_len = total_mqtt_len;
        client->event.current_data_offset = mqtt_offset;
        client->event.topic = (char *)mqtt_topic;
        client->event.topic_len = mqtt_topic_length;
        esp_mqtt_dispatch_event(client);

        mqtt_offset += mqtt_len;
        if (client->mqtt_state.message_length_read >= client->mqtt_state.message_length)
            break;

        len_read = transport_read(client->transport,
                                      (char *)client->mqtt_state.in_buffer,
                                      client->mqtt_state.in_buffer_length,
                                      client->config->network_timeout_ms);
        if (len_read <= 0) {
            ESP_LOGE(TAG, "Read error or timeout: %d", errno);
            break;
        }
        client->mqtt_state.message_length_read += len_read;
    } while (1);


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

static esp_err_t mqtt_process_receive(esp_mqtt_client_handle_t client)
{
    int read_len;
    uint8_t msg_type;
    uint8_t msg_qos;
    uint16_t msg_id;

    read_len = transport_read(client->transport, (char *)client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_length, 1000);
    if (read_len < 0) {
        ESP_LOGE(TAG, "%s: read error or end of stream", __func__);
        return ESP_FAIL;
    }
    if (read_len == 0) {
        return ESP_OK;
    }

    msg_type = mqtt_get_type(client->mqtt_state.in_buffer);
    msg_qos = mqtt_get_qos(client->mqtt_state.in_buffer);
    msg_id = mqtt_get_id(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_length);
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
            ESP_LOGD(TAG, "%s: received MQTT_MSG_TYPE_PUBLISH", __func__);
            client->mqtt_state.message_length_read = read_len;
            client->mqtt_state.message_length = mqtt_get_total_length(client->mqtt_state.in_buffer, client->mqtt_state.message_length_read);
            ESP_LOGI(TAG, "deliver_publish, message_length_read=%d, message_length=%d", read_len, client->mqtt_state.message_length);
            deliver_publish(client, client->mqtt_state.in_buffer, client->mqtt_state.message_length_read);
            if (msg_qos == 1) {
                client->mqtt_state.outbound_message = mqtt_msg_puback(&client->mqtt_state.mqtt_connection, msg_id);
            }
            else if (msg_qos == 2) {
                client->mqtt_state.outbound_message = mqtt_msg_pubrec(&client->mqtt_state.mqtt_connection, msg_id);
                mqtt_enqueue(client, MQTT_MSG_TYPE_PUBREC, msg_id);
            }

            if (msg_qos == 1 || msg_qos == 2) {
                ESP_LOGD(TAG, "Queue response QoS: %d", msg_qos);

                if (mqtt_write_data(client) != ESP_OK) {
                    ESP_LOGE(TAG, "Error write qos msg repsonse, qos = %d", msg_qos);
                    // TODO: Shoule reconnect?
                    // return ESP_FAIL;
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
                mqtt_write_data(client);
                mqtt_enqueue(client, MQTT_MSG_TYPE_PUBREL, msg_id);
            } else {
                ESP_LOGW(TAG, "%s: received PUBREC for unknown msg_id: %d", __func__, msg_id);
            }
            break;
        case MQTT_MSG_TYPE_PUBREL:
            ESP_LOGD(TAG, "%s: received MQTT_MSG_TYPE_PUBREL", __func__);
            if (is_recent_mqtt_msg(client, MQTT_MSG_TYPE_PUBREC, msg_id)) {
                client->mqtt_state.outbound_message = mqtt_msg_pubcomp(&client->mqtt_state.mqtt_connection, msg_id);
                mqtt_write_data(client);
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
    while (client->run) {

        switch ((int)client->state) {
            case MQTT_STATE_INIT:
                if (client->transport == NULL) {
                    ESP_LOGE(TAG, "There are no transport");
                    client->run = false;
                }

                if (transport_connect(client->transport,
                                      client->config->host,
                                      client->config->port,
                                      client->config->network_timeout_ms) < 0) {
                    ESP_LOGE(TAG, "Error transport connect");
                    esp_mqtt_abort_connection(client);
                    break;
                }
                ESP_LOGD(TAG, "Transport connected to %s://%s:%d", client->config->scheme, client->config->host, client->config->port);
                if (esp_mqtt_connect(client, client->config->network_timeout_ms) != ESP_OK) {
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
                if (platform_tick_get_ms() - client->reconnect_tick > client->wait_timeout_ms) {
                    client->state = MQTT_STATE_INIT;
                    client->reconnect_tick = platform_tick_get_ms();
                    ESP_LOGD(TAG, "Reconnecting...");
                }
                vTaskDelay(client->wait_timeout_ms/2/portTICK_RATE_MS);
                break;
        }
    }
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
