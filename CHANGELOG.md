# Changelog

## [1.1.0](https://github.com/espressif/esp-mqtt/compare/v1.0.0...v1.1.0) (2026-07-30)


### Features

* Add support for percent-encoding in password and it's corresponding test ([32df7e2](https://github.com/espressif/esp-mqtt/commit/32df7e27fc2497ae38eb76f2aafef83ecebce310))
* Adds support to use PSA opaque DS driver ([11f070c](https://github.com/espressif/esp-mqtt/commit/11f070c685d98d026d4975695daa788498318357))
* Allows esp-mqtt task and work buffers to be allocated in external memory ([b21e6bb](https://github.com/espressif/esp-mqtt/commit/b21e6bb3d4129ea61077a51ce7423579be46c7f3))
* **mqtt5:** Exposes server properties in connect event ([e121e1b](https://github.com/espressif/esp-mqtt/commit/e121e1b34b9dfab7eec7b8014576d8672e8f0e31)), closes [#325](https://github.com/espressif/esp-mqtt/issues/325)
* **mqtt:** Add API to get MQTT client's status ([b6e8c65](https://github.com/espressif/esp-mqtt/commit/b6e8c65305dae8544f1c59b11e7cdaad7aa0730d))
* **mqtt:** Conformance test infrastructure (DUT app + CI) ([3f62c75](https://github.com/espressif/esp-mqtt/commit/3f62c7578160a0a398ab6959c3048559df68c958))
* Provide reason code in MQTT event ([f1fc00c](https://github.com/espressif/esp-mqtt/commit/f1fc00ca4436ad9b7e786dc0c1d63d382bf78e61))


### Bug Fixes

* Change CI Images from gitlab to Docker ([e5cf3d2](https://github.com/espressif/esp-mqtt/commit/e5cf3d25549bd92dd6ae357d4b7e98c74b7f4f42))
* **examples:** Add nvs_flash dependency explicitly ([d289d73](https://github.com/espressif/esp-mqtt/commit/d289d73fc635de6b6f7f45cc0359c1675c11ff0b))
* Fixed uri inconsistency when updated ([736d440](https://github.com/espressif/esp-mqtt/commit/736d4403616da2f99377c1747429e231aea8ffb0))
* Mqtt examples default broker ([6cf94c4](https://github.com/espressif/esp-mqtt/commit/6cf94c4011cbcd6711dd8c7e7e40b53259291027))
* **mqtt_msg:** Fix signed integer overflow in remaining length decoding ([689a265](https://github.com/espressif/esp-mqtt/commit/689a2656a76f10466c58c1278498eca2eff38925))
* **mqtt5:** Fix mqtt5 max inflight message counting ([20b6eb0](https://github.com/espressif/esp-mqtt/commit/20b6eb0e30376703df70c3fb87e20a5e0e80cdd9)), closes [#312](https://github.com/espressif/esp-mqtt/issues/312)
* **mqtt5:** Fix UB in variable len processing ([a5695b0](https://github.com/espressif/esp-mqtt/commit/a5695b05f378a26edfecfe821fbcbd0e314672b4))
* **mqtt5:** Remove in_buffer_length constraint on maximum_packet_size ([08f0d26](https://github.com/espressif/esp-mqtt/commit/08f0d26bb3ac6b8343b54df17b46eddc5c240f38))
* **mqtt5:** Removes incorrect reason code verification when resending pubrel ([3fdfc06](https://github.com/espressif/esp-mqtt/commit/3fdfc0617e404ac3df795cf1bbea9c23180aa120)), closes [#333](https://github.com/espressif/esp-mqtt/issues/333)
* **mqtt5:** Sanitize property len/types to harden mqtt5-msg ([6f15500](https://github.com/espressif/esp-mqtt/commit/6f155005df004a25bff1323dfec29e03632998d4))
* **mqtt:** Fix unused-but-set-variable warning ([3a15be4](https://github.com/espressif/esp-mqtt/commit/3a15be48079e82bd167a93e283c51341271a3462))
* Prevent control messages to be counted in mqtt5 ([cf77940](https://github.com/espressif/esp-mqtt/commit/cf779408ebd4e4e792a3df86c21402c9346f5830))
* Removes forward / from topics in examples and test ([55ee628](https://github.com/espressif/esp-mqtt/commit/55ee628c30b71f0a1283db3a718ea3bc7f2a0e47))
* Update images from Docker to Gitlab container ([9198843](https://github.com/espressif/esp-mqtt/commit/91988439b553ba749d213a6c8efaa7ae514b93d6))


### Documentation

* Add advice regarding outbox message pile-up to the documentation ([0069d74](https://github.com/espressif/esp-mqtt/commit/0069d74433be617656b191ad4cde407501f472b8))
* Clarify defaults on configuration options ([4c0edaf](https://github.com/espressif/esp-mqtt/commit/4c0edaf09af59d8e32cb2581e233c4b87509a6d6))
* Clarify that esp_mqtt_client_reconnect behaviour ([c4ca04f](https://github.com/espressif/esp-mqtt/commit/c4ca04f88c903aead6dacf291b1b6851d2cf25e6))
* Clarify when MQTT_EVENT_DISCONNECTED and MQTT_EVENT_ERROR are dispatched ([a84ecb3](https://github.com/espressif/esp-mqtt/commit/a84ecb365306c6a772729776fc537d7cb22b2847))
* Fix documentation link ([367a469](https://github.com/espressif/esp-mqtt/commit/367a469911d1cd808cca41fb84a875c9f54b100b))
* **mqtt:** Clarify outbox limit (bytes) vs buffer.out_size; add sizing guidance ([b2df2e4](https://github.com/espressif/esp-mqtt/commit/b2df2e4f72fd61622a8952398219cea30f853ee9))
* Re-enable zh_CN build ([4cfd15f](https://github.com/espressif/esp-mqtt/commit/4cfd15f0a918701c23c81be1a9829be845f09f81))
