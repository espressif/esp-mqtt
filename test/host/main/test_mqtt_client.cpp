/*
 * SPDX-FileCopyrightText: 2022-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "esp_transport.h"
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <cstddef>
#include <exception>
#include <memory>
#include <net/if.h>
#include <rapidcheck.h>
#include <string>
#include <string_view>
#include <type_traits>

#include "mqtt_client.h"
#include "test_log_intercept.hpp"
#include "test_log_matchers.hpp"
extern "C" {
#include "Mockesp_event.h"
#include "Mockesp_transport.h"
#include "Mockesp_transport_ssl.h"
#include "Mockesp_transport_tcp.h"
#include "Mockesp_transport_ws.h"
#include "Mockevent_groups.h"
#include "Mockqueue.h"
#include "Mocktask.h"
#include "esp_log.h"
#if __has_include("Mockidf_additions.h")
    /* Some functions were moved from "task.h" to "idf_additions.h" */
#include "Mockidf_additions.h"
#endif
#include "Mockesp_timer.h"
    /*
     * The following functions are not directly called but the generation of them
     * from cmock is broken, so we need to define them here.
     */
    esp_err_t esp_tls_get_and_clear_last_error(esp_tls_error_handle_t h,
                                               int *esp_tls_code,
                                               int *esp_tls_flags)
    {
        return ESP_OK;
    }
}

static std::string build_uri(std::string_view scheme, std::string_view host,
                             const uint16_t *port, std::string_view path = {},
                             std::string_view query = {},
                             std::string_view user_info = {})
{
    std::string uri{scheme};
    uri += "://";

    if (!user_info.empty()) {
        uri += user_info;
        uri += "@";
    }

    uri += host;

    if (port) {
        uri += ":";
        uri += std::to_string(*port);
    }

    if (!path.empty()) {
        uri += "/";
        uri += path;
    }

    if (!query.empty()) {
        uri += "?";
        uri += query;
    }

    return uri;
}

