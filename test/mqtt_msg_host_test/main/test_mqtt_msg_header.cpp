/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "test_mqtt_msg_utils.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <rapidcheck.h>
#include <rapidcheck/catch.h>

extern "C" {
#include "esp_log.h"
#include "mqtt5_msg.h"
#include "mqtt_msg.h"
}

using namespace mqtt_msg_test;

TEST_CASE("mqtt_get_total_length bounds reads by its length argument")
{
    rc::prop("no packet prefix causes an out-of-bounds access", []() {
        for_each_generated_packet_prefix([](uint8_t *packet, size_t prefix_length) {
            auto fixed_header_size = 0;
            mqtt_get_total_length(packet, prefix_length, &fixed_header_size);
        });
    });
}

TEST_CASE("mqtt_header_complete bounds reads by its length argument")
{
    rc::prop("no packet prefix causes an out-of-bounds access", []() {
        for_each_generated_packet_prefix([](uint8_t *packet, size_t prefix_length) {
            mqtt_header_complete(packet, prefix_length);
        });
    });
}

TEST_CASE("mqtt_get_id bounds reads by its length argument")
{
    rc::prop("no packet prefix causes an out-of-bounds access", []() {
        for_each_generated_packet_prefix([](uint8_t *packet, size_t prefix_length) {
            mqtt_get_id(packet, prefix_length);
        });
    });
}

TEST_CASE("mqtt5_get_id bounds reads by its length argument")
{
    rc::prop("no packet prefix causes an out-of-bounds access", []() {
        for_each_generated_packet_prefix([](uint8_t *packet, size_t prefix_length) {
            mqtt5_get_id(packet, prefix_length);
        });
    });
}

TEST_CASE("mqtt_has_valid_msg_hdr bounds reads by its length argument")
{
    rc::prop("no packet prefix causes an out-of-bounds access", []() {
        for_each_generated_packet_prefix([](uint8_t *packet, size_t prefix_length) {
            mqtt_has_valid_msg_hdr(packet, prefix_length);
        });
    });
}

TEST_CASE("mqtt5_msg_get_reason_code bounds reads by its length argument")
{
    esp_log_level_set("mqtt5_msg", ESP_LOG_NONE);
    rc::prop("no packet prefix causes an out-of-bounds access", []() {
        for_each_generated_packet_prefix([](uint8_t *packet, size_t prefix_length) {
            mqtt5_msg_get_reason_code(packet, prefix_length);
        });
    });
}

TEST_CASE("remaining length encoding crosses variable-byte boundaries")
{
    const auto remaining_length = GENERATE(size_t{127}, size_t{128}, size_t{16383}, size_t{16384});
    CAPTURE(remaining_length);
    const auto payload_length = remaining_length - 3;
    auto payload = std::string(payload_length, 'p');
    const auto exact_buffer_size = mqtt_fixed_header_size + remaining_length;
    auto exact = unique_msg(mqtt_msg_create(exact_buffer_size));
    REQUIRE(exact != nullptr);
    auto message_id = uint16_t{0};
    auto *encoded = mqtt_msg_publish(
                        exact.get(), "t", payload.data(), static_cast<int>(payload.size()), 0, 0, &message_id);
    REQUIRE(encoded->length > 0);
    auto fixed_header_size = 0;
    REQUIRE(mqtt_get_total_length(encoded->data, encoded->length, &fixed_header_size) ==
            encoded->length);
    const auto expected_fixed_header_size = remaining_length <= 127 ? 2 :
                                            remaining_length <= 16383 ? 3 : 4;
    REQUIRE(fixed_header_size == expected_fixed_header_size);
    auto one_short = unique_msg(mqtt_msg_create(exact_buffer_size - 1));
    REQUIRE(one_short != nullptr);
    encoded = mqtt_msg_publish(
                  one_short.get(), "t", payload.data(), static_cast<int>(payload.size()), 0, 0, &message_id);
    REQUIRE(encoded->length > 0);
    REQUIRE(encoded->fragmented_msg_total_length == exact_buffer_size);
    REQUIRE(mqtt_get_total_length(encoded->data, encoded->length, nullptr) ==
            remaining_length + static_cast<size_t>(expected_fixed_header_size));
}
