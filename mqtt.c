/*
* @Author: Tuan PM
* @Date:   2016-09-10 09:33:06
* @Last Modified by:   Tuan PM
* @Last Modified time: 2016-09-12 17:03:56
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "ringbuf.h"
#include "mqtt.h"

static TaskHandle_t xMqttTask = NULL;
static TaskHandle_t xMqttSendingTask = NULL;


static int resolev_dns(const char *host, struct sockaddr_in *ip) {
    struct hostent *he;
    struct in_addr **addr_list;
    he = gethostbyname(host);
    if (he == NULL) return 0;
    addr_list = (struct in_addr **)he->h_addr_list;
    if (addr_list[0] == NULL) return 0;
    ip->sin_family = AF_INET;
    memcpy(&ip->sin_addr, addr_list[0], sizeof(ip->sin_addr));
    return 1;
}

static int client_connect(const char *stream_host, int stream_port)
{
    int sock;
    struct sockaddr_in remote_ip;
    while (1) {
        bzero(&remote_ip, sizeof(struct sockaddr_in));
        remote_ip.sin_family = AF_INET;
        //if stream_host is not ip address, resolve it
        if (inet_aton(stream_host, &(remote_ip.sin_addr)) == 0) {
            mqtt_info("Resolve dns for domain: %s", stream_host);
            if (!resolev_dns(stream_host, &remote_ip)) {
                vTaskDelay(1000 / portTICK_RATE_MS);
                continue;
            }
        }
        sock = socket(PF_INET, SOCK_STREAM, 0);
        if (sock == -1) {
            continue;
        }
        remote_ip.sin_port = htons(stream_port);
        mqtt_info("Connecting to server %s:%d,%d",
                  inet_ntoa((remote_ip.sin_addr)),
                  stream_port,
                  remote_ip.sin_port);

        if (connect(sock, (struct sockaddr *)(&remote_ip), sizeof(struct sockaddr)) != 00) {
            close(sock);
            mqtt_error("Conn err.");
            vTaskDelay(1000 / portTICK_RATE_MS);
            continue;
        }
        return sock;
    }
}
/*
 * mqtt_connect
 * input - client
 * return 1: success, 0: fail
 */
static bool mqtt_connect(mqtt_client *client)
{
    int write_len, read_len, connect_rsp_code;
    struct timeval tv;

    tv.tv_sec = 10;  /* 30 Secs Timeout */
    tv.tv_usec = 0;  // Not init'ing this can cause strange errors

    setsockopt(client->socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval));

    mqtt_msg_init(&client->mqtt_state.mqtt_connection,
                  client->mqtt_state.out_buffer,
                  client->mqtt_state.out_buffer_length);
    client->mqtt_state.outbound_message = mqtt_msg_connect(&client->mqtt_state.mqtt_connection,
                                          client->mqtt_state.connect_info);
    client->mqtt_state.pending_msg_type = mqtt_get_type(client->mqtt_state.outbound_message->data);
    client->mqtt_state.pending_msg_id = mqtt_get_id(client->mqtt_state.outbound_message->data,
                                        client->mqtt_state.outbound_message->length);
    mqtt_info("Sending MQTT CONNECT message, type: %d, id: %04X",
              client->mqtt_state.pending_msg_type,
              client->mqtt_state.pending_msg_id);
    write_len = write(client->socket,
                      client->mqtt_state.outbound_message->data,
                      client->mqtt_state.outbound_message->length);
    mqtt_info("Reading MQTT CONNECT response message");
    read_len = read(client->socket, client->mqtt_state.in_buffer, CONFIG_MQTT_BUFFER_SIZE_BYTE);

    tv.tv_sec = 0;  /* No timeout */
    setsockopt(client->socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval));

    if (read_len < 0) {
        mqtt_error("Error network response");
        return false;
    }
    if (mqtt_get_type(client->mqtt_state.in_buffer) != MQTT_MSG_TYPE_CONNACK) {
        mqtt_error("Invalid MSG_TYPE response: %d, read_len: %d", mqtt_get_type(client->mqtt_state.in_buffer), read_len);
        return false;
    }
    connect_rsp_code = mqtt_get_connect_return_code(client->mqtt_state.in_buffer);
    switch (connect_rsp_code) {
        case CONNECTION_ACCEPTED:
            mqtt_info("Connected");
            return true;
        case CONNECTION_REFUSE_PROTOCOL:
        case CONNECTION_REFUSE_SERVER_UNAVAILABLE:
        case CONNECTION_REFUSE_BAD_USERNAME:
        case CONNECTION_REFUSE_NOT_AUTHORIZED:
            mqtt_warn("Connection refuse, reason code: %d", connect_rsp_code);
            return false;
        default:
            mqtt_warn("Connection refuse, Unknow reason");
            return false;
    }
    return false;
}

