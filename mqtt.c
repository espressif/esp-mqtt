/*
* @Author: Tuan PM
* @Date:   2016-09-10 09:33:06
* @Last Modified by:   Tuan PM
* @Last Modified time: 2017-02-15 13:11:53
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

static bool terminate_mqtt = false;

static int resolve_dns(const char *host, struct sockaddr_in *ip) {
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
static void mqtt_queue(mqtt_client *client)
{
    int msg_len;
    while (rb_available(&client->send_rb) < client->mqtt_state.outbound_message->length) {
        xQueueReceive(client->xSendingQueue, &msg_len, 1000 / portTICK_RATE_MS);
        rb_read(&client->send_rb, client->mqtt_state.out_buffer, msg_len);
    }
    rb_write(&client->send_rb,
             client->mqtt_state.outbound_message->data,
             client->mqtt_state.outbound_message->length);
    xQueueSend(client->xSendingQueue, &client->mqtt_state.outbound_message->length, 0);
}

static bool client_connect(mqtt_client *client)
{
    struct sockaddr_in remote_ip;

    while (1) {

        bzero(&remote_ip, sizeof(struct sockaddr_in));
        remote_ip.sin_family = AF_INET;
        remote_ip.sin_port = htons(client->settings->port);


        //if host is not ip address, resolve it
        if (inet_aton( client->settings->host, &(remote_ip.sin_addr)) == 0) {
            mqtt_info("Resolve dns for domain: %s", client->settings->host);

            if (!resolve_dns(client->settings->host, &remote_ip)) {
                vTaskDelay(1000 / portTICK_RATE_MS);
                continue;
            }
        }


#if defined(CONFIG_MQTT_SECURITY_ON)  // ENABLE MQTT OVER SSL
        client->ctx = NULL;
        client->ssl = NULL;

        client->ctx = SSL_CTX_new(TLSv1_2_client_method());
        if (!client->ctx) {
            mqtt_error("Failed to create SSL CTX");
            goto failed1;
        }
#endif

        client->socket = socket(PF_INET, SOCK_STREAM, 0);
        if (client->socket == -1) {
            mqtt_error("Failed to create socket");
            goto failed2;
        }



        mqtt_info("Connecting to server %s:%d,%d",
                  inet_ntoa((remote_ip.sin_addr)),
                  client->settings->port,
                  remote_ip.sin_port);


        if (connect(client->socket, (struct sockaddr *)(&remote_ip), sizeof(struct sockaddr)) != 00) {
            mqtt_error("Connect failed");
            goto failed3;
        }

#if defined(CONFIG_MQTT_SECURITY_ON)  // ENABLE MQTT OVER SSL
        mqtt_info("Creating SSL object...");
        client->ssl = SSL_new(client->ctx);
        if (!client->ssl) {
            mqtt_error("Unable to creat new SSL");
            goto failed3;
        }

        if (!SSL_set_fd(client->ssl, client->socket)) {
            mqtt_error("SSL set_fd failed");
            goto failed3;
        }

        mqtt_info("Start SSL connect..");
        if (!SSL_connect(client->ssl)) {
            mqtt_error("SSL Connect FAILED");
            goto failed4;
        }
#endif
        mqtt_info("Connected!");

        return true;

        //failed5:
        //   SSL_shutdown(client->ssl);

#if defined(CONFIG_MQTT_SECURITY_ON)
        failed4:
          SSL_free(client->ssl);
          client->ssl = NULL;
#endif

        failed3:
          close(client->socket);
          client->socket = -1;

        failed2:
#if defined(CONFIG_MQTT_SECURITY_ON)
          SSL_CTX_free(client->ctx);

        failed1:
          client->ctx = NULL;
#endif
         vTaskDelay(1000 / portTICK_RATE_MS);

     }
}


// Close client socket
// including SSL objects if CNFIG_MQTT_SECURITY_ON is enabled
void closeclient(mqtt_client *client)
{
    mqtt_info("Closing client socket");

    if (client->socket != -1)
	{
	  close(client->socket);
	  client->socket = -1;
	}

#if defined(CONFIG_MQTT_SECURITY_ON)
	if (client->ssl != NULL)
	{
	  SSL_shutdown(client->ssl);

	  SSL_free(client->ssl);
	  client->ssl = NULL;
	}

	if (client->ctx != NULL)
	{
	  SSL_CTX_free(client->ctx);
	  client->ctx = NULL;
	}
#endif

}

int mqtt_read(mqtt_client *client, void *buffer, int len, int timeout_ms)
{
    int result;
    struct timeval tv;
    if (timeout_ms > 0) {
        tv.tv_sec = 0;
        tv.tv_usec = timeout_ms * 1000;
        while (tv.tv_usec > 1000 * 1000) {
            tv.tv_usec -= 1000 * 1000;
            tv.tv_sec++;
        }
        setsockopt(client->socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

#if defined(CONFIG_MQTT_SECURITY_ON)
    result = SSL_read(client->ssl, buffer, len);
#else
    result = read(client->socket, buffer, len);
#endif

    if (timeout_ms > 0) {
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        setsockopt(client->socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    return result;
}

int mqtt_write(mqtt_client *client, const void *buffer, int len, int timeout_ms)
{
    int result;
    struct timeval tv;
    if (timeout_ms > 0) {
        tv.tv_sec = 0;
        tv.tv_usec = timeout_ms * 1000;
        while (tv.tv_usec > 1000 * 1000) {
            tv.tv_usec -= 1000 * 1000;
            tv.tv_sec++;
        }
        setsockopt(client->socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }

#if defined(CONFIG_MQTT_SECURITY_ON)
    result = SSL_write(client->ssl, buffer, len);
#else
    result = write(client->socket, buffer, len);
#endif

    if (result > 0 && timeout_ms > 0) {
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        setsockopt(client->socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }

    return result;
}

/*
 * mqtt_connect
 * input - client
 * return 1: success, 0: fail
 */
