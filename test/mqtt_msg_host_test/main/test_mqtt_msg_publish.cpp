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
#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <utility>

extern "C" {
#include "esp_log.h"
#include "mqtt5_msg.h"
#include "mqtt_msg.h"
}

using namespace mqtt_msg_test;

namespace
{

struct mqtt5_publish_view {
    char *topic = nullptr;
    size_t topic_length = 0;
    esp_mqtt5_publish_resp_property_t property = {};
    uint16_t property_length = 0;
    char *payload = nullptr;
    size_t payload_length = 0;
    user_properties user_property;
};

std::expected<mqtt5_publish_view, esp_err_t> parse_mqtt5_publish(uint8_t *packet, size_t packet_length)
{
    auto view = mqtt5_publish_view{};
    view.payload = mqtt5_get_publish_property_payload(
                       packet, packet_length, &view.topic, &view.topic_length, &view.property,
                       &view.property_length, &view.payload_length, view.user_property.out());

    if (view.payload == nullptr) {
        return std::unexpected(ESP_FAIL);
    }

    return view;
}

} // namespace

TEST_CASE("mqtt_msg_publish rejects an empty topic")
{
    auto message = unique_msg(mqtt_msg_create(32));
    REQUIRE(message != nullptr);
    auto message_id = uint16_t{7};
    auto *encoded = mqtt_msg_publish(message.get(), "", "payload", 7, 0, 0, &message_id);
    REQUIRE(encoded == message.get());
    REQUIRE(encoded->length == 0);
}

TEST_CASE("mqtt5 publish parser respects the declared packet length")
{
    auto publish_with_trailing_property = std::array<uint8_t, 6> {
        std::to_underlying(packet_type::publish), 3, 0, 1, 't', 0
    };
    const auto view = parse_mqtt5_publish(
                          publish_with_trailing_property.data(), publish_with_trailing_property.size());
    REQUIRE_FALSE(view.has_value());
}

TEST_CASE("mqtt_get_publish_topic returns only views within its length argument")
{
    rc::prop("every non-null view lies within the packet prefix", []() {
        for_each_generated_packet_prefix([](uint8_t *packet, size_t prefix_length) {
            auto topic_length = prefix_length;
            auto *topic = mqtt_get_publish_topic(packet, &topic_length);

            if (topic != nullptr) {
                RC_ASSERT(is_pointer_in_range(packet, prefix_length, topic, topic_length));
            }
        });
    });
}

TEST_CASE("mqtt_get_publish_data returns only views within its length argument")
{
    rc::prop("every non-null view lies within the packet prefix", []() {
        for_each_generated_packet_prefix([](uint8_t *packet, size_t prefix_length) {
            auto payload_length = prefix_length;
            auto *payload = mqtt_get_publish_data(packet, &payload_length);

            if (payload != nullptr) {
                RC_ASSERT(is_pointer_in_range(packet, prefix_length, payload, payload_length));
            }
        });
    });
}

TEST_CASE("mqtt5_get_publish_property_payload returns only views within its length argument")
{
    esp_log_level_set("mqtt5_msg", ESP_LOG_NONE);
    rc::prop("every non-null view lies within the packet prefix", []() {
        for_each_generated_packet_prefix([](uint8_t *packet, size_t prefix_length) {
            const auto view = parse_mqtt5_publish(packet, prefix_length);

            if (view.has_value()) {
                RC_ASSERT(is_pointer_in_range(packet, prefix_length, view->topic, view->topic_length));
                RC_ASSERT(is_pointer_in_range(packet, prefix_length, view->payload, view->payload_length));
            }
        });
    });
}

TEST_CASE("mqtt_msg_publish accepts a null zero-length payload")
{
    const auto qos = GENERATE(0, 1, 2);
    CAPTURE(qos);
    const auto buffer_size = mqtt_fixed_header_size + 2 + 1 + (qos > 0 ? 2 : 0);
    auto message = unique_msg(mqtt_msg_create(buffer_size));
    REQUIRE(message != nullptr);
    auto message_id = uint16_t{0};
    auto *encoded = mqtt_msg_publish(message.get(), "t", nullptr, 0, qos, 0, &message_id);
    REQUIRE(encoded == message.get());
    REQUIRE(encoded->length > 0);
    auto payload_length = encoded->length;
    const auto *payload = mqtt_get_publish_data(encoded->data, &payload_length);
    REQUIRE(payload != nullptr);
    REQUIRE(payload_length == 0);
    REQUIRE(mqtt_get_qos(encoded->data) == qos);

    if (qos == 0) {
        REQUIRE(message_id == 0);
    } else {
        REQUIRE(message_id != 0);
        REQUIRE(mqtt_get_id(encoded->data, encoded->length) == message_id);
    }
}

