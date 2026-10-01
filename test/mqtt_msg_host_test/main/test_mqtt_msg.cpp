/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <catch2/catch_test_macros.hpp>
#include <rapidcheck.h>
#include <rapidcheck/catch.h>
#include <cstdint>
#include <cstring>
#include <memory>
#include <random>
#include <string>

extern "C" {
#include "mqtt_msg.h"

    int platform_random(int max)
    {
        if (max <= 0) {
            return 0;
        }

        static std::mt19937 engine{std::random_device{}()};
        std::uniform_int_distribution<int> distribution(0, max - 1);
        return distribution(engine);
    }
}

namespace
{

constexpr size_t mqtt_fixed_header_size = 5;

struct MessageDeleter {
    void operator()(mqtt_message_t *message) const
    {
        mqtt_msg_destroy(message);
    }
};

using MessagePtr = std::unique_ptr<mqtt_message_t, MessageDeleter>;

MessagePtr make_message(size_t buffer_size)
{
    return MessagePtr(mqtt_msg_create(buffer_size));
}

std::string generated_bytes(int length, int minimum, int maximum)
{
    std::string value(static_cast<size_t>(length), '\0');

    for (char &byte : value) {
        byte = static_cast<char>(*rc::gen::inRange(minimum, maximum));
    }

    return value;
}

} // namespace

TEST_CASE("mqtt_msg_pingreq encodes the fixed MQTT control packet")
{
    MessagePtr message = make_message(mqtt_fixed_header_size);
    REQUIRE(message != nullptr);
    mqtt_message_t *encoded = mqtt_msg_pingreq(message.get());
    REQUIRE(encoded == message.get());
    REQUIRE(encoded->length == 2);
    REQUIRE(mqtt_get_type(encoded->data) == MQTT_MSG_TYPE_PINGREQ);
    REQUIRE(encoded->data[0] == 0xC0);
    REQUIRE(encoded->data[1] == 0x00);
    REQUIRE(mqtt_has_valid_msg_hdr(encoded->data, encoded->length) == 1);
}

TEST_CASE("mqtt_msg_publish rejects an empty topic")
{
    MessagePtr message = make_message(32);
    REQUIRE(message != nullptr);
    uint16_t message_id = 7;
    mqtt_message_t *encoded = mqtt_msg_publish(message.get(), "", "payload", 7, 0, 0, &message_id);
    REQUIRE(encoded == message.get());
    REQUIRE(encoded->length == 0);
}

TEST_CASE("mqtt_msg_publish round-trips an empty payload")
{
    const int qos_values[] = {0, 1};

    for (const int qos : qos_values) {
        const size_t buffer_size = mqtt_fixed_header_size + 2 + 1 + (qos > 0 ? 2 : 0);
        MessagePtr message = make_message(buffer_size);
        REQUIRE(message != nullptr);
        uint16_t message_id = 0;
        mqtt_message_t *encoded = mqtt_msg_publish(message.get(), "t", nullptr, 0, qos, 0, &message_id);
        REQUIRE(encoded == message.get());
        REQUIRE(encoded->length > 0);
        size_t payload_length = encoded->length;
        const char *payload = mqtt_get_publish_data(encoded->data, &payload_length);
        REQUIRE(payload != nullptr);
        REQUIRE(payload_length == 0);

        if (qos == 0) {
            REQUIRE(message_id == 0);
        } else {
            REQUIRE(message_id != 0);
            REQUIRE(mqtt_get_id(encoded->data, encoded->length) == message_id);
        }
    }
}

TEST_CASE("mqtt_msg_publish round-trips a complete packet")
{
    rc::prop("encoded publish recovers topic, payload, flags, and packet id", []() {
        const int qos = *rc::gen::inRange(0, 3);
        const int retain = *rc::gen::inRange(0, 2);
        const std::string topic = generated_bytes(*rc::gen::inRange(1, 65), 1, 127);
        const std::string payload = generated_bytes(*rc::gen::inRange(0, 129), 0, 256);
        const size_t buffer_size = mqtt_fixed_header_size + 2 + topic.size() +
                                   (qos > 0 ? 2 : 0) + payload.size();
        MessagePtr message = make_message(buffer_size);
        RC_ASSERT(message != nullptr);
        uint16_t message_id = 0;
        mqtt_message_t *encoded = mqtt_msg_publish(message.get(), topic.c_str(), payload.data(),
                                                   static_cast<int>(payload.size()), qos, retain, &message_id);
        RC_ASSERT(encoded == message.get());
        RC_ASSERT(encoded->length > 0);
        RC_ASSERT(encoded->length <= buffer_size);
        int fixed_header_size = 0;
        const size_t total_length = mqtt_get_total_length(encoded->data, encoded->length, &fixed_header_size);
        RC_ASSERT(total_length == encoded->length);
        RC_ASSERT(fixed_header_size >= 2);
        RC_ASSERT(mqtt_get_type(encoded->data) == MQTT_MSG_TYPE_PUBLISH);
        RC_ASSERT(mqtt_get_qos(encoded->data) == qos);
        RC_ASSERT(mqtt_get_retain(encoded->data) == retain);
        RC_ASSERT(mqtt_get_dup(encoded->data) == 0);
        RC_ASSERT(mqtt_has_valid_msg_hdr(encoded->data, encoded->length) == 1);
        size_t topic_length = encoded->length;
        const char *encoded_topic = mqtt_get_publish_topic(encoded->data, &topic_length);
        RC_ASSERT(encoded_topic != nullptr);
        RC_ASSERT(topic_length == topic.size());
        RC_ASSERT(std::memcmp(encoded_topic, topic.data(), topic.size()) == 0);
        size_t payload_length = encoded->length;
        const char *encoded_payload = mqtt_get_publish_data(encoded->data, &payload_length);
        RC_ASSERT(encoded_payload != nullptr);
        RC_ASSERT(payload_length == payload.size());
        RC_ASSERT(std::memcmp(encoded_payload, payload.data(), payload.size()) == 0);

        if (qos == 0) {
            RC_ASSERT(message_id == 0);
        } else {
            RC_ASSERT(message_id != 0);
            RC_ASSERT(mqtt_get_id(encoded->data, encoded->length) == message_id);
        }
    });
}
