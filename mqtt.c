/*
* @Author: Tuan PM
* @Date:   2016-09-10 09:33:06
* @Last Modified by:   Tuan PM
* @Last Modified time: 2016-09-12 12:35:23
*/
#include "mqtt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include <stdio.h>

static TaskHandle_t xMqttTask = NULL;


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
            if (client->settings->connected_cb) {
                client->settings->connected_cb(client);
            }
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


void mqtt_destroy(mqtt_client *client)
{
    free(client->mqtt_state.in_buffer);
    free(client->mqtt_state.out_buffer);
    free(client);
}

void mqtt_task(void *pvParameters)
{
    mqtt_client *client = (mqtt_client *)pvParameters;
    client->socket = client_connect(client->settings->host, client->settings->port);
    mqtt_info("Connected to server %s:%d", client->settings->host, client->settings->port);

    if (!mqtt_connect(client)) {
        close(client->socket);
        //return;
    }
    mqtt_info("wait");
    while(1);
    mqtt_destroy(client);
    vTaskDelete(NULL);

}

void mqtt_start(mqtt_settings *settings)
{
    if (xMqttTask != NULL)
        return;
    mqtt_client *client = malloc(sizeof(mqtt_client));
    memset(client, 0, sizeof(mqtt_client));
    if (client == NULL) {
        mqtt_error("Memory not enought");
        return;
    }
    client->settings = settings;
    client->connect_info.client_id = settings->client_id;
    client->connect_info.username = settings->username;
    client->connect_info.password = settings->password;
    client->connect_info.will_topic = settings->lwt_topic;
    client->connect_info.will_message = settings->lwt_msg;
    client->connect_info.will_qos = settings->lwt_qos;
    client->connect_info.will_retain = settings->lwt_retain;

    client->connect_info.keepalive = settings->keepalive;
    client->connect_info.clean_session = settings->clean_session;

    client->mqtt_state.in_buffer = (uint8_t *)malloc(CONFIG_MQTT_BUFFER_SIZE_BYTE);
    client->mqtt_state.in_buffer_length = CONFIG_MQTT_BUFFER_SIZE_BYTE;
    client->mqtt_state.out_buffer =  (uint8_t *)malloc(CONFIG_MQTT_BUFFER_SIZE_BYTE);
    client->mqtt_state.out_buffer_length = CONFIG_MQTT_BUFFER_SIZE_BYTE;
    client->mqtt_state.connect_info = &client->connect_info;

    mqtt_msg_init(&client->mqtt_state.mqtt_connection,
                  client->mqtt_state.out_buffer,
                  client->mqtt_state.out_buffer_length);

    xTaskCreate(&mqtt_task, "mqtt_task", 2048, client, CONFIG_MQTT_PRIORITY, &xMqttTask);
}

void mqtt_stop()
{

}