TEST_CASE("mqtt_msg_publish round-trips a complete packet")
{
    rc::prop("encoded publish recovers topic, payload, flags, and packet id", []() {
        const auto qos = *rc::gen::inRange(0, 3);
        const auto retain = *rc::gen::inRange(0, 2);
        const auto topic = generate_bytes(*rc::gen::inRange(1, 65), 1, 127);
        const auto payload = generate_bytes(*rc::gen::inRange(0, 129), 0, 256);
        const auto buffer_size = mqtt_fixed_header_size + 2 + topic.size() +
                                 (qos > 0 ? 2 : 0) + payload.size();
        auto message = unique_msg(mqtt_msg_create(buffer_size));
        RC_ASSERT(message != nullptr);
        auto message_id = uint16_t{0};
        auto *encoded = mqtt_msg_publish(message.get(), topic.c_str(), payload.data(),
                                         static_cast<int>(payload.size()), qos, retain, &message_id);
        RC_ASSERT(encoded == message.get());
        RC_ASSERT(encoded->length > 0);
        RC_ASSERT(encoded->length <= buffer_size);
        auto fixed_header_size = 0;
        const auto total_length = mqtt_get_total_length(encoded->data, encoded->length, &fixed_header_size);
        RC_ASSERT(total_length == encoded->length);
        RC_ASSERT(fixed_header_size >= 2);
        RC_ASSERT(mqtt_get_type(encoded->data) == MQTT_MSG_TYPE_PUBLISH);
        RC_ASSERT(mqtt_get_qos(encoded->data) == qos);
        RC_ASSERT(mqtt_get_retain(encoded->data) == retain);
        RC_ASSERT(mqtt_get_dup(encoded->data) == 0);
        RC_ASSERT(mqtt_has_valid_msg_hdr(encoded->data, encoded->length) == 1);
        auto topic_length = encoded->length;
        const auto *encoded_topic = mqtt_get_publish_topic(encoded->data, &topic_length);
        RC_ASSERT(encoded_topic != nullptr);
        RC_ASSERT(topic_length == topic.size());
        RC_ASSERT(as_string_view(encoded_topic, topic_length) == topic);
        auto payload_length = encoded->length;
        const auto *encoded_payload = mqtt_get_publish_data(encoded->data, &payload_length);
        RC_ASSERT(encoded_payload != nullptr);
        RC_ASSERT(payload_length == payload.size());
        RC_ASSERT(as_string_view(encoded_payload, payload_length) == payload);

        if (qos == 0) {
            RC_ASSERT(message_id == 0);
        } else {
            RC_ASSERT(message_id != 0);
            RC_ASSERT(mqtt_get_id(encoded->data, encoded->length) == message_id);
        }
    });
}

