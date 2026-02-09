# MQTT conformance app (HIL)

This app exposes a console API for pytest-embedded HIL tests that target MQTT conformance behavior.

## Console commands

- `init`: Create and configure MQTT client
- `set_uri <uri>`: Override broker URI before `start`
- `start`: Start MQTT client
- `stop`: Stop MQTT client
- `destroy`: Destroy MQTT client
- `subscribe <topic> <qos>`: Subscribe to topic
- `publish <topic> <pattern> <pattern_repetitions> <qos> <retain> <enqueue>`: Publish payload

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
