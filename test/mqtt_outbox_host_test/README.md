# mqtt_outbox host tests

Isolated host tests for `lib/mqtt_outbox.c`. Tests call the outbox API directly —
no MQTT client, no transport, no FreeRTOS scheduler required.

## Build and run

```bash
cd test/mqtt_outbox_host_test
idf.py --preview set-target linux
idf.py build
./build/mqtt_outbox_host_test.elf
```