static bool mqtt_connect(mqtt_client *client)
{
    int write_len, read_len, connect_rsp_code;


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

    write_len = client->settings->write_cb(client,
                      client->mqtt_state.outbound_message->data,
                      client->mqtt_state.outbound_message->length, 0);
    if(write_len < 0) {
        mqtt_error("Writing failed: %d", errno);
        return false;
    }

    mqtt_info("Reading MQTT CONNECT response message");

    read_len = client->settings->read_cb(client, client->mqtt_state.in_buffer, CONFIG_MQTT_BUFFER_SIZE_BYTE, 10 * 1000);

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
            mqtt_warn("Connection refused, bad protocol");
            return false;
        case CONNECTION_REFUSE_SERVER_UNAVAILABLE:
            mqtt_warn("Connection refused, server unavailable");
            return false;
        case CONNECTION_REFUSE_BAD_USERNAME:
            mqtt_warn("Connection refused, bad username or password");
            return false;
        case CONNECTION_REFUSE_NOT_AUTHORIZED:
            mqtt_warn("Connection refused, not authorized");
            return false;
        default:
            mqtt_warn("Connection refused, Unknown reason");
            return false;
    }
    return false;
}

void mqtt_sending_task(void *pvParameters)
{
    mqtt_client *client = (mqtt_client *)pvParameters;
    uint32_t msg_len;
    int send_len;
    bool connected = true;
    mqtt_info("mqtt_sending_task");

    while (connected) {
        if (xQueueReceive(client->xSendingQueue, &msg_len, 1000 / portTICK_RATE_MS)) {
            //queue available
            while (msg_len > 0) {
                send_len = msg_len;
                if (send_len > CONFIG_MQTT_BUFFER_SIZE_BYTE) {
                    send_len = CONFIG_MQTT_BUFFER_SIZE_BYTE;
                }
                mqtt_info("Sending...%d bytes", send_len);

                // blocking operation, takes data from ring buffer
                rb_read(&client->send_rb, client->mqtt_state.out_buffer, send_len);
                client->mqtt_state.pending_msg_type = mqtt_get_type(client->mqtt_state.out_buffer);
                client->mqtt_state.pending_msg_id = mqtt_get_id(client->mqtt_state.out_buffer, send_len);
                send_len = client->settings->write_cb(client, client->mqtt_state.out_buffer, send_len, 5 * 1000);
                if(send_len <= 0) {
                    mqtt_info("Write error: %d, result=%d", errno, send_len);
                    connected = false;
                    break;
                }

                //TODO: Check sending type, to callback publish message
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
                send_len = client->settings->write_cb(client,
                      client->mqtt_state.outbound_message->data,
                      client->mqtt_state.outbound_message->length, 0);
                if(send_len <= 0) {
					mqtt_info("Write error: %d", errno);
                    connected = false;
					break;
				}
            }
        }
    }
    closeclient(client);
    xMqttSendingTask = NULL;
    mqtt_info("mqtt_sending_task destroy");
    vTaskDelete(NULL);
}

