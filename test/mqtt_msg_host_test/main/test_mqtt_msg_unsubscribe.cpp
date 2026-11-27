/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "test_mqtt_msg_utils.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>

extern "C" {
#include "mqtt5_msg.h"
#include "mqtt_msg.h"
}

using namespace mqtt_msg_test;

TEST_CASE("mqtt3 unsubscribe preserves its packet type and packet id")
{
    const auto topic = std::string{"sensor/one"};
    const auto buffer_size = mqtt_fixed_header_size + 2 + 2 + topic.size();
    auto message = unique_msg(mqtt_msg_create(buffer_size));
    REQUIRE(message != nullptr);
    auto message_id = uint16_t{0};
    auto *encoded = mqtt_msg_unsubscribe(message.get(), topic.c_str(), &message_id);
    REQUIRE(encoded->length > 0);
    REQUIRE(mqtt_get_type(encoded->data) == MQTT_MSG_TYPE_UNSUBSCRIBE);
    REQUIRE(mqtt_get_id(encoded->data, encoded->length) == message_id);
    REQUIRE(mqtt_has_valid_msg_hdr(encoded->data, encoded->length) == 1);
    auto short_message = unique_msg(mqtt_msg_create(buffer_size - 1));
    REQUIRE(short_message != nullptr);
    REQUIRE(mqtt_msg_unsubscribe(
                short_message.get(), topic.c_str(), &message_id)->length == 0);
}

TEST_CASE("mqtt5 unsubscribe preserves its packet type and packet id")
{
    const auto topic = std::string{"sensor/one"};
    const auto buffer_size = mqtt_fixed_header_size + 2 + 2 + topic.size() + 1;
    auto message = unique_msg(mqtt_msg_create(buffer_size));
    REQUIRE(message != nullptr);
    auto message_id = uint16_t{0};
    auto *encoded = mqtt5_msg_unsubscribe(message.get(), topic.c_str(), &message_id, nullptr);
    REQUIRE(encoded->length > 0);
    REQUIRE(mqtt_get_type(encoded->data) == MQTT_MSG_TYPE_UNSUBSCRIBE);
    REQUIRE(mqtt5_get_id(encoded->data, encoded->length) == message_id);
    REQUIRE(mqtt_has_valid_msg_hdr(encoded->data, encoded->length) == 1);
    auto short_message = unique_msg(mqtt_msg_create(buffer_size - 1));
    REQUIRE(short_message != nullptr);
    REQUIRE(mqtt5_msg_unsubscribe(
                short_message.get(), topic.c_str(), &message_id, nullptr)->length == 0);
}

TEST_CASE("mqtt5_msg_unsubscribe encodes shared subscription topic without truncation")
{
    auto property = esp_mqtt5_unsubscribe_property_config_t{};
    property.is_share_subscribe = true;
    property.share_name = "group";
    auto message = unique_msg(mqtt_msg_create(64));
    REQUIRE(message != nullptr);
    auto message_id = uint16_t{1};
    auto *encoded = mqtt5_msg_unsubscribe(message.get(), "a/b", &message_id, &property);
    REQUIRE(encoded->length > 0);
    const auto expected = std::string_view{"\x00\x10$share/group/a/b", 18};
    REQUIRE(as_string_view(encoded->data, encoded->length).find(expected) != std::string_view::npos);
    property.share_name = nullptr;
    REQUIRE(mqtt5_msg_unsubscribe(message.get(), "a/b", &message_id, &property)->length == 0);
}
