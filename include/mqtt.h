#ifndef _MQTT_H_
#define _MQTT_H_
#include <stdint.h>
#include <string.h>
#include "mqtt_config.h"
#include "mqtt_msg.h"
#include "ringbuf.h"


#if defined(CONFIG_MQTT_SECURITY_ON)  // ENABLE MQTT OVER SSL
#include "openssl/ssl.h"

  #define ClientRead(buf,num) SSL_read(client->ssl, buf, num)    
  #define ClientWrite(buf,num) SSL_write(client->ssl, buf, num)

#else

  #define ClientRead(buf,num) read(client->socket, buf, num) 
  #define ClientWrite(buf,num) write(client->socket, buf, num)   
#endif


typedef void (* mqtt_callback)(void *, void *);

typedef struct {
    mqtt_callback connected_cb;
    mqtt_callback disconnected_cb;
    mqtt_callback reconnect_cb;

    mqtt_callback subscribe_cb;
    mqtt_callback publish_cb;
    mqtt_callback data_cb;

    char host[CONFIG_MQTT_MAX_HOST_LEN];
    uint32_t port;
    char client_id[CONFIG_MQTT_MAX_CLIENT_LEN];
    char username[CONFIG_MQTT_MAX_USERNAME_LEN];
    char password[CONFIG_MQTT_MAX_PASSWORD_LEN];
    char lwt_topic[CONFIG_MQTT_MAX_LWT_TOPIC];
    char lwt_msg[CONFIG_MQTT_MAX_LWT_MSG];
    uint32_t lwt_qos;
    uint32_t lwt_retain;
    uint32_t clean_session;
    uint32_t keepalive;
} mqtt_settings;

typedef struct mqtt_event_data_t
{
  uint8_t type;
  const char* topic;
  const char* data;
  uint16_t topic_length;
  uint16_t data_length;
  uint16_t data_offset;
  uint16_t data_total_length;
} mqtt_event_data_t;

typedef struct mqtt_state_t
{
  uint16_t port;
  int auto_reconnect;
  mqtt_connect_info_t* connect_info;
  uint8_t* in_buffer;
  uint8_t* out_buffer;
  int in_buffer_length;
  int out_buffer_length;
  uint16_t message_length;
  uint16_t message_length_read;
  mqtt_message_t* outbound_message;
  mqtt_connection_t mqtt_connection;
  uint16_t pending_msg_id;
  int pending_msg_type;
  int pending_publish_qos;
} mqtt_state_t;

typedef struct  {
  int socket;

#if defined(CONFIG_MQTT_SECURITY_ON)  // ENABLE MQTT OVER SSL
  SSL_CTX *ctx;
  SSL *ssl;
#endif

  mqtt_settings *settings;
  mqtt_state_t  mqtt_state;
  mqtt_connect_info_t connect_info;
  QueueHandle_t xSendingQueue;
  RINGBUF send_rb;
  uint32_t keepalive_tick;
} mqtt_client;

mqtt_client *mqtt_start(mqtt_settings *mqtt_info);
void mqtt_stop();
void mqtt_task(void *pvParameters);
void mqtt_subscribe(mqtt_client *client, char *topic, uint8_t qos);
void mqtt_publish(mqtt_client* client, char *topic, char *data, int len, int qos, int retain);
void mqtt_detroy();
#endif