void deliver_publish(mqtt_client *client, uint8_t *message, int length)
{
    mqtt_event_data_t event_data;
    int len_read, total_mqtt_len = 0, mqtt_len = 0, mqtt_offset = 0;

    do
    {
        event_data.topic_length = length;
        event_data.topic = mqtt_get_publish_topic(message, &event_data.topic_length);
        event_data.data_length = length;
        event_data.data = mqtt_get_publish_data(message, &event_data.data_length);

        if(total_mqtt_len == 0){
            total_mqtt_len = client->mqtt_state.message_length - client->mqtt_state.message_length_read + event_data.data_length;
            mqtt_len = event_data.data_length;
        } else {
            mqtt_len = len_read;
        }

        event_data.data_total_length = total_mqtt_len;
        event_data.data_offset = mqtt_offset;
        event_data.data_length = mqtt_len;

        mqtt_info("Data received: %d/%d bytes ", mqtt_len, total_mqtt_len);
        if(client->settings->data_cb) {
            client->settings->data_cb(client, &event_data);
        }
        mqtt_offset += mqtt_len;
        if (client->mqtt_state.message_length_read >= client->mqtt_state.message_length)
            break;

        len_read = client->settings->read_cb(client, client->mqtt_state.in_buffer, CONFIG_MQTT_BUFFER_SIZE_BYTE, 0);
        if(len_read < 0) {
            mqtt_info("Read error: %d", errno);
            break;
        }
        client->mqtt_state.message_length_read += len_read;
    } while (1);

}
void mqtt_start_receive_schedule(mqtt_client *client)
{
    int read_len;
    uint8_t msg_type;
    uint8_t msg_qos;
    uint16_t msg_id;

    while (1) {

    	if (terminate_mqtt) break;
    	if (xMqttSendingTask == NULL) break;

        read_len = client->settings->read_cb(client, client->mqtt_state.in_buffer, CONFIG_MQTT_BUFFER_SIZE_BYTE, 0);

        mqtt_info("Read len %d", read_len);
        if (read_len <= 0) {
            // ECONNRESET for example
            mqtt_info("Read error %d", errno);
            break;
        }

        msg_type = mqtt_get_type(client->mqtt_state.in_buffer);
        msg_qos = mqtt_get_qos(client->mqtt_state.in_buffer);
        msg_id = mqtt_get_id(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_length);
        mqtt_info("msg_type %d, msg_id %d, pending_type %d, pending_id %d", msg_type, msg_id, client->mqtt_state.pending_msg_type, client->mqtt_state.pending_msg_id);
        switch (msg_type)
        {
            case MQTT_MSG_TYPE_SUBACK:
                if (client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_SUBSCRIBE && client->mqtt_state.pending_msg_id == msg_id) {
                    mqtt_info("Subscribe successful");
                    if (client->settings->subscribe_cb) {
                        client->settings->subscribe_cb(client, NULL);
                    }
                }
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
                    mqtt_queue(client);
                    // if (QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1) {
                    //     mqtt_info("MQTT: Queue full");
                    // }
                }
                client->mqtt_state.message_length_read = read_len;
                client->mqtt_state.message_length = mqtt_get_total_length(client->mqtt_state.in_buffer, client->mqtt_state.message_length_read);
                mqtt_info("deliver_publish");

                deliver_publish(client, client->mqtt_state.in_buffer, client->mqtt_state.message_length_read);
                // deliver_publish(client, client->mqtt_state.in_buffer, client->mqtt_state.message_length_read);
                break;
            case MQTT_MSG_TYPE_PUBACK:
                if (client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_PUBLISH && client->mqtt_state.pending_msg_id == msg_id) {
                    mqtt_info("received MQTT_MSG_TYPE_PUBACK, finish QoS1 publish");
                }

                break;
            case MQTT_MSG_TYPE_PUBREC:
                client->mqtt_state.outbound_message = mqtt_msg_pubrel(&client->mqtt_state.mqtt_connection, msg_id);
                mqtt_queue(client);
                break;
            case MQTT_MSG_TYPE_PUBREL:
                client->mqtt_state.outbound_message = mqtt_msg_pubcomp(&client->mqtt_state.mqtt_connection, msg_id);
                mqtt_queue(client);

                break;
            case MQTT_MSG_TYPE_PUBCOMP:
                if (client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_PUBREL && client->mqtt_state.pending_msg_id == msg_id) {
                    mqtt_info("Receive MQTT_MSG_TYPE_PUBCOMP, finish QoS2 publish");
                }
                break;
            case MQTT_MSG_TYPE_PINGREQ:
                client->mqtt_state.outbound_message = mqtt_msg_pingresp(&client->mqtt_state.mqtt_connection);
                mqtt_queue(client);
                break;
            case MQTT_MSG_TYPE_PINGRESP:
                mqtt_info("MQTT_MSG_TYPE_PINGRESP");
                // Ignore
                break;
        }
    }
}