TEST_CASE("mqtt5 publish properties round-trip")
{
    rc::prop("encoded topic, payload, properties, and user properties are recovered", []() {
        const auto topic = generate_bytes(*rc::gen::inRange(1, 65), 1, 127);
        const auto payload = generate_bytes(*rc::gen::inRange(0, 129), 0, 256);
        const auto response_topic = generate_bytes(*rc::gen::inRange(1, 65), 1, 127);
        const auto content_type = generate_bytes(*rc::gen::inRange(1, 65), 1, 127);
        const auto correlation_data = generate_bytes(*rc::gen::inRange(0, 65), 0, 256);
        const auto qos = *rc::gen::inRange(0, 3);
        const auto retain = *rc::gen::inRange(0, 2);
        const auto include_response_topic = *rc::gen::arbitrary<bool>();
        const auto include_content_type = *rc::gen::arbitrary<bool>();
        const auto include_correlation_data = *rc::gen::arbitrary<bool>();
        auto encoded_user_property = user_properties::generate();
        auto property = esp_mqtt5_publish_property_config_t{};
        property.payload_format_indicator = *rc::gen::arbitrary<bool>();
        property.message_expiry_interval = static_cast<uint32_t>(*rc::gen::inRange(0, 100000));
        property.topic_alias = static_cast<uint16_t>(*rc::gen::inRange(0, 65536));
        property.response_topic = include_response_topic ? response_topic.c_str() : nullptr;
        property.correlation_data = include_correlation_data ? correlation_data.data() : nullptr;
        property.correlation_data_len = include_correlation_data
                                        ? static_cast<uint16_t>(correlation_data.size())
                                        : 0;
        property.content_type = include_content_type ? content_type.c_str() : nullptr;
        property.user_property = encoded_user_property.handle;
        auto message = unique_msg(mqtt_msg_create(4096));
        RC_ASSERT(message != nullptr);
        auto message_id = uint16_t{0};
        auto *encoded = mqtt5_msg_publish(
                            message.get(), topic.c_str(), payload.data(), static_cast<int>(payload.size()), qos,
                            retain, &message_id, &property, nullptr);
        RC_ASSERT(encoded == message.get());
        RC_ASSERT(encoded->length > 0);
        RC_ASSERT(mqtt_get_total_length(encoded->data, encoded->length, nullptr) == encoded->length);
        RC_ASSERT(mqtt_get_type(encoded->data) == MQTT_MSG_TYPE_PUBLISH);
        RC_ASSERT(mqtt_get_qos(encoded->data) == qos);
        RC_ASSERT(mqtt_get_retain(encoded->data) == retain);
        RC_ASSERT(mqtt_get_dup(encoded->data) == 0);

        if (qos == 0) {
            RC_ASSERT(message_id == 0);
        } else {
            RC_ASSERT(message_id != 0);
            RC_ASSERT(mqtt5_get_id(encoded->data, encoded->length) == message_id);
        }

        const auto decoded = parse_mqtt5_publish(encoded->data, encoded->length);
        RC_ASSERT(decoded.has_value());
        RC_ASSERT(decoded->topic_length == topic.size());
        RC_ASSERT(as_string_view(decoded->topic, decoded->topic_length) == topic);
        RC_ASSERT(decoded->payload_length == payload.size());
        RC_ASSERT(as_string_view(decoded->payload, decoded->payload_length) == payload);
        RC_ASSERT(decoded->property.payload_format_indicator == property.payload_format_indicator);
        RC_ASSERT(decoded->property.message_expiry_interval == property.message_expiry_interval);
        RC_ASSERT(decoded->property.topic_alias == property.topic_alias);

        if (include_response_topic) {
            RC_ASSERT(decoded->property.response_topic != nullptr);
            RC_ASSERT(static_cast<size_t>(decoded->property.response_topic_len) == response_topic.size());
            RC_ASSERT(as_string_view(decoded->property.response_topic,
                                     decoded->property.response_topic_len) == response_topic);
        } else {
            RC_ASSERT(decoded->property.response_topic == nullptr);
        }

        if (include_content_type) {
            RC_ASSERT(decoded->property.content_type != nullptr);
            RC_ASSERT(static_cast<size_t>(decoded->property.content_type_len) == content_type.size());
            RC_ASSERT(as_string_view(decoded->property.content_type,
                                     decoded->property.content_type_len) == content_type);
        } else {
            RC_ASSERT(decoded->property.content_type == nullptr);
        }

        if (include_correlation_data && !correlation_data.empty()) {
            RC_ASSERT(decoded->property.correlation_data != nullptr);
            RC_ASSERT(decoded->property.correlation_data_len == correlation_data.size());
            RC_ASSERT(as_string_view(decoded->property.correlation_data,
                                     decoded->property.correlation_data_len) == correlation_data);
        } else {
            RC_ASSERT(decoded->property.correlation_data == nullptr);
        }

        encoded_user_property.assert_equal(decoded->user_property.handle);
    });
}

TEST_CASE("mqtt5 publish accepts a topic alias without a topic")
{
    rc::prop("a non-zero topic alias makes an empty topic valid", []() {
        const auto payload = generate_bytes(*rc::gen::inRange(0, 129), 0, 256);
        const auto qos = *rc::gen::inRange(0, 3);
        const auto topic_alias = static_cast<uint16_t>(*rc::gen::inRange(1, 65536));
        auto property = esp_mqtt5_publish_property_config_t{};
        property.topic_alias = topic_alias;
        auto message = unique_msg(mqtt_msg_create(4096));
        RC_ASSERT(message != nullptr);
        auto message_id = uint16_t{0};
        auto *encoded = mqtt5_msg_publish(
                            message.get(), "", payload.data(), static_cast<int>(payload.size()), qos,
                            0, &message_id, &property, nullptr);
        RC_ASSERT(encoded == message.get());
        RC_ASSERT(encoded->length > 0);
        const auto decoded = parse_mqtt5_publish(encoded->data, encoded->length);
        RC_ASSERT(decoded.has_value());
        RC_ASSERT(decoded->topic != nullptr);
        RC_ASSERT(decoded->topic_length == 0);
        RC_ASSERT(decoded->property.topic_alias == topic_alias);
        RC_ASSERT(decoded->payload_length == payload.size());
        RC_ASSERT(as_string_view(decoded->payload, decoded->payload_length) == payload);
    });
}

