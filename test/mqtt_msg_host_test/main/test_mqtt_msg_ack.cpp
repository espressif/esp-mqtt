/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "test_mqtt_msg_utils.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

extern "C" {
#include "mqtt5_msg.h"
#include "mqtt_msg.h"
}

using namespace mqtt_msg_test;

TEST_CASE("mqtt5 ACK parsers release stale user properties")
{
    const auto empty_packet = uint8_t{0};
    auto stale_property = user_properties{};
    REQUIRE(mqtt5_msg_set_user_property(
                stale_property.out(), "key", 3, "value", 5) == ESP_OK);
    auto length = size_t{0};
    SECTION("PUBACK") {
        REQUIRE(mqtt5_get_puback_data(
                    const_cast<uint8_t *>(&empty_packet), &length,
                    stale_property.out()) == nullptr);
    }
    SECTION("SUBACK") {
        REQUIRE(mqtt5_get_suback_data(
                    const_cast<uint8_t *>(&empty_packet), &length,
                    stale_property.out()) == nullptr);
    }
    REQUIRE(stale_property.handle == nullptr);
}

TEST_CASE("mqtt_msg_puback serializes a valid MQTT3 PUBACK packet")
{
    auto message = unique_msg(mqtt_msg_create(7));
    REQUIRE(message != nullptr);
    auto *encoded = mqtt_msg_puback(message.get(), 0x1234);
    REQUIRE(encoded->length == 4);
    REQUIRE(mqtt_get_type(encoded->data) == MQTT_MSG_TYPE_PUBACK);
    REQUIRE(mqtt_has_valid_msg_hdr(encoded->data, encoded->length) == 1);
    REQUIRE(mqtt_get_id(encoded->data, encoded->length) == 0x1234);
}

TEST_CASE("mqtt5_msg_puback serializes a valid MQTT5 PUBACK packet")
{
    auto message = unique_msg(mqtt_msg_create(9));
    REQUIRE(message != nullptr);
    auto *encoded = mqtt5_msg_puback(message.get(), 0x5678);
    REQUIRE(encoded->length == 6);
    REQUIRE(mqtt_get_type(encoded->data) == MQTT_MSG_TYPE_PUBACK);
    REQUIRE(mqtt_has_valid_msg_hdr(encoded->data, encoded->length) == 1);
    REQUIRE(mqtt5_get_id(encoded->data, encoded->length) == 0x5678);
    REQUIRE(mqtt5_msg_get_reason_code(encoded->data, encoded->length) == 0);
}

TEST_CASE("mqtt_msg_pubrec serializes a valid MQTT3 PUBREC packet")
{
    auto message = unique_msg(mqtt_msg_create(7));
    REQUIRE(message != nullptr);
    auto *encoded = mqtt_msg_pubrec(message.get(), 0x1234);
    REQUIRE(encoded->length == 4);
    REQUIRE(mqtt_get_type(encoded->data) == MQTT_MSG_TYPE_PUBREC);
    REQUIRE(mqtt_has_valid_msg_hdr(encoded->data, encoded->length) == 1);
    REQUIRE(mqtt_get_id(encoded->data, encoded->length) == 0x1234);
}

TEST_CASE("mqtt5_msg_pubrec serializes a valid MQTT5 PUBREC packet")
{
    auto message = unique_msg(mqtt_msg_create(9));
    REQUIRE(message != nullptr);
    auto *encoded = mqtt5_msg_pubrec(message.get(), 0x5678);
    REQUIRE(encoded->length == 6);
    REQUIRE(mqtt_get_type(encoded->data) == MQTT_MSG_TYPE_PUBREC);
    REQUIRE(mqtt_has_valid_msg_hdr(encoded->data, encoded->length) == 1);
    REQUIRE(mqtt5_get_id(encoded->data, encoded->length) == 0x5678);
    REQUIRE(mqtt5_msg_get_reason_code(encoded->data, encoded->length) == 0);
}

TEST_CASE("mqtt_msg_pubrel serializes a valid MQTT3 PUBREL packet")
{
    auto message = unique_msg(mqtt_msg_create(7));
    REQUIRE(message != nullptr);
    auto *encoded = mqtt_msg_pubrel(message.get(), 0x1234);
    REQUIRE(encoded->length == 4);
    REQUIRE(mqtt_get_type(encoded->data) == MQTT_MSG_TYPE_PUBREL);
    REQUIRE(mqtt_has_valid_msg_hdr(encoded->data, encoded->length) == 1);
    REQUIRE(mqtt_get_id(encoded->data, encoded->length) == 0x1234);
}

TEST_CASE("mqtt5_msg_pubrel serializes a valid MQTT5 PUBREL packet")
{
    auto message = unique_msg(mqtt_msg_create(9));
    REQUIRE(message != nullptr);
    auto *encoded = mqtt5_msg_pubrel(message.get(), 0x5678);
    REQUIRE(encoded->length == 6);
    REQUIRE(mqtt_get_type(encoded->data) == MQTT_MSG_TYPE_PUBREL);
    REQUIRE(mqtt_has_valid_msg_hdr(encoded->data, encoded->length) == 1);
    REQUIRE(mqtt5_get_id(encoded->data, encoded->length) == 0x5678);
    REQUIRE(mqtt5_msg_get_reason_code(encoded->data, encoded->length) == 0);
}

TEST_CASE("mqtt_msg_pubcomp serializes a valid MQTT3 PUBCOMP packet")
{
    auto message = unique_msg(mqtt_msg_create(7));
    REQUIRE(message != nullptr);
    auto *encoded = mqtt_msg_pubcomp(message.get(), 0x1234);
    REQUIRE(encoded->length == 4);
    REQUIRE(mqtt_get_type(encoded->data) == MQTT_MSG_TYPE_PUBCOMP);
    REQUIRE(mqtt_has_valid_msg_hdr(encoded->data, encoded->length) == 1);
    REQUIRE(mqtt_get_id(encoded->data, encoded->length) == 0x1234);
}

TEST_CASE("mqtt5_msg_pubcomp serializes a valid MQTT5 PUBCOMP packet")
{
    auto message = unique_msg(mqtt_msg_create(9));
    REQUIRE(message != nullptr);
    auto *encoded = mqtt5_msg_pubcomp(message.get(), 0x5678);
    REQUIRE(encoded->length == 6);
    REQUIRE(mqtt_get_type(encoded->data) == MQTT_MSG_TYPE_PUBCOMP);
    REQUIRE(mqtt_has_valid_msg_hdr(encoded->data, encoded->length) == 1);
    REQUIRE(mqtt5_get_id(encoded->data, encoded->length) == 0x5678);
    REQUIRE(mqtt5_msg_get_reason_code(encoded->data, encoded->length) == 0);
}
