/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "test_mqtt_msg_utils.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <rapidcheck.h>
#include <rapidcheck/catch.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

extern "C" {
#include "esp_log.h"
#include "mqtt5_msg.h"
#include "mqtt_msg.h"
}

using namespace mqtt_msg_test;

namespace
{

struct connack_outputs {
    mqtt_connect_info_t connection_info{};
    esp_mqtt5_connection_property_storage_t connection_property{};
    esp_mqtt5_connection_server_resp_property_t response_property{};
    int reason_code = 0;
    uint8_t ack_flag = 0;
    user_properties user_property;

    ~connack_outputs()
    {
        free(connection_info.client_id);
        free(response_property.response_info);
    }
};

} // namespace

TEST_CASE("mqtt5 CONNACK parser handles fixed-width property boundaries")
{
    const auto [property_id, property_width] = GENERATE(table<uint8_t, size_t>({
        {MQTT5_PROPERTY_SESSION_EXPIRY_INTERVAL, 4},
        {MQTT5_PROPERTY_RECEIVE_MAXIMUM, 2},
        {MQTT5_PROPERTY_MAXIMUM_QOS, 1},
        {MQTT5_PROPERTY_RETAIN_AVAILABLE, 1},
        {MQTT5_PROPERTY_TOPIC_ALIAS_MAXIMIM, 2},
        {MQTT5_PROPERTY_MAXIMUM_PACKET_SIZE, 4},
        {MQTT5_PROPERTY_WILDCARD_SUBSCR_AVAILABLE, 1},
        {MQTT5_PROPERTY_SUBSCR_IDENTIFIER_AVAILABLE, 1},
        {MQTT5_PROPERTY_SHARED_SUBSCR_AVAILABLE, 1},
        {MQTT5_PROPERTY_SERVER_KEEP_ALIVE, 2},
    }));
    const auto available = GENERATE_COPY(range(size_t{0}, property_width + 1));
    CAPTURE(property_id, available);
    auto packet = std::vector<uint8_t>(6 + available, 0);
    packet[0] = std::to_underlying(packet_type::connack);
    packet[1] = static_cast<uint8_t>(4 + available);
    packet[4] = static_cast<uint8_t>(1 + available);
    packet[5] = property_id;
    auto outputs = connack_outputs{};
    const auto result = mqtt5_msg_parse_connack_property(
                            packet.data(), packet.size(), &outputs.connection_info, &outputs.connection_property,
                            &outputs.response_property, &outputs.reason_code, &outputs.ack_flag,
                            outputs.user_property.out());
    REQUIRE(result == (available == property_width ? ESP_OK : ESP_FAIL));
}

TEST_CASE("mqtt5 CONNACK parser handles string property boundaries")
{
    const auto property_id = static_cast<uint8_t>(GENERATE(
                                                      MQTT5_PROPERTY_ASSIGNED_CLIENT_IDENTIFIER,
                                                      MQTT5_PROPERTY_REASON_STRING,
                                                      MQTT5_PROPERTY_RESP_INFO,
                                                      MQTT5_PROPERTY_SERVER_REFERENCE,
                                                      MQTT5_PROPERTY_AUTHENTICATION_METHOD,
                                                      MQTT5_PROPERTY_AUTHENTICATION_DATA));
    const auto property = std::vector<uint8_t> {property_id, 0, 3, 'a', 'b', 'c'};
    const auto available = GENERATE_COPY(range(size_t{0}, property.size() + 1));
    CAPTURE(property_id, available);
    auto packet = std::vector<uint8_t>(5 + available, 0);
    packet[0] = std::to_underlying(packet_type::connack);
    packet[1] = static_cast<uint8_t>(3 + property.size());
    packet[4] = static_cast<uint8_t>(property.size());
    std::copy_n(property.begin(), available, packet.begin() + 5);
    auto outputs = connack_outputs{};
    const auto result = mqtt5_msg_parse_connack_property(
                            packet.data(), packet.size(), &outputs.connection_info, &outputs.connection_property,
                            &outputs.response_property, &outputs.reason_code, &outputs.ack_flag,
                            outputs.user_property.out());
    REQUIRE(result == (available == property.size() ? ESP_OK : ESP_FAIL));
}

TEST_CASE("mqtt5 CONNACK parser handles user property boundaries")
{
    const auto user_property = std::vector<uint8_t> {
        MQTT5_PROPERTY_USER_PROPERTY, 0, 3, 'k', 'e', 'y', 0, 5, 'v', 'a', 'l', 'u', 'e',
    };
    const auto available = GENERATE_COPY(range(size_t{0}, user_property.size() + 1));
    CAPTURE(available);
    auto packet = std::vector<uint8_t>(5 + available, 0);
    packet[0] = std::to_underlying(packet_type::connack);
    packet[1] = static_cast<uint8_t>(3 + user_property.size());
    packet[4] = static_cast<uint8_t>(user_property.size());
    std::copy_n(user_property.begin(), available, packet.begin() + 5);
    auto outputs = connack_outputs{};
    const auto result = mqtt5_msg_parse_connack_property(
                            packet.data(), packet.size(), &outputs.connection_info, &outputs.connection_property,
                            &outputs.response_property, &outputs.reason_code, &outputs.ack_flag,
                            outputs.user_property.out());
    REQUIRE(result == (available == user_property.size() ? ESP_OK : ESP_FAIL));

    if (result == ESP_OK) {
        REQUIRE(mqtt5_msg_get_user_property_count(outputs.user_property.handle) == 1);
    }
}

TEST_CASE("mqtt5_msg_parse_connack_property bounds reads by buffer_len")
{
    esp_log_level_set("mqtt5_msg", ESP_LOG_NONE);
    rc::prop("no packet prefix causes an out-of-bounds access", []() {
        for_each_generated_packet_prefix([](uint8_t *packet, size_t prefix_length) {
            auto outputs = connack_outputs{};
            mqtt5_msg_parse_connack_property(
                packet, prefix_length, &outputs.connection_info, &outputs.connection_property,
                &outputs.response_property, &outputs.reason_code, &outputs.ack_flag,
                outputs.user_property.out());
        });
    });
}
