/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "test_mqtt_msg_utils.hpp"
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cstdint>
#include <string_view>

extern "C" {
#include "mqtt5_msg.h"
#include "mqtt_msg.h"
}

using namespace mqtt_msg_test;

namespace
{

struct subscription {
    std::string_view filter;
    int qos;
};

constexpr auto subscriptions = std::array{
    subscription{"sensor/one", 0},
    subscription{"sensor/+", 1},
    subscription{"system/#", 2},
};

auto mqtt_topics()
{
    auto topics = std::array<esp_mqtt_topic_t, subscriptions.size()> {};

    for (auto i = size_t{0}; i < topics.size(); ++i) {
        topics[i] = {subscriptions[i].filter.data(), subscriptions[i].qos};
    }

    return topics;
}

constexpr auto subscriptions_wire_length = []()
{
    auto length = size_t{0};

    for (const auto &subscription : subscriptions) {
        length += 2 + subscription.filter.size() + 1;
    }

    return length;
}();

void assert_encoded_subscriptions(const mqtt_message_t *encoded, size_t offset)
{
    for (const auto &subscription : subscriptions) {
        const auto filter_length = (encoded->data[offset] << 8) | encoded->data[offset + 1];
        offset += 2;
        REQUIRE(filter_length == subscription.filter.size());
        REQUIRE(as_string_view(encoded->data + offset, filter_length) == subscription.filter);
        offset += filter_length;
        REQUIRE(encoded->data[offset++] == subscription.qos);
    }

    REQUIRE(offset == encoded->length);
}

} // namespace

TEST_CASE("mqtt3 subscribe preserves topic filters, QoS values, and packet id")
{
    const auto topics = mqtt_topics();
    const auto buffer_size = mqtt_fixed_header_size + 2 + subscriptions_wire_length;
    auto message = unique_msg(mqtt_msg_create(buffer_size));
    REQUIRE(message != nullptr);
    auto message_id = uint16_t{0};
    auto *encoded = mqtt_msg_subscribe(message.get(), topics.data(), topics.size(), &message_id);
    REQUIRE(encoded->length > 0);
    REQUIRE(message_id != 0);
    REQUIRE(mqtt_get_type(encoded->data) == MQTT_MSG_TYPE_SUBSCRIBE);
    REQUIRE(mqtt_has_valid_msg_hdr(encoded->data, encoded->length) == 1);
    REQUIRE(mqtt_get_id(encoded->data, encoded->length) == message_id);
    auto fixed_header_size = 0;
    mqtt_get_total_length(encoded->data, encoded->length, &fixed_header_size);
    assert_encoded_subscriptions(encoded, static_cast<size_t>(fixed_header_size) + 2);
    auto short_message = unique_msg(mqtt_msg_create(buffer_size - 1));
    REQUIRE(short_message != nullptr);
    auto short_message_id = uint16_t{0};
    REQUIRE(mqtt_msg_subscribe(
                short_message.get(), topics.data(), topics.size(), &short_message_id)->length == 0);
}

TEST_CASE("mqtt5 subscribe preserves topic filters, QoS values, and packet id")
{
    const auto topics = mqtt_topics();
    const auto buffer_size = mqtt_fixed_header_size + 2 + 1 + subscriptions_wire_length;
    auto message = unique_msg(mqtt_msg_create(buffer_size));
    REQUIRE(message != nullptr);
    auto message_id = uint16_t{0};
    auto *encoded = mqtt5_msg_subscribe(
                        message.get(), topics.data(), topics.size(), &message_id, nullptr);
    REQUIRE(encoded->length > 0);
    REQUIRE(message_id != 0);
    REQUIRE(mqtt_get_type(encoded->data) == MQTT_MSG_TYPE_SUBSCRIBE);
    REQUIRE(mqtt_has_valid_msg_hdr(encoded->data, encoded->length) == 1);
    REQUIRE(mqtt5_get_id(encoded->data, encoded->length) == message_id);
    auto fixed_header_size = 0;
    mqtt_get_total_length(encoded->data, encoded->length, &fixed_header_size);
    auto offset = static_cast<size_t>(fixed_header_size) + 2;
    REQUIRE(encoded->data[offset++] == 0);
    assert_encoded_subscriptions(encoded, offset);
    auto short_message = unique_msg(mqtt_msg_create(buffer_size - 1));
    REQUIRE(short_message != nullptr);
    auto short_message_id = uint16_t{0};
    REQUIRE(mqtt5_msg_subscribe(
                short_message.get(), topics.data(), topics.size(), &short_message_id, nullptr)->length == 0);
}

TEST_CASE("mqtt5_msg_subscribe encodes shared subscription topic without truncation")
{
    const auto topic = esp_mqtt_topic_t{.filter = "a/b", .qos = 1};
    auto property = esp_mqtt5_subscribe_property_config_t{};
    property.is_share_subscribe = true;
    property.share_name = "group";
    auto message = unique_msg(mqtt_msg_create(64));
    REQUIRE(message != nullptr);
    auto message_id = uint16_t{1};
    auto *encoded = mqtt5_msg_subscribe(message.get(), &topic, 1, &message_id, &property);
    REQUIRE(encoded->length > 0);
    const auto expected = std::string_view{"\x00\x10$share/group/a/b", 18};
    REQUIRE(as_string_view(encoded->data, encoded->length).find(expected) != std::string_view::npos);
}
