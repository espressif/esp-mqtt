/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#pragma once

#include <deque>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

#include "esp_err.h"
#include "mqtt5_client.h"
#include "mqtt_client.h"

struct arg_int;
struct arg_str;
struct arg_end;

struct command_context_t {
    esp_mqtt_client_handle_t mqtt_client = nullptr;
    esp_mqtt5_subscribe_property_config_t subscribe_property = {};
    // Backing storage for subscribe_property.share_name: esp-mqtt5 retains a
    // pointer to *subscribe_property* itself (not a copy) between `config`
    // and the next subscribe, so this string must outlive the config call.
    std::string subscribe_share_name;
};

struct subscribe_args_t {
    struct arg_str *topic;
    struct arg_int *qos;
    struct arg_end *end;
};

struct unsubscribe_args_t {
    struct arg_str *topic;
    struct arg_end *end;
};

struct publish_args_t {
    struct arg_str *topic;
    struct arg_str *pattern;
    struct arg_int *pattern_repetitions;
    struct arg_int *qos;
    struct arg_int *retain;
    struct arg_int *enqueue;
    struct arg_end *end;
};

struct json_config_args_t {
    struct arg_str *b64;
    struct arg_end *end;
};

/**
 * Owns a heap-allocated C config struct plus the backing storage for any of
 * its const char* fields that were populated from JSON. Strings live in a
 * deque. pointers handed out via .c_str() stay valid for the lifetime of this
 * object — no manual malloc/free bookkeeping needed. Plain aggregate: no
 * invariant to protect, so callers use the members directly.
 */
template <typename T>
struct owned_config {
    std::unique_ptr<T> data;
    std::deque<std::string> data_storage;
};

using unique_mqtt_config = owned_config<esp_mqtt_client_config_t>;
using unique_connection_property_config = owned_config<esp_mqtt5_connection_property_config_t>;
using unique_publish_property_config = owned_config<esp_mqtt5_publish_property_config_t>;
using unique_subscribe_property_config = owned_config<esp_mqtt5_subscribe_property_config_t>;
using unique_disconnect_property_config = owned_config<esp_mqtt5_disconnect_property_config_t>;

/**
 * Config parsed from a single JSON blob.
 */
using parsed_config = std::variant<unique_mqtt_config, unique_connection_property_config, unique_publish_property_config, unique_subscribe_property_config, unique_disconnect_property_config>;

/** MQTT event handler — registered directly by the console with esp_mqtt_client_register_event. */
void conformance_mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                    int32_t event_id, void *event_data);

/** JSON layer: decode base64 JSON blob → owned config. */
[[nodiscard]] std::expected<parsed_config, esp_err_t> conformance_parse_json_config(std::string_view b64);

/** Apply a parsed config to an initialised client. */
void conformance_apply_config(command_context_t *ctx, parsed_config cfg);