void mqtt_sending_task(void *pvParameters)
{
    mqtt_client *client = (mqtt_client *)pvParameters;
    uint32_t msg_len, send_len;
    mqtt_info("mqtt_sending_task");

    while (1) {
        if (xQueueReceive(client->xSendingQueue, &msg_len, 1000 / portTICK_RATE_MS)) {
            //queue available
            while (msg_len > 0) {
                send_len = msg_len;
                if (send_len > CONFIG_MQTT_BUFFER_SIZE_BYTE)
                    send_len = CONFIG_MQTT_BUFFER_SIZE_BYTE;
                mqtt_info("Sending...%d bytes", send_len);

                rb_read(&client->send_rb, client->mqtt_state.out_buffer, send_len);
                client->mqtt_state.pending_msg_type = mqtt_get_type(client->mqtt_state.out_buffer);
                client->mqtt_state.pending_msg_id = mqtt_get_id(client->mqtt_state.out_buffer, send_len);
                write(client->socket, client->mqtt_state.out_buffer, send_len);
                msg_len -= send_len;
            }
            //invalidate keepalive timer
            client->keepalive_tick = client->settings->keepalive / 2;
        }
        else {
            if (client->keepalive_tick > 0) client->keepalive_tick --;
            else {
                client->keepalive_tick = client->settings->keepalive / 2;
                client->mqtt_state.outbound_message = mqtt_msg_pingreq(&client->mqtt_state.mqtt_connection);
                client->mqtt_state.pending_msg_type = mqtt_get_type(client->mqtt_state.outbound_message->data);
                client->mqtt_state.pending_msg_id = mqtt_get_id(client->mqtt_state.outbound_message->data,
                                                    client->mqtt_state.outbound_message->length);
                mqtt_info("Sending pingreq");
                write(client->socket,
                      client->mqtt_state.outbound_message->data,
                      client->mqtt_state.outbound_message->length);
            }
        }
    }
    vTaskDelete(NULL);
}

void mqtt_start_receive_schedule(mqtt_client *client)
{
    int read_len;
    uint8_t msg_type;
    uint8_t msg_qos;
    uint16_t msg_id;

    while (1) {
        read_len = read(client->socket, client->mqtt_state.in_buffer, CONFIG_MQTT_BUFFER_SIZE_BYTE);
        mqtt_info("Read len %d", read_len);
        if (read_len == 0)
            break;

        msg_type = mqtt_get_type(client->mqtt_state.in_buffer);
        msg_qos = mqtt_get_qos(client->mqtt_state.in_buffer);
        msg_id = mqtt_get_id(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_length);
        // mqtt_info("msg_type %d, msg_id: %d, pending_id: %d", msg_type, msg_id, client->mqtt_state.pending_msg_type);
        switch (msg_type)
        {
            case MQTT_MSG_TYPE_SUBACK:
                if (client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_SUBSCRIBE && client->mqtt_state.pending_msg_id == msg_id)
                    mqtt_info("Subscribe successful");
                break;
            case MQTT_MSG_TYPE_UNSUBACK:
                if (client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_UNSUBSCRIBE && client->mqtt_state.pending_msg_id == msg_id)
                    mqtt_info("UnSubscribe successful");
                break;
            case MQTT_MSG_TYPE_PUBLISH:
                if (msg_qos == 1)
                    client->mqtt_state.outbound_message = mqtt_msg_puback(&client->mqtt_state.mqtt_connection, msg_id);
                else if (msg_qos == 2)
                    client->mqtt_state.outbound_message = mqtt_msg_pubrec(&client->mqtt_state.mqtt_connection, msg_id);
                if (msg_qos == 1 || msg_qos == 2) {
                    mqtt_info("Queue response QoS: %d", msg_qos);
                    // if (QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1) {
                    //     mqtt_info("MQTT: Queue full");
                    // }
                }
                // deliver_publish(client, client->mqtt_state.in_buffer, client->mqtt_state.message_length_read);
                break;
            case MQTT_MSG_TYPE_PUBACK:
                if (client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_PUBLISH && client->mqtt_state.pending_msg_id == msg_id) {
                    mqtt_info("received MQTT_MSG_TYPE_PUBACK, finish QoS1 publish");
                }

                break;
            case MQTT_MSG_TYPE_PUBREC:
                client->mqtt_state.outbound_message = mqtt_msg_pubrel(&client->mqtt_state.mqtt_connection, msg_id);
                // if (QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1) {
                //     mqtt_info("MQTT: Queue full");
                // }
                break;
            case MQTT_MSG_TYPE_PUBREL:
                client->mqtt_state.outbound_message = mqtt_msg_pubcomp(&client->mqtt_state.mqtt_connection, msg_id);
                // if (QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1) {
                //     mqtt_info("MQTT: Queue full");
                // }
                break;
            case MQTT_MSG_TYPE_PUBCOMP:
                if (client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_PUBLISH && client->mqtt_state.pending_msg_id == msg_id) {
                    mqtt_info("eceive MQTT_MSG_TYPE_PUBCOMP, finish QoS2 publish");
                }
                break;
            case MQTT_MSG_TYPE_PINGREQ:
                client->mqtt_state.outbound_message = mqtt_msg_pingresp(&client->mqtt_state.mqtt_connection);
                // if (QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1) {
                //     mqtt_info("MQTT: Queue full");
                // }
                break;
            case MQTT_MSG_TYPE_PINGRESP:
                // Ignore
                break;
        }
    }
    mqtt_info("network disconnected");
}

