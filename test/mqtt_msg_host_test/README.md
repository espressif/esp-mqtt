# mqtt_msg host tests

Isolated host tests for `lib/mqtt_msg`. Tests call the message API directly —
no outbox, MQTT client, transport, or FreeRTOS scheduler required.

## Build and run

```bash
cd test/mqtt_msg_host_test
idf.py --preview set-target linux
idf.py build
./build/mqtt_msg_host_test.elf
```