void mqtt_destroy(mqtt_client *client)
{
	if (client == NULL) return;

	vQueueDelete(client->xSendingQueue);

    free(client->mqtt_state.in_buffer);
    free(client->mqtt_state.out_buffer);
    free(client->send_rb.p_o);
    free(client);

    mqtt_info("Client destroyed");
}

void mqtt_task(void *pvParameters)
{
    mqtt_info("Starting mqtt task");

    mqtt_client *client = (mqtt_client *)pvParameters;

    while (1) {
    	if (terminate_mqtt) break;

        client->settings->connect_cb(client);

        mqtt_info("Connected to server %s:%d", client->settings->host, client->settings->port);
        if (!mqtt_connect(client)) {
            client->settings->disconnect_cb(client);

            if (client->settings->disconnected_cb) {
				client->settings->disconnected_cb(client, NULL);
			}

            if (!client->settings->auto_reconnect) {
				break;
			} else {
				continue;
			}
        }
        mqtt_info("Connected to MQTT broker, create sending thread before call connected callback");
        xTaskCreate(&mqtt_sending_task, "mqtt_sending_task", 2048, client, CONFIG_MQTT_PRIORITY + 1, &xMqttSendingTask);
        if (client->settings->connected_cb) {
            client->settings->connected_cb(client, NULL);
        }

        mqtt_info("mqtt_start_receive_schedule");
        mqtt_start_receive_schedule(client);

        client->settings->disconnect_cb(client);
        if (client->settings->disconnected_cb) {
        	client->settings->disconnected_cb(client, NULL);
		}

        if (xMqttSendingTask != NULL) {
        	vTaskDelete(xMqttSendingTask);
        }
        if (!client->settings->auto_reconnect) {
			break;
		}

        // clean up for new reconnect
        xQueueReset(client->xSendingQueue);
        rb_reset(&client->send_rb);

        vTaskDelay(1000 / portTICK_RATE_MS);

    }

    mqtt_destroy(client);
    xMqttTask = NULL;
    vTaskDelete(NULL);
}

