| Supported Targets | Linux |
| ----------------- | ----- |

# Description

This directory contains test code for the mqtt client that runs on host.

Tests are written using [Catch2](https://github.com/catchorg/Catch2) test framework 

# Build

Tests build regularly like an idf project. 

```
idf.py build
```

# Run

The build produces an executable in the build folder. 

Just run:

```
./build/host_mqtt_client_test.elf
```

The test executable have some options provided by the test framework. 

# Log capture

Some behaviors inside `esp-mqtt` only surface through log output (e.g. debug
breadcrumbs on internal decisions). The `test::esp_log::Capture` utility in
`main/test_log_intercept.{hpp,cpp}` lets tests assert on those without the
component having to expose internal state through its public API.

`Capture` is an RAII guard: construction installs a `vprintf` hook on the
esp-log system, destruction restores the previous one. While alive, it parses
each log line into `Entry { level, tag, message }` records and still forwards
the text to the original `vprintf` so test output stays visible. Only one
`Capture` may be alive at a time; constructing a second one throws
`std::logic_error`.

Asserting on captured output uses either `Capture`'s own predicates or the
Catch2 matchers in `main/test_log_matchers.{hpp,cpp}`:

```cpp
#include "test_log_intercept.hpp"
#include "test_log_matchers.hpp"

TEST_CASE("client logs its core-selection decision") {
    esp_log_level_set("mqtt_client", ESP_LOG_DEBUG);
    test::esp_log::Capture log;

    // ... exercise the code under test ...

    // Plain boolean check (no matcher machinery):
    REQUIRE(log.contains("mqtt_client", "Core selection"));

    // Catch2 matcher (nicer failure output on REQUIRE_THAT):
    using namespace test::esp_log::matchers;
    REQUIRE_THAT(log, HasMessageIn("mqtt_client", "Core selection"));
}
```

The log parser assumes the stock esp-log text format with colors disabled;
`test/host/sdkconfig.defaults` sets `CONFIG_LOG_COLORS=n` and
`CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y` so DEBUG-level messages are visible (call
sites still need `esp_log_level_set(tag, ESP_LOG_DEBUG)` for their tag). A
guard test in `main/test_log_parser.cpp` round-trips real `ESP_LOGx` output
through `Capture`, so if the IDF log format ever changes the failure surfaces
there rather than in every downstream test.