TEST_CASE("mqtt5 publish parser rejects every truncation before the payload")
{
    rc::prop("no incomplete variable header is accepted", []() {
        const auto topic = generate_bytes(*rc::gen::inRange(1, 65), 1, 127);
        const auto payload = generate_bytes(*rc::gen::inRange(0, 129), 0, 256);
        const auto response_topic = generate_bytes(*rc::gen::inRange(1, 65), 1, 127);
        const auto content_type = generate_bytes(*rc::gen::inRange(1, 65), 1, 127);
        const auto correlation_data = generate_bytes(*rc::gen::inRange(1, 65), 0, 256);
        const auto qos = *rc::gen::inRange(0, 3);
        auto property = esp_mqtt5_publish_property_config_t{};
        property.payload_format_indicator = *rc::gen::arbitrary<bool>();
        property.message_expiry_interval = static_cast<uint32_t>(*rc::gen::inRange(0, 100000));
        property.topic_alias = static_cast<uint16_t>(*rc::gen::inRange(0, 65536));
        property.response_topic = response_topic.c_str();
        property.correlation_data = correlation_data.data();
        property.correlation_data_len = static_cast<uint16_t>(correlation_data.size());
        property.content_type = content_type.c_str();
        auto message = unique_msg(mqtt_msg_create(4096));
        RC_ASSERT(message != nullptr);
        auto message_id = uint16_t{0};
        auto *encoded = mqtt5_msg_publish(
                            message.get(), topic.c_str(), payload.data(), static_cast<int>(payload.size()), qos,
                            0, &message_id, &property, nullptr);
        RC_ASSERT(encoded == message.get());
        RC_ASSERT(encoded->length > 0);
        const auto decoded = parse_mqtt5_publish(encoded->data, encoded->length);
        RC_ASSERT(decoded.has_value());
        const auto payload_offset = static_cast<size_t>(
                                        reinterpret_cast<uint8_t *>(decoded->payload) - encoded->data);

        for (auto prefix_length = size_t{0}; prefix_length < payload_offset; ++prefix_length) {
            const auto truncated = parse_mqtt5_publish(encoded->data, prefix_length);
            RC_ASSERT(!truncated.has_value());
        }
    });
}

