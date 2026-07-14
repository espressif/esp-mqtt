# MQTT conformance app (HIL)

This app exposes a console API for pytest-embedded HIL tests that target MQTT conformance behavior.

## Console commands

- `init <base64_json>`: Create MQTT client from a base64-encoded JSON config (must include `uri`)
- `config <base64_json>`: Apply base64-encoded JSON config to initialized client
- `start`: Start MQTT client
- `stop`: Stop MQTT client
- `disconnect`: Request disconnect
- `reconnect`: Request reconnect
- `destroy`: Destroy MQTT client
- `subscribe <topic> <qos>`: Subscribe to topic
- `unsubscribe <topic>`: Unsubscribe topic
- `publish <topic> <pattern> <pattern_repetitions> <qos> <retain> <enqueue>`: Publish payload

## JSON config keys

All configuration is passed as a base64-encoded JSON object with exactly one top-level key, naming which config category the blob targets. The JSON shape mirrors the real esp_mqtt C struct layout, so field names/paths match `mqtt_client.h` / `mqtt5_client.h` directly.

### `mqtt_config` (used with `init`)

Mirrors `esp_mqtt_client_config_t`'s nesting:

```json
{
  "mqtt_config": {
    "broker": { "address": { "uri": "mqtt://192.168.1.1:1883" } },
    "credentials": { "client_id": "my-client" },
    "session": { "keepalive": 30, "disable_clean_session": false, "protocol_ver": 3 },
    "network": { "disable_auto_reconnect": true }
  }
}
```

| Path | Type | Description |
|------|------|-------------|
| `broker.address.uri` | string | Broker URI (e.g. `mqtt://192.168.1.1:1883`) |
| `credentials.client_id` | string | Client identifier |
| `session.keepalive` | int | Keepalive interval (seconds) |
| `session.disable_clean_session` | bool | `true` = persistent session (clean start = false) |
| `session.protocol_ver` | int | Raw `esp_mqtt_protocol_ver_t` ordinal: `0`=UNDEFINED, `1`=MQTT 3.1, `2`=MQTT 3.1.1, `3`=MQTT 5.0 |
| `network.disable_auto_reconnect` | bool | Disable MQTT client automatic reconnect |

### `connect_property` (MQTT5 connect properties)

Already flat in C, so the JSON object is flat too:

| Key | Type | Description |
|-----|------|-------------|
| `session_expiry_interval` | int | Session expiry (seconds) |
| `receive_maximum` | int | Receive maximum |
| `topic_alias_maximum` | int | Topic alias maximum |
| `maximum_packet_size` | int | Maximum packet size |
| `will_delay_interval` | int | Will delay interval (seconds) |

### `publish_property` (MQTT5 publish properties)

| Key | Type | Description |
|-----|------|-------------|
| `message_expiry_interval` | int | Message expiry (seconds) |
| `payload_format_indicator` | bool | `true` = UTF-8 encoded payload |
| `topic_alias` | int | Topic alias |
| `content_type` | string | Content type |
| `response_topic` | string | Response topic |

### `subscribe_property` (MQTT5 subscribe properties)

| Key | Type | Description |
|-----|------|-------------|
| `subscribe_id` | int | Subscription identifier |
| `no_local_flag` | bool | No local flag |
| `retain_as_published_flag` | bool | Retain as published flag |
| `retain_handle` | int | Retain handling option (0/1/2) |
| `is_share_subscribe` | bool | Shared subscription flag |
| `share_name` | string | Shared subscription group name |

### `disconnect_property` (MQTT5 disconnect properties)

| Key | Type | Description |
|-----|------|-------------|
| `session_expiry_interval` | int | Session expiry override on disconnect |
| `disconnect_reason` | int | Disconnect reason code |

## Conformance mapping

Each pytest case should document the MQTT specification section it validates where practical.

The paho reference suite is integrated as git submodule at:

`test/tools/paho.mqtt.testing`

## Running tests locally

From the repository root (or the mqtt worktree root if using worktrees):

1. Ensure the environment is active (e.g. `direnv allow` at repo root so IDF and pytest-embedded are available).

2. Initialize the paho.mqtt.testing submodule:
   ```bash
   git submodule update --init --recursive test/tools/paho.mqtt.testing
   ```

3. Run the conformance tests (connect a board with Ethernet, or use the same target/port as in CI):
   ```bash
   pytest test/apps/mqtt_conformance/ -v
   ```
   To run a single test or filter by keyword, add e.g. `-k test_mqtt_v311` or the test path.
