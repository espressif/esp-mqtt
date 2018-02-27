/*
 * This file is subject to the terms and conditions defined in
 * file 'LICENSE', which is part of this source code package.
 * Tuan PM <tuanpm at live dot com>
 */
#ifndef _MQTT_CONFIG_H_
#define _MQTT_CONFIG_H_
#define MQTT_PROTOCOL_311           1
#define MQTT_RECONNECT_TIMEOUT_MS   (10*1000)
#define MQTT_QUEUE_BUFFER_SIZE_WORD 1024
#define MQTT_BUFFER_SIZE_BYTE       1024
#define MQTT_MAX_HOST_LEN           64
#define MQTT_MAX_CLIENT_LEN         32
#define MQTT_MAX_USERNAME_LEN       32
#define MQTT_MAX_PASSWORD_LEN       65
#define MQTT_MAX_LWT_TOPIC          32
#define MQTT_MAX_LWT_MSG            128
#define MQTT_TASK_PRIORITY          5
#define MQTT_TASK_STACK             (6*1024)
#define MQTT_KEEPALIVE_TICK         (120)
#define MQTT_BUFFER_SIZE            (1*1024)
#define MQTT_CMD_QUEUE_SIZE         (10)
#define MQTT_NETWORK_TIMEOUT_MS     (10000)
#define MQTT_TCP_DEFAULT_PORT       (1883)
#define MQTT_SSL_DEFAULT_PORT       (8883)
#define MQTT_WS_DEFAULT_PORT        (80)
#define MQTT_WSS_DEFAULT_PORT       (443)

#define MQTT_ENABLE_SSL             1
#define MQTT_ENABLE_WS              1
#define MQTT_ENABLE_WSS             1

#define OUTBOX_EXPIRED_TIMEOUT_MS   (30*1000)
#define OUTBOX_MAX_SIZE             (4*1024)
#endif
