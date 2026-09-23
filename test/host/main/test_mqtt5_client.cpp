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
#include "Mockqueue.h"
#include "Mocktask.h"
#include "mqtt5_client_priv.h"

    esp_err_t test_mqtt5_check_inflight_maximum(uint16_t send_count, uint16_t receive_maximum);
    int test_mqtt5_increment_packet_counter_with_dup(void);
    esp_err_t test_mqtt5_set_connect_null_property(void);
    esp_err_t test_mqtt5_set_publish_null_property(void);
    esp_err_t test_mqtt5_set_subscribe_null_property(void);
    esp_err_t test_mqtt5_set_unsubscribe_null_property(void);
    esp_err_t test_mqtt5_set_disconnect_null_property(void);
    esp_mqtt_client_handle_t test_mqtt5_property_client_reset(void);
    mqtt5_staged_property_t *test_mqtt5_publish_property_slot(void);
    mqtt5_staged_property_t *test_mqtt5_subscribe_property_slot(void);
    mqtt5_staged_property_t *test_mqtt5_unsubscribe_property_slot(void);
}

static void use_task(TaskHandle_t task)
{
    xTaskGetCurrentTaskHandle_IgnoreAndReturn(task);
}

static esp_mqtt_client_handle_t prepare_property_client()
{
    xQueueTakeMutexRecursive_IgnoreAndReturn(pdTRUE);
    xQueueGiveMutexRecursive_IgnoreAndReturn(pdTRUE);
    return test_mqtt5_property_client_reset();
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

TEST_CASE("MQTT5 staged property enforces task ownership")
{
    int task_a_storage;
    int task_b_storage;
    int property_a = 1;
    int property_b = 2;
    int replacement = 3;
    TaskHandle_t task_a = reinterpret_cast<TaskHandle_t>(&task_a_storage);
    TaskHandle_t task_b = reinterpret_cast<TaskHandle_t>(&task_b_storage);
    mqtt5_staged_property_t slot = {};
    use_task(task_a);
    REQUIRE(esp_mqtt5_staged_property_set(&slot, &property_a) == ESP_OK);
    REQUIRE(esp_mqtt5_staged_property_get(&slot) == &property_a);
    use_task(task_b);
    REQUIRE(esp_mqtt5_staged_property_get(&slot) == nullptr);
    esp_mqtt5_staged_property_clear(&slot);
    REQUIRE(slot.property == &property_a);
    REQUIRE(slot.owner == task_a);
    REQUIRE(esp_mqtt5_staged_property_set(&slot, &property_b) == ESP_ERR_INVALID_STATE);
    REQUIRE(slot.property == &property_a);
    REQUIRE(slot.owner == task_a);
    use_task(task_a);
    REQUIRE(esp_mqtt5_staged_property_set(&slot, &replacement) == ESP_OK);
    REQUIRE(esp_mqtt5_staged_property_get(&slot) == &replacement);
    esp_mqtt5_staged_property_clear(&slot);
    REQUIRE(slot.property == nullptr);
    REQUIRE(slot.owner == nullptr);
    use_task(task_b);
    REQUIRE(esp_mqtt5_staged_property_set(&slot, &property_b) == ESP_OK);
    REQUIRE(esp_mqtt5_staged_property_get(&slot) == &property_b);
}

TEST_CASE("MQTT5 property setters use task-owned slots")
{
    int task_a_storage;
    int task_b_storage;
    TaskHandle_t task_a = reinterpret_cast<TaskHandle_t>(&task_a_storage);
    TaskHandle_t task_b = reinterpret_cast<TaskHandle_t>(&task_b_storage);
    esp_mqtt5_publish_property_config_t publish_a = {};
    esp_mqtt5_publish_property_config_t publish_b = {};
    esp_mqtt5_subscribe_property_config_t subscribe_a = {};
    esp_mqtt5_subscribe_property_config_t subscribe_b = {};
    esp_mqtt5_unsubscribe_property_config_t unsubscribe_a = {};
    esp_mqtt5_unsubscribe_property_config_t unsubscribe_b = {};
    esp_mqtt_client_handle_t client = prepare_property_client();
    use_task(task_a);
    REQUIRE(esp_mqtt5_client_set_publish_property(client, &publish_a) == ESP_OK);
    REQUIRE(esp_mqtt5_client_set_subscribe_property(client, &subscribe_a) == ESP_OK);
    REQUIRE(esp_mqtt5_client_set_unsubscribe_property(client, &unsubscribe_a) == ESP_OK);
    REQUIRE(test_mqtt5_publish_property_slot()->property == &publish_a);
    REQUIRE(test_mqtt5_subscribe_property_slot()->property == &subscribe_a);
    REQUIRE(test_mqtt5_unsubscribe_property_slot()->property == &unsubscribe_a);
    REQUIRE(test_mqtt5_publish_property_slot()->owner == task_a);
    REQUIRE(test_mqtt5_subscribe_property_slot()->owner == task_a);
    REQUIRE(test_mqtt5_unsubscribe_property_slot()->owner == task_a);
    use_task(task_b);
    REQUIRE(esp_mqtt5_client_set_publish_property(client, &publish_b) == ESP_ERR_INVALID_STATE);
    REQUIRE(esp_mqtt5_client_set_subscribe_property(client, &subscribe_b) == ESP_ERR_INVALID_STATE);
    REQUIRE(esp_mqtt5_client_set_unsubscribe_property(client, &unsubscribe_b) == ESP_ERR_INVALID_STATE);
    REQUIRE(test_mqtt5_publish_property_slot()->property == &publish_a);
    REQUIRE(test_mqtt5_subscribe_property_slot()->property == &subscribe_a);
    REQUIRE(test_mqtt5_unsubscribe_property_slot()->property == &unsubscribe_a);
}