void mqtt_destroy(mqtt_client *client)
{
    free(client->mqtt_state.in_buffer);
    free(client->mqtt_state.out_buffer);
    free(client);
    vTaskDelete(xMqttTask);
}

void mqtt_task(void *pvParameters)
{
    mqtt_client *client = (mqtt_client *)pvParameters;

    while (1) {
        client->socket = client_connect(client->settings->host, client->settings->port);
        mqtt_info("Connected to server %s:%d", client->settings->host, client->settings->port);
        if (!mqtt_connect(client)) {
            continue;
            //return;
        }
        mqtt_info("Connected to MQTT broker, create sending thread before call connected callback");
        xTaskCreate(&mqtt_sending_task, "mqtt_sending_task", 2048, client, CONFIG_MQTT_PRIORITY + 1, &xMqttSendingTask);
        if (client->settings->connected_cb) {
            client->settings->connected_cb(client);
        }

        mqtt_info("mqtt_start_receive_schedule");
        mqtt_start_receive_schedule(client);

        close(client->socket);
        vTaskDelete(xMqttSendingTask);
        vTaskDelay(1000 / portTICK_RATE_MS);

    }
    mqtt_destroy(client);


}

void mqtt_start(mqtt_settings *settings)
{
    uint8_t *rb_buf;
    if (xMqttTask != NULL)
        return;
    mqtt_client *client = malloc(sizeof(mqtt_client));

    if (client == NULL) {
        mqtt_error("Memory not enough");
        return;
    }
    memset(client, 0, sizeof(mqtt_client));

    client->settings = settings;
    client->connect_info.client_id = settings->client_id;
    client->connect_info.username = settings->username;
    client->connect_info.password = settings->password;
    client->connect_info.will_topic = settings->lwt_topic;
    client->connect_info.will_message = settings->lwt_msg;
    client->connect_info.will_qos = settings->lwt_qos;
    client->connect_info.will_retain = settings->lwt_retain;
    client->keepalive_tick = settings->keepalive / 2;

    client->connect_info.keepalive = settings->keepalive;
    client->connect_info.clean_session = settings->clean_session;

    client->mqtt_state.in_buffer = (uint8_t *)malloc(CONFIG_MQTT_BUFFER_SIZE_BYTE);
    client->mqtt_state.in_buffer_length = CONFIG_MQTT_BUFFER_SIZE_BYTE;
    client->mqtt_state.out_buffer =  (uint8_t *)malloc(CONFIG_MQTT_BUFFER_SIZE_BYTE);
    client->mqtt_state.out_buffer_length = CONFIG_MQTT_BUFFER_SIZE_BYTE;
    client->mqtt_state.connect_info = &client->connect_info;



    /* Create a queue capable of containing 64 unsigned long values. */
    client->xSendingQueue = xQueueCreate(64, sizeof( uint32_t ));
    rb_buf = (uint8_t*) malloc(CONFIG_MQTT_QUEUE_BUFFER_SIZE_WORD * 4);

    if (rb_buf == NULL) {
        mqtt_error("Memory not enough");
        return;
    }

    rb_init(&client->send_rb, rb_buf, CONFIG_MQTT_QUEUE_BUFFER_SIZE_WORD * 4, 1);

    mqtt_msg_init(&client->mqtt_state.mqtt_connection,
                  client->mqtt_state.out_buffer,
                  client->mqtt_state.out_buffer_length);

    xTaskCreate(&mqtt_task, "mqtt_task", 2048, client, CONFIG_MQTT_PRIORITY, &xMqttTask);
}

void mqtt_subscribe(mqtt_client *client, char *topic, uint8_t qos)
{

    client->mqtt_state.outbound_message = mqtt_msg_subscribe(&client->mqtt_state.mqtt_connection,
                                          topic, qos,
                                          &client->mqtt_state.pending_msg_id);
    mqtt_info("MQTT: queue subscribe, topic\"%s\", id: %d", topic, client->mqtt_state.pending_msg_id);
    rb_write(&client->send_rb,
             client->mqtt_state.outbound_message->data,
             client->mqtt_state.outbound_message->length);
    xQueueSend(client->xSendingQueue, &client->mqtt_state.outbound_message->length, 0);

}
void mqtt_stop()
{

}

