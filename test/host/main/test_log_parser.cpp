/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Guard test: exercises test::esp_log::parse_line and an end-to-end Capture
 * round-trip through the real esp-log plumbing. Any upstream change to the
 * text format lands loudly here instead of in downstream matcher tests.
 */

#include "test_log_intercept.hpp"
#include "test_log_matchers.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

extern "C" {
#include "esp_log.h"
}

using namespace test::esp_log;
using namespace test::esp_log::matchers;

TEST_CASE("parse_line extracts level, tag, and message", "[log_parser]")
{
    SECTION("with timestamp") {
        auto entry = parse_line("I (123) mqtt_client: hello world");
        REQUIRE(entry.has_value());
        REQUIRE(entry->level == ESP_LOG_INFO);
        REQUIRE(entry->tag == "mqtt_client");
        REQUIRE(entry->message == "hello world");
    }
    SECTION("without timestamp") {
        auto entry = parse_line("W tag: warn msg");
        REQUIRE(entry.has_value());
        REQUIRE(entry->level == ESP_LOG_WARN);
        REQUIRE(entry->tag == "tag");
        REQUIRE(entry->message == "warn msg");
    }
    SECTION("trailing newline is stripped") {
        auto entry = parse_line("E (1) t: boom\n");
        REQUIRE(entry.has_value());
        REQUIRE(entry->message == "boom");
    }
    SECTION("non-log line returns nullopt") {
        REQUIRE_FALSE(parse_line("not a log line").has_value());
        REQUIRE_FALSE(parse_line("").has_value());
        REQUIRE_FALSE(parse_line("I no-colon-here").has_value());
    }
}

TEST_CASE("Capture round-trips real esp_log output", "[log_parser]")
{
    Capture logs;
    static const char *TAG = "log_parser_test";
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    ESP_LOGI(TAG, "hello %d", 42);
    ESP_LOGW(TAG, "careful");
    REQUIRE(logs.entries().size() == 2);
    const auto &first = logs.entries()[0];
    REQUIRE(first.level == ESP_LOG_INFO);
    REQUIRE(first.tag == TAG);
    REQUIRE(first.message == "hello 42");
    const auto &second = logs.entries()[1];
    REQUIRE(second.level == ESP_LOG_WARN);
    REQUIRE(second.tag == TAG);
    REQUIRE(second.message == "careful");
    REQUIRE_THAT(logs, HasMessage("hello 42"));
    REQUIRE_THAT(logs, HasMessageIn(TAG, "careful"));
    REQUIRE_THAT(logs, LogsInOrderIn(TAG, {"hello", "careful"}));
    REQUIRE_THAT(logs, LogsInOrderAny({"hello", "careful"}));
}