TEST_CASE("fragmented mqtt publish reassembles")
{
    rc::prop("mqtt3 fragment metadata and bytes reconstruct the complete packet", []() {
        const auto topic = generate_bytes(*rc::gen::inRange(1, 33), 1, 127);
        const auto payload = generate_bytes(*rc::gen::inRange(8, 257), 0, 256);
        const auto qos = *rc::gen::inRange(0, 3);
        const auto included_payload = static_cast<size_t>(
                                          *rc::gen::inRange(0, static_cast<int>(payload.size())));
        const auto variable_header_length = 2 + topic.size() + (qos > 0 ? 2 : 0);
        const auto buffer_size = mqtt_fixed_header_size + variable_header_length + included_payload;
        auto message = unique_msg(mqtt_msg_create(buffer_size));
        RC_ASSERT(message != nullptr);
        auto message_id = uint16_t{0};
        auto *encoded = mqtt_msg_publish(
                            message.get(), topic.c_str(), payload.data(), static_cast<int>(payload.size()), qos, 0,
                            &message_id);
        RC_ASSERT(encoded->length > 0);
        RC_ASSERT(encoded->fragmented_msg_total_length ==
                  mqtt_fixed_header_size + variable_header_length + payload.size());
        RC_ASSERT(encoded->fragmented_msg_data_offset <= encoded->length);
        RC_ASSERT(encoded->length - encoded->fragmented_msg_data_offset == included_payload);
        auto fixed_header_size = 0;
        const auto complete_wire_length = mqtt_get_total_length(
                                              encoded->data, encoded->length, &fixed_header_size);
        RC_ASSERT(complete_wire_length ==
                  static_cast<size_t>(fixed_header_size) + variable_header_length + payload.size());
        auto reassembled = unique_msg(mqtt_msg_create(complete_wire_length));
        RC_ASSERT(reassembled != nullptr);
        RC_ASSERT(mqtt_msg_copy(reassembled.get(), encoded) == ESP_OK);
        RC_ASSERT(mqtt_msg_append(
                      reassembled.get(),
                      reinterpret_cast<const uint8_t *>(payload.data() + included_payload),
                      payload.size() - included_payload) == ESP_OK);
        RC_ASSERT(reassembled->length == complete_wire_length);
        auto decoded_topic_length = reassembled->length;
        auto *decoded_topic = mqtt_get_publish_topic(reassembled->data, &decoded_topic_length);
        RC_ASSERT(decoded_topic != nullptr);
        RC_ASSERT(decoded_topic_length == topic.size());
        RC_ASSERT(as_string_view(decoded_topic, decoded_topic_length) == topic);
        auto decoded_payload_length = reassembled->length;
        auto *decoded_payload = mqtt_get_publish_data(reassembled->data, &decoded_payload_length);
        RC_ASSERT(decoded_payload != nullptr);
        RC_ASSERT(decoded_payload_length == payload.size());
        RC_ASSERT(as_string_view(decoded_payload, decoded_payload_length) == payload);
    });
    rc::prop("mqtt5 fragment metadata and bytes reconstruct the complete packet", []() {
        const auto topic = generate_bytes(*rc::gen::inRange(1, 33), 1, 127);
        const auto payload = generate_bytes(*rc::gen::inRange(8, 257), 0, 256);
        const auto qos = *rc::gen::inRange(0, 3);
        const auto included_payload = static_cast<size_t>(
                                          *rc::gen::inRange(0, static_cast<int>(payload.size())));
        const auto variable_header_length = 2 + topic.size() + (qos > 0 ? 2 : 0) + 1;
        const auto buffer_size = mqtt_fixed_header_size + variable_header_length + included_payload;
        auto message = unique_msg(mqtt_msg_create(buffer_size));
        RC_ASSERT(message != nullptr);
        auto message_id = uint16_t{0};
        auto *encoded = mqtt5_msg_publish(
                            message.get(), topic.c_str(), payload.data(), static_cast<int>(payload.size()), qos, 0,
                            &message_id, nullptr, nullptr);
        RC_ASSERT(encoded->length > 0);
        RC_ASSERT(encoded->fragmented_msg_total_length ==
                  mqtt_fixed_header_size + variable_header_length + payload.size());
        RC_ASSERT(encoded->fragmented_msg_data_offset <= encoded->length);
        RC_ASSERT(encoded->length - encoded->fragmented_msg_data_offset == included_payload);
        const auto complete_wire_length = mqtt_get_total_length(
                                              encoded->data, encoded->length, nullptr);
        auto reassembled = unique_msg(mqtt_msg_create(complete_wire_length));
        RC_ASSERT(reassembled != nullptr);
        RC_ASSERT(mqtt_msg_copy(reassembled.get(), encoded) == ESP_OK);
        RC_ASSERT(mqtt_msg_append(
                      reassembled.get(),
                      reinterpret_cast<const uint8_t *>(payload.data() + included_payload),
                      payload.size() - included_payload) == ESP_OK);
        const auto decoded = parse_mqtt5_publish(reassembled->data, reassembled->length);
        RC_ASSERT(decoded.has_value());
        RC_ASSERT(decoded->topic_length == topic.size());
        RC_ASSERT(as_string_view(decoded->topic, decoded->topic_length) == topic);
        RC_ASSERT(decoded->payload_length == payload.size());
        RC_ASSERT(as_string_view(decoded->payload, decoded->payload_length) == payload);
    });
}

TEST_CASE("mqtt5_msg_publish joins response_topic and resp_info without truncation")
{
    auto property = esp_mqtt5_publish_property_config_t{};
    property.response_topic = "response";
    auto message = unique_msg(mqtt_msg_create(64));
    REQUIRE(message != nullptr);
    auto message_id = uint16_t{1};
    auto *encoded = mqtt5_msg_publish(message.get(), "t", "x", 1, 1, 0, &message_id, &property, "info");
    REQUIRE(encoded->length > 0);
    const auto decoded = parse_mqtt5_publish(encoded->data, encoded->length);
    REQUIRE(decoded.has_value());
    REQUIRE(as_string_view(decoded->property.response_topic, decoded->property.response_topic_len) == "response/info");
}
