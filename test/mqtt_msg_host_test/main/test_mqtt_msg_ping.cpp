/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "test_mqtt_msg_utils.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <array>
#include <cstdint>
#include <utility>

extern "C" {
#include "mqtt_msg.h"
}

using namespace mqtt_msg_test;

TEST_CASE("mqtt_msg_pingreq encodes the fixed MQTT control packet")
{
    auto message = unique_msg(mqtt_msg_create(mqtt_fixed_header_size));
    REQUIRE(message != nullptr);
    auto *encoded = mqtt_msg_pingreq(message.get());
    REQUIRE(encoded == message.get());
    REQUIRE(encoded->length == 2);
    REQUIRE(mqtt_get_type(encoded->data) == MQTT_MSG_TYPE_PINGREQ);
    REQUIRE(encoded->data[0] == std::to_underlying(packet_type::pingreq));
    REQUIRE(encoded->data[1] == 0x00);
    REQUIRE(mqtt_has_valid_msg_hdr(encoded->data, encoded->length) == 1);
}

TEST_CASE("mqtt_has_valid_msg_hdr accepts a well-formed inbound PINGRESP")
{
    auto packet = std::array<uint8_t, 2> {std::to_underlying(packet_type::pingresp), 0x00};
    REQUIRE(mqtt_get_type(packet.data()) == MQTT_MSG_TYPE_PINGRESP);
    auto fixed_header_size = 0;
    REQUIRE(mqtt_get_total_length(packet.data(), packet.size(), &fixed_header_size) == packet.size());
    REQUIRE(fixed_header_size == 2);
    REQUIRE(mqtt_has_valid_msg_hdr(packet.data(), packet.size()) == 1);
}

TEST_CASE("mqtt_has_valid_msg_hdr rejects reserved PINGRESP flags")
{
    const auto flags = GENERATE(range(1, 16));
    CAPTURE(flags);
    auto packet = std::array<uint8_t, 2> {
        static_cast<uint8_t>(std::to_underlying(packet_type::pingresp) | flags), 0x00
    };
    REQUIRE(mqtt_has_valid_msg_hdr(packet.data(), packet.size()) == 0);
}
