/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <type_traits>

#include "esp_err.h"
#include "esp_transport.h"
/* mqtt_client.h must come first: mqtt5_client.h sets its own include guard
   before including mqtt_client.h, so pulling it in first leaves mqtt_client.h
   without the MQTT5 types its own prototypes reference. */
#include "mqtt_client.h"
#include "mqtt5_client.h"

extern "C" {
    esp_err_t test_mqtt5_check_inflight_maximum(uint16_t send_count, uint16_t receive_maximum);
    int test_mqtt5_increment_packet_counter_with_dup(void);
    esp_err_t test_mqtt5_validate_subscribe_property(const esp_mqtt5_subscribe_property_config_t *property,
                                                     bool shared_available, esp_mqtt_protocol_ver_t protocol_ver);
    esp_err_t test_mqtt5_validate_unsubscribe_property(const esp_mqtt5_unsubscribe_property_config_t *property,
                                                       bool shared_available, esp_mqtt_protocol_ver_t protocol_ver);
    esp_err_t test_mqtt5_validate_publish_property(const esp_mqtt5_publish_property_config_t *property,
                                                   uint16_t topic_alias_maximum, esp_mqtt_protocol_ver_t protocol_ver);
    const void *test_mqtt5_staged_publish_property(esp_mqtt_client_handle_t client);

#include "Mockesp_event.h"
#include "Mockesp_timer.h"
#include "Mockesp_transport.h"
#include "Mockesp_transport_ssl.h"
#include "Mockesp_transport_tcp.h"
#include "Mockesp_transport_ws.h"
#include "Mockevent_groups.h"
#include "Mockqueue.h"
#include "Mocktask.h"
#if __has_include("Mockidf_additions.h")
    /* Some functions were moved from "task.h" to "idf_additions.h" */
#include "Mockidf_additions.h"
#endif
}

using unique_mqtt_client =
    std::unique_ptr < std::remove_pointer_t<esp_mqtt_client_handle_t>,
    decltype([](esp_mqtt_client_handle_t client)
{
    esp_mqtt_client_destroy(client);
}) >;

static esp_err_t validate_subscribe(const esp_mqtt5_subscribe_property_config_t &property, bool shared_available = true)
{
    return test_mqtt5_validate_subscribe_property(&property, shared_available, MQTT_PROTOCOL_V_5);
}

static esp_err_t validate_unsubscribe(const esp_mqtt5_unsubscribe_property_config_t &property,
                                      bool shared_available = true)
{
    return test_mqtt5_validate_unsubscribe_property(&property, shared_available, MQTT_PROTOCOL_V_5);
}