using unique_mqtt_client =
    std::unique_ptr < std::remove_pointer_t<esp_mqtt_client_handle_t>,
    decltype([](esp_mqtt_client_handle_t client)
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
    esp_timer_get_time_IgnoreAndReturn(0);
    xQueueTakeMutexRecursive_IgnoreAndReturn(true);
    xQueueGiveMutexRecursive_IgnoreAndReturn(true);
    xQueueCreateMutex_ExpectAnyArgsAndReturn(
        reinterpret_cast<QueueHandle_t>(&mtx));
    xEventGroupCreate_IgnoreAndReturn(
        reinterpret_cast<EventGroupHandle_t>(&event_group));
    esp_transport_list_init_IgnoreAndReturn(
        reinterpret_cast<esp_transport_list_handle_t>(&transport_list));
    esp_transport_tcp_init_IgnoreAndReturn(
        reinterpret_cast<esp_transport_handle_t>(&transport));
    esp_transport_ssl_init_IgnoreAndReturn(
        reinterpret_cast<esp_transport_handle_t>(&transport));
    esp_transport_ws_init_IgnoreAndReturn(
        reinterpret_cast<esp_transport_handle_t>(&transport));
    esp_transport_ws_set_subprotocol_IgnoreAndReturn(ESP_OK);
    esp_transport_list_add_IgnoreAndReturn(ESP_OK);
    esp_transport_set_default_port_IgnoreAndReturn(ESP_OK);
    esp_event_loop_create_IgnoreAndReturn(ESP_OK);
    esp_transport_list_destroy_IgnoreAndReturn(ESP_OK);
    esp_transport_destroy_IgnoreAndReturn(ESP_OK);
    vEventGroupDelete_Ignore();
    vQueueDelete_Ignore();
    GIVEN("An a minimal config") {
        static constexpr std::array<char, 36> char_set = {
            'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
            'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '0', '1',
            '2', '3', '4', '5', '6', '7', '8', '9',
        };
        esp_mqtt_client_config_t config{};
        config.broker.address.uri = "mqtt://1.1.1.1";
        xTaskCreatePinnedToCore_ExpectAnyArgsAndReturn(pdTRUE);
        SECTION("Client with minimal config") {
            auto client = unique_mqtt_client{esp_mqtt_client_init(&config)};
            REQUIRE(client != nullptr);
            SECTION("User will set a new uri") {
                using namespace test::esp_log::matchers;
                esp_log_level_set("mqtt_client", ESP_LOG_DEBUG);
                test::esp_log::Capture log;
                SECTION("User set a correct URI") {
                    REQUIRE(esp_mqtt_client_set_uri(
                                client.get(), "mqtt://example.com/valid") == ESP_OK);
                    REQUIRE_THAT(log, HasMessageIn("mqtt_client",
                                                   "config_uri=mqtt://example.com/valid"));
                }
                SECTION("Incorrect URI from user leaves config unchanged") {
                    REQUIRE(esp_mqtt_client_set_uri(
                                client.get(),
                                "invalid string that is not a url") == ESP_FAIL);
                    REQUIRE_THAT(log, HasErrorIn("mqtt_client",
                                                 "Error parse uri"));
                    REQUIRE(esp_mqtt_client_set_uri(client.get(),
                                                    "mqtt://1.1.1.1") == ESP_OK);
                    REQUIRE_THAT(log, HasMessageIn("mqtt_client",
                                                   "config_uri=mqtt://1.1.1.1"));
                }
                SECTION("set_uri stores complete URI with all components") {
                    const char *full_uri =
                        "mqtts://user:pass@broker.local:8883/topic?opt=1";
                    REQUIRE(esp_mqtt_client_set_uri(client.get(),
                                                    full_uri) == ESP_OK);
                    REQUIRE_THAT(log, HasMessageIn("mqtt_client",
                                                   std::string{"config_uri="} + full_uri));
                }
                SECTION("set_uri value survives round-trip through set_config "
                        "without uri") {
                    static constexpr std::array schemes = {"mqtt", "mqtts",
                                                           "ws", "wss"
                                                          };
                    rc::check("set_uri then set_config(no uri) preserves URI",
                    [&] {
                        auto char_gen = rc::gen::elementOf(char_set);
                        auto scheme = *rc::gen::elementOf(schemes);
                        auto host = *rc::gen::container<std::string>(
                            *rc::gen::inRange<int>(1, 20), char_gen).as("host");
                        auto path = *rc::gen::container<std::string>(
                            *rc::gen::inRange<int>(0, 10), char_gen).as("path");
                        auto port = *rc::gen::maybe(
                            rc::gen::inRange<uint16_t>(1, 65535)).as("port");
                        std::string uri = build_uri(scheme, host,
                                                    port ? &*port : nullptr, path);

                        log.clear();
                        RC_ASSERT(esp_mqtt_client_set_uri(client.get(),
                                                          uri.c_str()) == ESP_OK);
                        RC_ASSERT(log.contains("mqtt_client",
                        std::string{"config_uri="} + uri));

                        // set_config without a uri must not revert the stored URI
                        esp_mqtt_client_config_t no_uri_cfg{};
                        no_uri_cfg.credentials.client_id = "round-trip-id";
                        RC_ASSERT(esp_mqtt_set_config(client.get(),
                                                      &no_uri_cfg) == ESP_OK);

                        log.clear();
                        RC_ASSERT(esp_mqtt_client_set_uri(client.get(),
                                                          uri.c_str()) == ESP_OK);
                        RC_ASSERT(log.contains("mqtt_client",
                                               std::string{"config_uri="} + uri));
                    });
                }
                SECTION("set_config with new uri replaces the previous set_uri "
                        "value") {
                    static constexpr std::array schemes = {"mqtt", "mqtts",
                                                           "ws", "wss"
                                                          };
                    rc::check("set_config with URI overrides previous set_uri",
                    [&] {
                        auto char_gen = rc::gen::elementOf(char_set);
                        auto scheme1 = *rc::gen::elementOf(schemes);
                        auto host1 = *rc::gen::container<std::string>(
                            *rc::gen::inRange<int>(1, 16), char_gen).as("host1");
                        auto scheme2 = *rc::gen::elementOf(schemes);
                        auto host2 = *rc::gen::container<std::string>(
                            *rc::gen::inRange<int>(1, 16), char_gen).as("host2");
                        std::string first = build_uri(scheme1, host1, nullptr);
                        std::string second = build_uri(scheme2, host2, nullptr);
                        RC_PRE(first != second);

                        RC_ASSERT(esp_mqtt_client_set_uri(client.get(),
                                                          first.c_str()) == ESP_OK);
                        log.clear();
                        esp_mqtt_client_config_t new_cfg{};
                        new_cfg.broker.address.uri = second.c_str();
                        RC_ASSERT(esp_mqtt_set_config(client.get(),
                                                      &new_cfg) == ESP_OK);
                        RC_ASSERT(log.contains("mqtt_client",
                                               std::string{"config_uri="} + second));
                        RC_ASSERT(!log.contains("mqtt_client",
                                                std::string{"config_uri="} + first));
                    });
                }
                SECTION("set_config called repeatedly is safe") {
                    REQUIRE(esp_mqtt_set_config(client.get(), &config) == ESP_OK);
                    REQUIRE(esp_mqtt_set_config(client.get(), &config) == ESP_OK);
                    REQUIRE_THAT(log, HasMessageIn("mqtt_client",
                                                   "config_uri=mqtt://1.1.1.1"));
                }
                SECTION("URI with percent-encoded user_info is accepted") {
                    REQUIRE(esp_mqtt_client_set_uri(
                                client.get(),
                                "mqtt://user:p%40ss@broker.local") == ESP_OK);
                }
                SECTION("URI with invalid percent-encoded password returns "
                        "ESP_FAIL and leaves config unchanged") {
                    REQUIRE(esp_mqtt_client_set_uri(client.get(),
                                                    "mqtt://changed.host") == ESP_OK);
                    log.clear();
                    REQUIRE(esp_mqtt_client_set_uri(
                                client.get(),
                                "mqtt://user:p%ZZ@broker.local") == ESP_FAIL);
                    // The failed call must not have modified config
                    REQUIRE_FALSE(log.contains("mqtt_client",
                                               "config_uri=mqtt://user"));
                    // Client remains usable (lock not leaked)
                    REQUIRE(esp_mqtt_client_set_uri(
                                client.get(),
                                "mqtt://recovery.broker") == ESP_OK);
                    REQUIRE_THAT(log, HasMessageIn("mqtt_client",
                                                   "config_uri=mqtt://recovery.broker"));
                }
                SECTION("URI with invalid percent-encoded username returns "
                        "ESP_FAIL and leaves config unchanged") {
                    REQUIRE(esp_mqtt_client_set_uri(client.get(),
                                                    "mqtt://changed.host") == ESP_OK);
                    log.clear();
                    REQUIRE(esp_mqtt_client_set_uri(
                                client.get(),
                                "mqtt://us%XYer:pass@broker.local") == ESP_FAIL);
                    REQUIRE_FALSE(log.contains("mqtt_client",
                                               "config_uri=mqtt://us"));
                    REQUIRE(esp_mqtt_client_set_uri(
                                client.get(),
                                "mqtt://recovery2.broker") == ESP_OK);
                    REQUIRE_THAT(log, HasMessageIn("mqtt_client",
                                                   "config_uri=mqtt://recovery2.broker"));
                }
            }
            SECTION("Any well-formed URI is accepted") {
                esp_log_level_set("mqtt_client", ESP_LOG_DEBUG);
                static constexpr std::array schemes = {"mqtt", "mqtts",
                                                       "ws", "wss"
                                                      };
                test::esp_log::Capture log;
                rc::check("esp_mqtt_client_set_uri accepts well-formed URIs", [&] {
                    auto char_gen = rc::gen::elementOf(char_set);
                    auto scheme = *rc::gen::elementOf(schemes);
                    auto host = *rc::gen::container<std::string>(
                        *rc::gen::inRange<int>(1, 33), char_gen)
                    .as("host");
                    auto path = *rc::gen::container<std::string>(
                        *rc::gen::inRange<int>(0, 17), char_gen)
                    .as("path");
                    auto query = *rc::gen::container<std::string>(
                        *rc::gen::inRange<int>(0, 17), char_gen)
                    .as("query");
                    auto port =
                    *rc::gen::maybe(rc::gen::inRange<uint16_t>(1, 65535)).as("port");
                    std::string uri =
                    build_uri(scheme, host, port ? &*port : nullptr, path, query);

                    log.clear();
                    RC_ASSERT(esp_mqtt_client_set_uri(client.get(), uri.c_str()) ==
                              ESP_OK);
                    RC_ASSERT(
                    log.contains("mqtt_client", std::string{"config_uri="} + uri));
                });
            }
            SECTION("User set interface to use") {
                struct ifreq if_name = {};
                strncpy(if_name.ifr_name, "custom", IFNAMSIZ - 1);
                if_name.ifr_name[IFNAMSIZ - 1] = '\0';
                config.network.if_name = &if_name;
                SECTION("Client is not started") {
                    REQUIRE(esp_mqtt_set_config(client.get(), &config) == ESP_OK);
                }
            }
            SECTION("After Start Client Is Cleanly destroyed") {
                esp_log_level_set("mqtt_client", ESP_LOG_DEBUG);
                test::esp_log::Capture log;
                REQUIRE(esp_mqtt_client_start(client.get()) == ESP_OK);
                using namespace test::esp_log::matchers;
                REQUIRE_THAT(log, HasMessageIn("mqtt_client", "Core selection"));
                // Only need to start the client, destroy is called automatically at the
                // end of scope
            }
        }
        SECTION("Client with all allocating configuration set") {
            xQueueCreateMutex_IgnoreAndReturn(
                reinterpret_cast<QueueHandle_t>(&mtx));
            rc::check("esp_mqtt_client_init succeeds with arbitrary config fields",
            [&] {
                auto char_gen = rc::gen::elementOf(char_set);
                auto host = *rc::gen::container<std::string>(
                    *rc::gen::inRange<int>(1, 20), char_gen).as("host");
                auto path = *rc::gen::container<std::string>(
                    *rc::gen::inRange<int>(1, 10), char_gen).as("path");
                auto username = *rc::gen::container<std::string>(
                    *rc::gen::inRange<int>(1, 10), char_gen).as("username");
                auto client_id = *rc::gen::container<std::string>(
                    *rc::gen::inRange<int>(1, 10), char_gen).as("client_id");
                auto password = *rc::gen::container<std::string>(
                    *rc::gen::inRange<int>(1, 10), char_gen).as("password");
                auto lw_topic = *rc::gen::container<std::string>(
                    *rc::gen::inRange<int>(1, 10), char_gen).as("lw_topic");
                auto lw_msg = *rc::gen::container<std::string>(
                    *rc::gen::inRange<int>(1, 10), char_gen).as("lw_msg");

                esp_mqtt_client_config_t alloc_cfg{};
                alloc_cfg.broker = {
                    .address = {.hostname = host.data(), .path = path.data()}
                };
                alloc_cfg.credentials = {
                    .username = username.data(),
                                        .client_id = client_id.data(),
                    .authentication = {.password = password.data()}
                };
                alloc_cfg.session = {
                    .last_will{.topic = lw_topic.data(), .msg = lw_msg.data()}
                };
                auto client = unique_mqtt_client{esp_mqtt_client_init(&alloc_cfg)};
                const bool client_is_valid = client != nullptr;
                RC_ASSERT(client_is_valid);
            });
        }
    }
}
