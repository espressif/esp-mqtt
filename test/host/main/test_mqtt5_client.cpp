/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

#include "esp_err.h"
#include "mqtt_client.h"
#include "mqtt5_client.h"

extern "C" {
    esp_err_t test_mqtt5_check_inflight_maximum(uint16_t send_count, uint16_t receive_maximum);
    int test_mqtt5_increment_packet_counter_with_dup(void);
    esp_err_t test_mqtt5_set_connect_null_property(void);
    esp_err_t test_mqtt5_set_publish_null_property(void);
    esp_err_t test_mqtt5_set_subscribe_null_property(void);
    esp_err_t test_mqtt5_set_unsubscribe_null_property(void);
    esp_err_t test_mqtt5_set_disconnect_null_property(void);
}

TEST_CASE("MQTT5 inflight quota uses an exact upper bound")
{
    REQUIRE(test_mqtt5_check_inflight_maximum(1, 2) == ESP_OK);
    REQUIRE(test_mqtt5_check_inflight_maximum(2, 2) == ESP_FAIL);
}

TEST_CASE("MQTT5 first send on a connection counts even when PUBLISH has DUP set")
{
    REQUIRE(test_mqtt5_increment_packet_counter_with_dup() == 1);
}

TEST_CASE("MQTT5 property setters reject a null property")
{
    REQUIRE(test_mqtt5_set_connect_null_property() == ESP_ERR_INVALID_ARG);
    REQUIRE(test_mqtt5_set_publish_null_property() == ESP_ERR_INVALID_ARG);
    REQUIRE(test_mqtt5_set_subscribe_null_property() == ESP_ERR_INVALID_ARG);
    REQUIRE(test_mqtt5_set_unsubscribe_null_property() == ESP_ERR_INVALID_ARG);
    REQUIRE(test_mqtt5_set_disconnect_null_property() == ESP_ERR_INVALID_ARG);
}

TEST_CASE("MQTT5 property setters reject a null client")
{
    esp_mqtt5_connection_property_config_t connect = {};
    esp_mqtt5_publish_property_config_t publish = {};
    esp_mqtt5_subscribe_property_config_t subscribe = {};
    esp_mqtt5_unsubscribe_property_config_t unsubscribe = {};
    esp_mqtt5_disconnect_property_config_t disconnect = {};
    REQUIRE(esp_mqtt5_client_set_connect_property(nullptr, &connect) == ESP_ERR_INVALID_ARG);
    REQUIRE(esp_mqtt5_client_set_publish_property(nullptr, &publish) == ESP_ERR_INVALID_ARG);
    REQUIRE(esp_mqtt5_client_set_subscribe_property(nullptr, &subscribe) == ESP_ERR_INVALID_ARG);
    REQUIRE(esp_mqtt5_client_set_unsubscribe_property(nullptr, &unsubscribe) == ESP_ERR_INVALID_ARG);
    REQUIRE(esp_mqtt5_client_set_disconnect_property(nullptr, &disconnect) == ESP_ERR_INVALID_ARG);
}
