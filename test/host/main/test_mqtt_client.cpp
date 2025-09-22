/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <memory>
#include <net/if.h>
#include <random>
#include <string_view>
#include <type_traits>
#include "esp_transport.h"
#include <catch2/catch_test_macros.hpp>

#include "mqtt_client.h"
extern "C" {
#include "Mockesp_event.h"
#include "Mockesp_mac.h"
#include "Mockesp_transport.h"
#include "Mockesp_transport_ssl.h"
#include "Mockesp_transport_tcp.h"
#include "Mockesp_transport_ws.h"
#include "Mockevent_groups.h"
#include "Mockhttp_parser.h"
#include "Mockqueue.h"
#include "Mocktask.h"
#if __has_include ("Mockidf_additions.h")
/* Some functions were moved from "task.h" to "idf_additions.h" */
#include "Mockidf_additions.h"
#endif
#include "Mockesp_timer.h"

    /*
     * The following functions are not directly called but the generation of them
     * from cmock is broken, so we need to define them here.
     */
    esp_err_t esp_tls_get_and_clear_last_error(esp_tls_error_handle_t h, int *esp_tls_code, int *esp_tls_flags)
    {
        return ESP_OK;
    }
}

auto random_string(std::size_t n)
{
    static constexpr std::string_view char_set = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ123456790";
    std::string str;
    std::sample(char_set.begin(), char_set.end(), std::back_inserter(str), n,
                std::mt19937 {std::random_device{}()});
    return str;
}

using unique_mqtt_client = std::unique_ptr < std::remove_pointer_t<esp_mqtt_client_handle_t>, decltype([](esp_mqtt_client_handle_t client)
{
    esp_mqtt_client_destroy(client);
}) >;

SCENARIO("MQTT Client Operation")
{
    // Set expectations for the mocked calls.
    int mtx = 0;
    int transport_list = 0;
    int transport = 0;
    int event_group = 0;
    uint8_t mac[] = {0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55};
    esp_timer_get_time_IgnoreAndReturn(0);
    xQueueTakeMutexRecursive_IgnoreAndReturn(true);
    xQueueGiveMutexRecursive_IgnoreAndReturn(true);
    xQueueCreateMutex_ExpectAnyArgsAndReturn(
        reinterpret_cast<QueueHandle_t>(&mtx));
    xEventGroupCreate_IgnoreAndReturn(reinterpret_cast<EventGroupHandle_t>(&event_group));
    esp_transport_list_init_IgnoreAndReturn(reinterpret_cast<esp_transport_list_handle_t>(&transport_list));
    esp_transport_tcp_init_IgnoreAndReturn(reinterpret_cast<esp_transport_handle_t>(&transport));
    esp_transport_ssl_init_IgnoreAndReturn(reinterpret_cast<esp_transport_handle_t>(&transport));
    esp_transport_ws_init_IgnoreAndReturn(reinterpret_cast<esp_transport_handle_t>(&transport));
    esp_transport_ws_set_subprotocol_IgnoreAndReturn(ESP_OK);
    esp_transport_list_add_IgnoreAndReturn(ESP_OK);
    esp_transport_set_default_port_IgnoreAndReturn(ESP_OK);
    http_parser_url_init_Ignore();
    esp_event_loop_create_IgnoreAndReturn(ESP_OK);
    esp_read_mac_IgnoreAndReturn(ESP_OK);
    esp_read_mac_ReturnThruPtr_mac(mac);
    esp_transport_list_destroy_IgnoreAndReturn(ESP_OK);
    esp_transport_destroy_IgnoreAndReturn(ESP_OK);
    vEventGroupDelete_Ignore();
    vQueueDelete_Ignore();
    GIVEN("An a minimal config") {
        esp_mqtt_client_config_t config{};
        config.broker.address.uri = "mqtt://1.1.1.1";
        struct http_parser_url ret_uri = {
            .field_set = 1 | (1 << 1),
            .port = 0,
            .field_data = { { 0, 4 } /*mqtt*/, { 7, 1 } } // at least *scheme* and *host*
        };
        http_parser_parse_url_ExpectAnyArgsAndReturn(0);
        http_parser_parse_url_ReturnThruPtr_u(&ret_uri);
        xTaskCreatePinnedToCore_ExpectAnyArgsAndReturn(pdTRUE);
        SECTION("Client with minimal config") {
            auto client = unique_mqtt_client{esp_mqtt_client_init(&config)};
            REQUIRE(client != nullptr);
            SECTION("User will set a new uri") {
                struct http_parser_url ret_uri = {
                    .field_set = 1,
                    .port = 0,
                    .field_data = { { 0, 1} }
                };
                SECTION("User set a correct URI") {
                    http_parser_parse_url_StopIgnore();
                    http_parser_parse_url_ExpectAnyArgsAndReturn(0);
                    http_parser_parse_url_ReturnThruPtr_u(&ret_uri);
                    auto res = esp_mqtt_client_set_uri(client.get(), " ");
                    REQUIRE(res == ESP_OK);
                }
                SECTION("Incorrect URI from user") {
                    http_parser_parse_url_StopIgnore();
                    http_parser_parse_url_ExpectAnyArgsAndReturn(1);
                    http_parser_parse_url_ReturnThruPtr_u(&ret_uri);
                    auto res = esp_mqtt_client_set_uri(client.get(), " ");
                    REQUIRE(res == ESP_FAIL);
                }
            }
            SECTION("User set interface to use"){
                http_parser_parse_url_ExpectAnyArgsAndReturn(0);
                http_parser_parse_url_ReturnThruPtr_u(&ret_uri);
                struct ifreq if_name = {.ifr_ifrn = {"custom"}};
                config.network.if_name = &if_name;
                SECTION("Client is not started"){
                    REQUIRE(esp_mqtt_set_config(client.get(), &config)== ESP_OK);
                }
            }
            SECTION("After Start Client Is Cleanly destroyed") {
                REQUIRE(esp_mqtt_client_start(client.get()) == ESP_OK);
                // Only need to start the client, destroy is called automatically at the end of
                // scope
            }
        }
        SECTION("Client with all allocating configuration set") {
            auto host = random_string(20);
            auto path = random_string(10);
            auto username = random_string(10);
            auto client_id = random_string(10);
            auto password = random_string(10);
            auto lw_topic = random_string(10);
            auto lw_msg = random_string(10);

            config.broker = {.address = {
                    .hostname = host.data(),
                    .path = path.data()
                }
            };
            config.credentials = {
                .username = username.data(),
                .client_id = client_id.data(),
                .authentication = {
                    .password = password.data()
                }
            };
            config.session = {
                .last_will {
                    .topic = lw_topic.data(),
                    .msg = lw_msg.data()
                }
            };
            auto client = unique_mqtt_client{esp_mqtt_client_init(&config)};
            REQUIRE(client != nullptr);

        }
    }
}