mqtt_client *mqtt_start(mqtt_settings *settings)
{
	terminate_mqtt = false;

    int stackSize = 2048;

    uint8_t *rb_buf;
    if (xMqttTask != NULL)
        return NULL;
    mqtt_client *client = malloc(sizeof(mqtt_client));

    if (client == NULL) {
        mqtt_error("Memory not enough");
        return NULL;
    }
    memset(client, 0, sizeof(mqtt_client));

    if (settings->lwt_msg_len > CONFIG_MQTT_MAX_LWT_MSG) {
        mqtt_error("Last will message longer than CONFIG_MQTT_MAX_LWT_MSG!");
    }

    client->settings = settings;
    client->connect_info.client_id = settings->client_id;
    client->connect_info.username = settings->username;
    client->connect_info.password = settings->password;
    client->connect_info.will_topic = settings->lwt_topic;
    client->connect_info.will_message = settings->lwt_msg;
    client->connect_info.will_qos = settings->lwt_qos;
    client->connect_info.will_retain = settings->lwt_retain;
    client->connect_info.will_length = settings->lwt_msg_len;

    client->keepalive_tick = settings->keepalive / 2;

    client->connect_info.keepalive = settings->keepalive;
    client->connect_info.clean_session = settings->clean_session;

    client->mqtt_state.in_buffer = (uint8_t *)malloc(CONFIG_MQTT_BUFFER_SIZE_BYTE);
    client->mqtt_state.in_buffer_length = CONFIG_MQTT_BUFFER_SIZE_BYTE;
    client->mqtt_state.out_buffer =  (uint8_t *)malloc(CONFIG_MQTT_BUFFER_SIZE_BYTE);
    client->mqtt_state.out_buffer_length = CONFIG_MQTT_BUFFER_SIZE_BYTE;
    client->mqtt_state.connect_info = &client->connect_info;

    client->socket = -1;

    if (!client->settings->connect_cb)
        client->settings->connect_cb = client_connect;
    if (!client->settings->disconnect_cb)
        client->settings->disconnect_cb = closeclient;
    if (!client->settings->read_cb)
        client->settings->read_cb = mqtt_read;
    if (!client->settings->write_cb)
        client->settings->write_cb = mqtt_write;

#if defined(CONFIG_MQTT_SECURITY_ON)  // ENABLE MQTT OVER SSL
    client->ctx = NULL;
    client->ssl = NULL;
    stackSize = 10240; // Need more stack to handle SSL handshake
#endif

    /* Create a queue capable of containing 64 unsigned long values. */
    client->xSendingQueue = xQueueCreate(64, sizeof( uint32_t ));
    rb_buf = (uint8_t*) malloc(CONFIG_MQTT_QUEUE_BUFFER_SIZE_WORD * 4);

    if (rb_buf == NULL) {
        mqtt_error("Memory not enough");
        return NULL;
    }

    rb_init(&client->send_rb, rb_buf, CONFIG_MQTT_QUEUE_BUFFER_SIZE_WORD * 4, 1);

    mqtt_msg_init(&client->mqtt_state.mqtt_connection,
                  client->mqtt_state.out_buffer,
                  client->mqtt_state.out_buffer_length);

    xTaskCreate(&mqtt_task, "mqtt_task", stackSize, client, CONFIG_MQTT_PRIORITY, &xMqttTask);
    return client;
}

void mqtt_subscribe(mqtt_client *client, const char *topic, uint8_t qos)
{
    client->mqtt_state.outbound_message = mqtt_msg_subscribe(&client->mqtt_state.mqtt_connection,
                                          topic, qos,
                                          &client->mqtt_state.pending_msg_id);
    mqtt_info("Queue subscribe, topic\"%s\", id: %d", topic, client->mqtt_state.pending_msg_id);
    mqtt_queue(client);
}


void mqtt_unsubscribe(mqtt_client *client, const char *topic)
{
	client->mqtt_state.outbound_message = mqtt_msg_unsubscribe(&client->mqtt_state.mqtt_connection,
	                                          topic,
	                                          &client->mqtt_state.pending_msg_id);
	mqtt_info("Queue unsubscribe, topic\"%s\", id: %d", topic, client->mqtt_state.pending_msg_id);
	mqtt_queue(client);
}

void mqtt_publish(mqtt_client* client, const char *topic, const char *data, int len, int qos, int retain)
{

    client->mqtt_state.outbound_message = mqtt_msg_publish(&client->mqtt_state.mqtt_connection,
                                          topic, data, len,
                                          qos, retain,
                                          &client->mqtt_state.pending_msg_id);
    mqtt_queue(client);
    mqtt_info("Queuing publish, length: %d, queue size(%d/%d)",
              client->mqtt_state.outbound_message->length,
              client->send_rb.fill_cnt,
              client->send_rb.size);
}

void mqtt_stop()
{
	terminate_mqtt = true;
}

