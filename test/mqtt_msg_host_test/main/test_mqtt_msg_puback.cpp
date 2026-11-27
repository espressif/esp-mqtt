/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "test_mqtt_msg_utils.hpp"
#include <catch2/catch_test_macros.hpp>
#include <rapidcheck.h>
#include <rapidcheck/catch.h>
#include <cstdint>

extern "C" {
#include "esp_log.h"
#include "mqtt5_msg.h"
}

using namespace mqtt_msg_test;

TEST_CASE("mqtt5_get_puback_data returns only views within its length argument")
{
    esp_log_level_set("mqtt5_msg", ESP_LOG_NONE);
    rc::prop("every non-null view lies within the packet prefix", []() {
        for_each_generated_packet_prefix([](uint8_t *packet, size_t prefix_length) {
            auto ack_length = prefix_length;
            auto puback_user_property = user_properties{};
            auto *ack_data = mqtt5_get_puback_data(packet, &ack_length, puback_user_property.out());

            if (ack_data != nullptr) {
                RC_ASSERT(is_pointer_in_range(packet, prefix_length, ack_data, ack_length));
            }
        });
    });
}
