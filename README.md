# ESP32 MQTT Library

![GitHub Actions Workflow Status](https://img.shields.io/github/actions/workflow/status/espressif/esp-mqtt/test-examples.yml?branch=master)
![License](https://img.shields.io/github/license/espressif/esp-mqtt)
![GitHub contributors](https://img.shields.io/github/contributors/espressif/esp-mqtt)

## Features

- Based on: <https://github.com/tuanpmt/esp_mqtt>
- Support MQTT over TCP, SSL with mbedtls, MQTT over Websocket, MQTT over Websocket Secure
- Easy to setup with URI
- Multiple instances (Multiple clients in one application)
- Support subscribing, publishing, authentication, will messages, keep alive pings and all 3 QoS levels (it should be a fully functional client).
- Support for MQTT 3.1.1 and 5.0

## How to use

[ESP-MQTT](https://github.com/espressif/esp-mqtt) is a standard [ESP-IDF](https://github.com/espressif/esp-idf) component.
Please refer to instructions in [ESP-IDF](https://github.com/espressif/esp-idf)

## Documentation

- Please refer to the standard [ESP-IDF](https://github.com/espressif/esp-idf), documentation for the latest version: <https://docs.espressif.com/projects/esp-idf/>

- Documentation of ESP-MQTT API: <https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/protocols/mqtt.html>

## License

- MQTT Package - [Stephen Robinson - contiki-mqtt](https://github.com/esar/contiki-mqtt)
- Others [@tuanpmt](https://twitter.com/tuanpmt)
Apache License

## Older IDF verisons

For [ESP-IDF](https://github.com/espressif/esp-idf) versions prior to IDFv3.2, please  clone as a component of [ESP-IDF](https://github.com/espressif/esp-idf):

```
git submodule add https://github.com/espressif/esp-mqtt.git components/espmqtt
```

and checkout the [ESP-MQTT_FOR_IDF_3.1](https://github.com/espressif/esp-mqtt/tree/ESP-MQTT_FOR_IDF_3.1) tag
