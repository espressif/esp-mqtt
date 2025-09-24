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

ESP-MQTT is available through the [ESP-IDF Component Manager](https://components.espressif.com/) and ships as a standard [ESP-IDF](https://github.com/espressif/esp-idf) component.

- To add it via the Component Manager (recommended), declare the dependency in your project's `idf_component.yml`, for example:

  ```yaml
  dependencies:
    espressif/mqtt: "*"
  ```

  Replace `*` with the version constraint you want to track, or run `idf.py add-dependency espressif/mqtt`.
- For local development, clone this repository as `mqtt` so the component name matches:

  ```bash
  git clone https://github.com/espressif/esp-mqtt.git mqtt
  ```

## Documentation

- Documentation of ESP-MQTT API: <https://docs.espressif.com/projects/esp-mqtt/en/master/esp32/index.html>

## License

- Apache License 2.0
- MQTT package origin: [Stephen Robinson - contiki-mqtt](https://github.com/esar/contiki-mqtt)
- Additional contributions by [@tuanpmt](https://twitter.com/tuanpmt)

## Older IDF versions

For [ESP-IDF](https://github.com/espressif/esp-idf) versions prior to IDFv3.2, please  clone as a component of [ESP-IDF](https://github.com/espressif/esp-idf):

```
git submodule add https://github.com/espressif/esp-mqtt.git components/espmqtt
```

and checkout the [ESP-MQTT_FOR_IDF_3.1](https://github.com/espressif/esp-mqtt/tree/ESP-MQTT_FOR_IDF_3.1) tag