static esp_err_t validate_publish(const esp_mqtt5_publish_property_config_t &property, uint16_t topic_alias_maximum = 10)
{
    return test_mqtt5_validate_publish_property(&property, topic_alias_maximum, MQTT_PROTOCOL_V_5);
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

TEST_CASE("MQTT5 shared subscribe property needs a usable share name")
{
    // mqtt5_msg_subscribe() calls strlen(share_name) unconditionally once
    // is_share_subscribe is set, so these two must never reach the encoder.
    esp_mqtt5_subscribe_property_config_t no_name = {};
    no_name.is_share_subscribe = true;
    REQUIRE(validate_subscribe(no_name) == ESP_FAIL);
    esp_mqtt5_subscribe_property_config_t empty_name = {};
    empty_name.is_share_subscribe = true;
    empty_name.share_name = "";
    REQUIRE(validate_subscribe(empty_name) == ESP_FAIL);
    // Control: the same property with a share name is legal and must pass.
    esp_mqtt5_subscribe_property_config_t named = {};
    named.is_share_subscribe = true;
    named.share_name = "group";
    REQUIRE(validate_subscribe(named) == ESP_OK);
    // ... but only when the broker advertised shared-subscription support.
    REQUIRE(validate_subscribe(named, /*shared_available=*/false) == ESP_FAIL);
}

TEST_CASE("MQTT5 shared unsubscribe property needs a usable share name")
{
    esp_mqtt5_unsubscribe_property_config_t no_name = {};
    no_name.is_share_subscribe = true;
    REQUIRE(validate_unsubscribe(no_name) == ESP_FAIL);
    esp_mqtt5_unsubscribe_property_config_t named = {};
    named.is_share_subscribe = true;
    named.share_name = "group";
    REQUIRE(validate_unsubscribe(named) == ESP_OK);
    REQUIRE(validate_unsubscribe(named, /*shared_available=*/false) == ESP_FAIL);
}

TEST_CASE("MQTT5 rejects No Local on a shared subscription")
{
    // MQTT-3.8.3-4 forbids the combination; the broker answers with a
    // protocol-error DISCONNECT rather than a SUBACK.
    esp_mqtt5_subscribe_property_config_t property = {};
    property.is_share_subscribe = true;
    property.share_name = "group";
    property.no_local_flag = true;
    REQUIRE(validate_subscribe(property) == ESP_FAIL);
    // Control: No Local on a non-shared subscription is legal.
    esp_mqtt5_subscribe_property_config_t unshared = {};
    unshared.no_local_flag = true;
    REQUIRE(validate_subscribe(unshared) == ESP_OK);
}

TEST_CASE("MQTT5 subscribe retain_handle is limited to 0, 1, 2")
{
    esp_mqtt5_subscribe_property_config_t property = {};

    for (uint8_t handle = 0; handle <= 2; ++handle) {
        property.retain_handle = handle;
        REQUIRE(validate_subscribe(property) == ESP_OK);
    }

    property.retain_handle = 3;
    REQUIRE(validate_subscribe(property) == ESP_FAIL);
}

TEST_CASE("MQTT5 publish topic alias is bounded by the server maximum")
{
    esp_mqtt5_publish_property_config_t property = {};
    property.topic_alias = 11;
    REQUIRE(validate_publish(property, /*topic_alias_maximum=*/10) == ESP_FAIL);
    // Control: the maximum itself is a valid alias.
    property.topic_alias = 10;
    REQUIRE(validate_publish(property, /*topic_alias_maximum=*/10) == ESP_OK);
    // A server that advertised no alias support rejects every alias.
    property.topic_alias = 1;
    REQUIRE(validate_publish(property, /*topic_alias_maximum=*/0) == ESP_FAIL);
}

TEST_CASE("MQTT5 properties are rejected on a non-v5 connection")
{
    const esp_mqtt5_publish_property_config_t publish = {};
    const esp_mqtt5_subscribe_property_config_t subscribe = {};
    const esp_mqtt5_unsubscribe_property_config_t unsubscribe = {};
    REQUIRE(test_mqtt5_validate_publish_property(&publish, 10, MQTT_PROTOCOL_V_3_1_1) == ESP_FAIL);
    REQUIRE(test_mqtt5_validate_subscribe_property(&subscribe, true, MQTT_PROTOCOL_V_3_1_1) == ESP_FAIL);
    REQUIRE(test_mqtt5_validate_unsubscribe_property(&unsubscribe, true, MQTT_PROTOCOL_V_3_1_1) == ESP_FAIL);
    // A NULL property means "no properties" and is accepted on any version.
    REQUIRE(test_mqtt5_validate_publish_property(nullptr, 10, MQTT_PROTOCOL_V_3_1_1) == ESP_OK);
    REQUIRE(test_mqtt5_validate_subscribe_property(nullptr, true, MQTT_PROTOCOL_V_3_1_1) == ESP_OK);
    REQUIRE(test_mqtt5_validate_unsubscribe_property(nullptr, true, MQTT_PROTOCOL_V_3_1_1) == ESP_OK);
}

SCENARIO("MQTT5 per-message publish property does not consume the staged one")
{
    // Same mock preamble the mqtt_client host tests use to build a real client.
    int mtx = 0;
    int transport_list = 0;
    int transport = 0;
    int event_group = 0;
    esp_timer_get_time_IgnoreAndReturn(0);
    xQueueTakeMutexRecursive_IgnoreAndReturn(true);
    xQueueGiveMutexRecursive_IgnoreAndReturn(true);
    xQueueCreateMutex_ExpectAnyArgsAndReturn(reinterpret_cast<QueueHandle_t>(&mtx));
    xEventGroupCreate_IgnoreAndReturn(reinterpret_cast<EventGroupHandle_t>(&event_group));
    esp_transport_list_init_IgnoreAndReturn(reinterpret_cast<esp_transport_list_handle_t>(&transport_list));
    esp_transport_tcp_init_IgnoreAndReturn(reinterpret_cast<esp_transport_handle_t>(&transport));
    esp_transport_ssl_init_IgnoreAndReturn(reinterpret_cast<esp_transport_handle_t>(&transport));
    esp_transport_ws_init_IgnoreAndReturn(reinterpret_cast<esp_transport_handle_t>(&transport));
    esp_transport_ws_set_subprotocol_IgnoreAndReturn(ESP_OK);
    esp_transport_list_add_IgnoreAndReturn(ESP_OK);
    esp_transport_set_default_port_IgnoreAndReturn(ESP_OK);
    esp_event_loop_create_IgnoreAndReturn(ESP_OK);
    esp_event_loop_delete_IgnoreAndReturn(ESP_OK);
    esp_transport_list_destroy_IgnoreAndReturn(ESP_OK);
    esp_transport_destroy_IgnoreAndReturn(ESP_OK);
    xTaskCreatePinnedToCore_IgnoreAndReturn(pdTRUE);
    vEventGroupDelete_Ignore();
    vQueueDelete_Ignore();
    GIVEN("A v5 client with a staged publish property") {
        esp_mqtt_client_config_t config{};
        config.broker.address.uri = "mqtt://1.1.1.1";
        config.session.protocol_ver = MQTT_PROTOCOL_V_5;
        auto client = unique_mqtt_client{esp_mqtt_client_init(&config)};
        REQUIRE(client != nullptr);
        // The client stays disconnected on purpose: the publish still runs
        // through make_publish(), which is where the staged property is
        // consumed, and only then bails out at the "not connected" check.
        const esp_mqtt5_publish_property_config_t staged = { .message_expiry_interval = 60 };
        REQUIRE(esp_mqtt5_client_set_publish_property(client.get(), &staged) == ESP_OK);
        REQUIRE(test_mqtt5_staged_publish_property(client.get()) == &staged);
        WHEN("A per-message property is published") {
            const esp_mqtt5_publish_property_config_t per_message = { .message_expiry_interval = 5 };
            esp_mqtt_client_publish5(client.get(), "topic", "data", 0, 0, 0, &per_message);
            THEN("The staged property is left for the next plain publish") {
                // Otherwise a publish5() on one task silently strips the
                // properties off a publish() another task already staged.
                REQUIRE(test_mqtt5_staged_publish_property(client.get()) == &staged);
            }
        }
        WHEN("A plain publish is made") {
            esp_mqtt_client_publish(client.get(), "topic", "data", 0, 0, 0);
            THEN("The staged one-shot property is consumed as before") {
                REQUIRE(test_mqtt5_staged_publish_property(client.get()) == nullptr);
            }
        }
    }
}
