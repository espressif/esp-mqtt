/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "test_mqtt_msg_utils.hpp"
#include <catch2/catch_test_macros.hpp>
#include <rapidcheck.h>
#include <rapidcheck/catch.h>
#include <array>
#include <cstdint>
#include <utility>

extern "C" {
#include "esp_log.h"
#include "mqtt5_msg.h"
#include "mqtt_msg.h"
}

using namespace mqtt_msg_test;

TEST_CASE("mqtt5 reason-code parser rejects a truncated property length")
{
    auto suback_with_trailing_reason = std::array<uint8_t, 6> {
        std::to_underlying(packet_type::suback), 3, 0, 1, 0, 0x80
    };
    REQUIRE(mqtt5_msg_get_reason_code(
                suback_with_trailing_reason.data(), suback_with_trailing_reason.size()) == -1);
}

TEST_CASE("mqtt_get_suback_data returns only views within its length argument")
{
    rc::prop("every non-null view lies within the packet prefix", []() {
        for_each_generated_packet_prefix([](uint8_t *packet, size_t prefix_length) {
            auto mqtt3_suback_length = prefix_length;
            auto *mqtt3_suback_data = mqtt_get_suback_data(packet, &mqtt3_suback_length);

            if (mqtt3_suback_data != nullptr) {
                RC_ASSERT(is_pointer_in_range(
                              packet, prefix_length, mqtt3_suback_data, mqtt3_suback_length));
            }
        });
    });
}

TEST_CASE("mqtt5_get_suback_data returns only views within its length argument")
{
    esp_log_level_set("mqtt5_msg", ESP_LOG_NONE);
    rc::prop("every non-null view lies within the packet prefix", []() {
        for_each_generated_packet_prefix([](uint8_t *packet, size_t prefix_length) {
            auto suback_length = prefix_length;
            auto suback_user_property = user_properties{};
            auto *suback_data = mqtt5_get_suback_data(packet, &suback_length, suback_user_property.out());

            if (suback_data != nullptr) {
                RC_ASSERT(is_pointer_in_range(packet, prefix_length, suback_data, suback_length));
            }
        });
    });
}
