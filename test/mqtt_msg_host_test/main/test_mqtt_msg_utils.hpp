/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#pragma once

#include <exception>
#include <rapidcheck.h>
#include <algorithm>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

extern "C" {
#include "mqtt5_msg.h"
#include "mqtt_msg.h"
}

namespace mqtt_msg_test
{

constexpr auto mqtt_fixed_header_size = size_t{5};

enum class packet_type : uint8_t {
    connack = MQTT_MSG_TYPE_CONNACK << 4,
    publish = MQTT_MSG_TYPE_PUBLISH << 4,
    puback = MQTT_MSG_TYPE_PUBACK << 4,
    pubrec = MQTT_MSG_TYPE_PUBREC << 4,
    suback = MQTT_MSG_TYPE_SUBACK << 4,
    unsuback = MQTT_MSG_TYPE_UNSUBACK << 4,
    pingreq = MQTT_MSG_TYPE_PINGREQ << 4,
    pingresp = MQTT_MSG_TYPE_PINGRESP << 4,
    disconnect = MQTT_MSG_TYPE_DISCONNECT << 4,
};

struct mqtt_msg_deleter {
    void operator()(mqtt_message_t *message) const
    {
        mqtt_msg_destroy(message);
    }
};

using unique_msg = std::unique_ptr<mqtt_message_t, mqtt_msg_deleter>;

inline std::string generate_bytes(int length, int minimum, int maximum)
{
    auto value = std::string(static_cast<size_t>(length), '\0');

    for (auto &byte : value) {
        byte = static_cast<char>(*rc::gen::inRange(minimum, maximum));
    }

    return value;
}

class user_properties
{
public:
    mqtt5_user_property_handle_t handle = nullptr;

    user_properties() = default;
    user_properties(const user_properties &) = delete;
    user_properties &operator=(const user_properties &) = delete;

    user_properties(user_properties &&other) noexcept
        : handle(std::exchange(other.handle, nullptr)),
          expected_(std::move(other.expected_))
    {
    }

    user_properties &operator=(user_properties &&other) noexcept
    {
        if (this != &other) {
            mqtt5_msg_delete_user_property(handle);
            handle = std::exchange(other.handle, nullptr);
            expected_ = std::move(other.expected_);
        }

        return *this;
    }

    ~user_properties()
    {
        mqtt5_msg_delete_user_property(handle);
    }

    mqtt5_user_property_handle_t *out()
    {
        return &handle;
    }

    static user_properties generate()
    {
        auto properties = user_properties{};
        const auto count = static_cast<size_t>(*rc::gen::inRange(0, 4));

        for (auto i = size_t{0}; i < count; ++i) {
            auto key = generate_bytes(*rc::gen::inRange(1, 33), 1, 127);
            auto value = generate_bytes(*rc::gen::inRange(1, 97), 1, 127);
            RC_ASSERT(mqtt5_msg_set_user_property(
                          properties.out(), key.data(), key.size(), value.data(), value.size()) == ESP_OK);
            properties.expected_.emplace_back(std::move(key), std::move(value));
        }

        return properties;
    }

    void assert_equal(mqtt5_user_property_handle_t actual) const
    {
        if (expected_.empty()) {
            RC_ASSERT(!static_cast<bool>(actual));
            return;
        }

        RC_ASSERT(static_cast<bool>(actual));
        RC_ASSERT(mqtt5_msg_get_user_property_count(actual) == expected_.size());
        auto decoded_items = decoded(expected_.size());
        auto decoded_count = static_cast<uint8_t>(decoded_items.size());
        RC_ASSERT(mqtt5_msg_get_user_property(actual, decoded_items.data(), &decoded_count) == ESP_OK);
        RC_ASSERT(decoded_count == expected_.size());

        for (auto i = size_t{0}; i < expected_.size(); ++i) {
            RC_ASSERT(decoded_items.data()[i].key == expected_[i].first);
            RC_ASSERT(decoded_items.data()[i].value == expected_[i].second);
        }
    }

private:
    class decoded
    {
    public:
        explicit decoded(size_t size)
            : items_(size)
        {
        }

        decoded(const decoded &) = delete;
        decoded &operator=(const decoded &) = delete;

        ~decoded()
        {
            std::ranges::for_each(items_, [](const auto & item) {
                free(const_cast<char *>(item.key));
                free(const_cast<char *>(item.value));
            });
        }

        esp_mqtt5_user_property_item_t *data()
        {
            return items_.data();
        }

        size_t size() const
        {
            return items_.size();
        }

    private:
        std::vector<esp_mqtt5_user_property_item_t> items_;
    };

    std::vector<std::pair<std::string, std::string>> expected_;
};

inline std::vector<uint8_t> generated_packet_with_type(packet_type type)
{
    auto packet = generate_bytes(*rc::gen::inRange(1, 65), 0, 256);
    packet[0] = static_cast<char>(std::to_underlying(type));
    return {packet.begin(), packet.end()};
}

inline std::vector<uint8_t> generated_packet()
{
    return generated_packet_with_type(*rc::gen::element(
                                          packet_type::connack,
                                          packet_type::publish,
                                          packet_type::puback,
                                          packet_type::pubrec,
                                          packet_type::suback,
                                          packet_type::unsuback,
                                          packet_type::pingreq,
                                          packet_type::pingresp,
                                          packet_type::disconnect));
}

inline void for_each_packet_prefix(std::vector<uint8_t> packet, std::invocable<uint8_t *, size_t> auto check)
{
    for (auto prefix_length = size_t{0}; prefix_length <= packet.size(); ++prefix_length) {
        auto prefix = std::vector<uint8_t>(packet.begin(), packet.begin() + prefix_length);
        check(prefix.data(), prefix.size());
    }
}

inline void for_each_packet_prefix(packet_type type, std::invocable<uint8_t *, size_t> auto check)
{
    for_each_packet_prefix(generated_packet_with_type(type), check);
}

inline void for_each_generated_packet_prefix(std::invocable<uint8_t *, size_t> auto check)
{
    for_each_packet_prefix(generated_packet(), check);
}

inline bool is_pointer_in_range(const uint8_t *buffer, size_t buffer_length, const char *data, size_t data_length)
{
    if (data == nullptr) {
        return false;
    }

    const auto begin = reinterpret_cast<uintptr_t>(buffer);
    const auto end = begin + buffer_length;
    const auto pointer = reinterpret_cast<uintptr_t>(data);
    return pointer >= begin && pointer <= end && data_length <= end - pointer;
}

inline std::string_view as_string_view(const char *data, size_t length)
{
    return {data, length};
}

inline std::string_view as_string_view(const uint8_t *data, size_t length)
{
    return {reinterpret_cast<const char *>(data), length};
}

} // namespace mqtt_msg_test
