[![](https://travis-ci.org/tuanpmt/espmqtt.png?branch=master)](https://travis-ci.org/tuanpmt/espmqtt)
[![](http://hits.dwyl.io/tuanpmt/espmqtt.svg)](http://hits.dwyl.io/tuanpmt/espmqtt)
[![Twitter Follow](https://img.shields.io/twitter/follow/tuanpmt.svg?style=social&label=Follow)](https://twitter.com/tuanpmt)
![GitHub contributors](https://img.shields.io/github/contributors/tuanpmt/espmqtt.svg)


# ESP32 MQTT Library

## Features

- Based on: https://github.com/tuanpmt/esp_mqtt 
- Support MQTT over TCP, SSL with mbedtls, MQTT over Websocket, MQTT over Websocket Secure
- Easy to setup with URI 
- Multiple instances (Multiple clients in one application)
- Support subscribing, publishing, authentication, will messages, keep alive pings and all 3 QoS levels (it should be a fully functional client).

## How to use

Clone this component to [ESP-IDF](https://github.com/espressif/esp-idf) project (as submodule): 
```
git submodule add https://github.com/tuanpmt/espmqtt.git components/espmqtt
```

Or run a sample (make sure you have installed the [toolchain](http://esp-idf.readthedocs.io/en/latest/get-started/index.html#setup-toolchain)): 

```
git clone https://github.com/tuanpmt/espmqtt.git
cd espmqtt/examples/mqtt_tcp
make menuconfig
make flash monitor
```

## Documentation
### URI

- Curently support `mqtt`, `mqtts`, `ws`, `wss` schemes
- MQTT over HTTP, default port `1883`: `mqtt://iot.eclipse.org`
- MQTT over HTTP, port `1884`: `mqtt://iot.eclipse.org:1884`
- MQTT over HTTP, port `1884`, username and password: `mqtt://username:password@iot.eclipse.org:1884`
- MQTT over HTTPS, default port `8883`: `mqtts://iot.eclipse.org`
- MQTT over Websocket: `ws://iot.eclipse.org:80/ws`
- MQTT over Websocket Secure: `wss://iot.eclipse.org:443/ws`
- Minimal configurations: 

```cpp
const esp_mqtt_client_config_t mqtt_cfg = {
    .uri = "mqtt://iot.eclipse.org",
    .event_handle = mqtt_event_handler,
    // .user_context = (void *)your_context
};
```

### SSL 

- Get Certification from server, example: `iot.eclipse.org` `openssl s_client -showcerts -connect iot.eclipse.org:8883 </dev/null 2>/dev/null|openssl x509 -outform PEM >iot_eclipse_org.pem`
- Check the sample application: `examples/mqtt_ssl`
- Configuration: 

```cpp
const esp_mqtt_client_config_t mqtt_cfg = {
    .uri = "mqtts://iot.eclipse.org:8883",
    .event_handle = mqtt_event_handler,
    .cert_pem = (const char *)iot_eclipse_org_pem_start,
};
```


### More options for `esp_mqtt_client_config_t`

-  `event_handle` for MQTT events
-  `host`: replace `uri` host
-  `port`: replace `uri` port
-  `client_id`: replace default client id is `ESP32_%CHIPID%`
-  `lwt_topic, lwt_msg, lwt_qos, lwt_retain`: are mqtt lwt options, default NULL
-  `disable_clean_session`: mqtt clean session, default clean_session is true
-  `keepalive`: (value in seconds) mqtt keepalive, default is 120 seconds
-  `disable_auto_reconnect`: this mqtt client will reconnect to server (when errors/disconnect). Set `disable_auto_reconnect=true` to disable
-  `user_context` pass user context to this option, then can receive that context in `event->user_context`
-  `task_prio, task_stack` for MQTT task, default priority is 5, and task_stack = 4096 bytes
-  `buffer_size` for MQTT send/receive buffer, default is 1024
-  `cert_pem` pointer to CERT file for server verify (with SSL), default is NULL, not required to verify the server
-  `transport`: override URI transport
    +  `MQTT_TRANSPORT_OVER_TCP`: MQTT over TCP, using scheme: `mqtt`
    +  `MQTT_TRANSPORT_OVER_SSL`: MQTT over SSL, using scheme: `mqtts`
    +  `MQTT_TRANSPORT_OVER_WS`: MQTT over Websocket, using scheme: `ws`
    +  `MQTT_TRANSPORT_OVER_WSS`: MQTT over Websocket Secure, using scheme: `wss`


## Example

Check `examples/mqtt_tcp` and `examples/mqtt_ssl` project. In Short:

```cpp

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    // your_context_t *context = event->context;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            msg_id = esp_mqtt_client_subscribe(client, "/topic/qos0", 0);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 1);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
            ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
            ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            vTaskDelay(500/portTICK_RATE_MS);
            msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
            ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
    }
    return ESP_OK;
}
const esp_mqtt_client_config_t mqtt_cfg = {
    .uri = "mqtt://iot.eclipse.org",
    .event_handle = mqtt_event_handler,
    // .user_context = (void *)your_context
};

esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
esp_mqtt_client_start(client);
```

## License

[@tuanpmt](https://twitter.com/tuanpmt)
Apache License
