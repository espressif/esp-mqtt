#ifndef _MQTT_H_
#define _MQTT_H_
#include <stdint.h>
#include "mqtt_config.h"
#include "mqtt_msg.h"

typedef void (* mqtt_callback)(void *);
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
    uint32_t lwt_qos,
    uint32_t lwt_retain;
    uint32_t clean_session;
    uint32_t keepalive;
} mqtt_settings;

typedef struct  {
  int socket;
  mqtt_settings *settings;
  mqtt_state_t  mqtt_state;
  mqtt_connect_info_t connect_info;
} mqtt_client;

void mqtt_task(void *pv);
void mqtt_publish();
void mqtt_subscribe();
void mqtt_detroy();
#endif
