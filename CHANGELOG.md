# Changelog

## [1.1.0](https://github.com/espressif/esp-mqtt/compare/v1.0.0...v1.1.0) (2026-07-15)


### Features

* Add API to get MQTT client's status ([77a875c](https://github.com/espressif/esp-mqtt/commit/77a875c804c544fe8a9cf7856cfbb06d9c360dc6))
* Add mqtt conformance test app ([37a2e55](https://github.com/espressif/esp-mqtt/commit/37a2e555c57dc1a82ea474031a2b3790556d0eb4))
* Add support for percent-encoding in password and it's corresponding test ([32df7e2](https://github.com/espressif/esp-mqtt/commit/32df7e27fc2497ae38eb76f2aafef83ecebce310))
* Adds support to use PSA opaque DS driver ([11f070c](https://github.com/espressif/esp-mqtt/commit/11f070c685d98d026d4975695daa788498318357))
* Adds support to use PSA opaque DS driver ([aea4f3e](https://github.com/espressif/esp-mqtt/commit/aea4f3ef0dc6ba003c8526b8f968168dc8bb7b25))
* **mqtt5:** Exposes server properties in connect event ([e121e1b](https://github.com/espressif/esp-mqtt/commit/e121e1b34b9dfab7eec7b8014576d8672e8f0e31)), closes [#325](https://github.com/espressif/esp-mqtt/issues/325)
* **mqtt:** Add API to get MQTT client's status ([b6e8c65](https://github.com/espressif/esp-mqtt/commit/b6e8c65305dae8544f1c59b11e7cdaad7aa0730d))
* **mqtt:** Conformance test infrastructure (DUT app + CI) ([3f62c75](https://github.com/espressif/esp-mqtt/commit/3f62c7578160a0a398ab6959c3048559df68c958))
* Provide reason code in MQTT event ([f1fc00c](https://github.com/espressif/esp-mqtt/commit/f1fc00ca4436ad9b7e786dc0c1d63d382bf78e61))


### Bug Fixes

* Change CI Images from gitlab to Docker ([e5cf3d2](https://github.com/espressif/esp-mqtt/commit/e5cf3d25549bd92dd6ae357d4b7e98c74b7f4f42))
* **examples:** Add nvs_flash dependency explicitly ([d289d73](https://github.com/espressif/esp-mqtt/commit/d289d73fc635de6b6f7f45cc0359c1675c11ff0b))
* Fix signed integer overflow in remaining length decoding ([ffd44fb](https://github.com/espressif/esp-mqtt/commit/ffd44fb42438f62c740e941fe566b40838fc295b))
* Fixed uri inconsistency when updated ([736d440](https://github.com/espressif/esp-mqtt/commit/736d4403616da2f99377c1747429e231aea8ffb0))
* Mqtt examples default broker ([6cf94c4](https://github.com/espressif/esp-mqtt/commit/6cf94c4011cbcd6711dd8c7e7e40b53259291027))
* **mqtt_msg:** Fix signed integer overflow in remaining length decoding ([689a265](https://github.com/espressif/esp-mqtt/commit/689a2656a76f10466c58c1278498eca2eff38925))
* **mqtt5:** Fix UB in variable len processing ([a5695b0](https://github.com/espressif/esp-mqtt/commit/a5695b05f378a26edfecfe821fbcbd0e314672b4))
* **mqtt5:** Fix UB in variable len processing ([3c5777d](https://github.com/espressif/esp-mqtt/commit/3c5777d706a3d0cff6dad364bce0b0b5c0740fd3))
* **mqtt5:** Remove in_buffer_length constraint on maximum_packet_size ([08f0d26](https://github.com/espressif/esp-mqtt/commit/08f0d26bb3ac6b8343b54df17b46eddc5c240f38))
* **mqtt5:** Removes incorrect reason code verification when resending pubrel ([3fdfc06](https://github.com/espressif/esp-mqtt/commit/3fdfc0617e404ac3df795cf1bbea9c23180aa120)), closes [#333](https://github.com/espressif/esp-mqtt/issues/333)
* **mqtt5:** Sanitize property len/types to harden mqtt5-msg ([6f15500](https://github.com/espressif/esp-mqtt/commit/6f155005df004a25bff1323dfec29e03632998d4))
* **mqtt5:** Sanitize propery len/types to harden mqtt5-msg ([e427ebb](https://github.com/espressif/esp-mqtt/commit/e427ebb7eae801a5302ceec2a830ee4a8d6afff3))
* **mqtt:** Fix unused-but-set-variable warning ([3a15be4](https://github.com/espressif/esp-mqtt/commit/3a15be48079e82bd167a93e283c51341271a3462))
* Prevent control messages to be counted in mqtt5 ([cf77940](https://github.com/espressif/esp-mqtt/commit/cf779408ebd4e4e792a3df86c21402c9346f5830))
* Prevent control messages to be counted in mqtt5 ([7f0da30](https://github.com/espressif/esp-mqtt/commit/7f0da30addd6acb5d699164d6d474db1a0a09def))
* Removes forward / from topics in examples and test ([55ee628](https://github.com/espressif/esp-mqtt/commit/55ee628c30b71f0a1283db3a718ea3bc7f2a0e47))
* Removes forward / from topics in examples and test ([4c57d31](https://github.com/espressif/esp-mqtt/commit/4c57d316c02eae17e7f4b8e50e9a4c3bbeda78ce))
* Update images from Docker to Gitlab container ([9198843](https://github.com/espressif/esp-mqtt/commit/91988439b553ba749d213a6c8efaa7ae514b93d6))


### Documentation

* Add advice regarding outbox message pile-up ([ee9472f](https://github.com/espressif/esp-mqtt/commit/ee9472f12fbe52f7c0ec253dd80948b0df77a90f))
* Add advice regarding outbox message pile-up to the documentation ([0069d74](https://github.com/espressif/esp-mqtt/commit/0069d74433be617656b191ad4cde407501f472b8))
* Clarify defauls on configuation options ([4c0edaf](https://github.com/espressif/esp-mqtt/commit/4c0edaf09af59d8e32cb2581e233c4b87509a6d6))
* Clarify defauls on configuation options ([dbc6fed](https://github.com/espressif/esp-mqtt/commit/dbc6fed4f6282c62d1ab4e22bb1e304d3c752fcf))
* Clarify that esp_mqtt_client_reconnect behaviour ([c4ca04f](https://github.com/espressif/esp-mqtt/commit/c4ca04f88c903aead6dacf291b1b6851d2cf25e6))
* Clarify when MQTT_EVENT_DISCONNECTED and MQTT_EVENT_ERROR are dispatched ([a84ecb3](https://github.com/espressif/esp-mqtt/commit/a84ecb365306c6a772729776fc537d7cb22b2847))
* Fix documentation link ([367a469](https://github.com/espressif/esp-mqtt/commit/367a469911d1cd808cca41fb84a875c9f54b100b))
* **mqtt:** Clarify outbox limit (bytes) vs buffer.out_size; add sizing guidance ([b2df2e4](https://github.com/espressif/esp-mqtt/commit/b2df2e4f72fd61622a8952398219cea30f853ee9))
* Re-enable zh_CN build ([4cfd15f](https://github.com/espressif/esp-mqtt/commit/4cfd15f0a918701c23c81be1a9829be845f09f81))
* Re-enable zh_CN build ([42cce9f](https://github.com/espressif/esp-mqtt/commit/42cce9f86fb25dd629e7d009755e77d73a1ae446))
