/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <concepts>
#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <nlohmann/json_impl.hpp>

#include "esp_log.h"
#include "mbedtls/base64.h"
#include "mqtt_client.h"
#include "mqtt5_client.h"
#include "mqtt_conformance.hpp"

static constexpr auto TAG = "mqtt_conformance";

namespace
{

template <typename... Ts> struct overloaded : Ts... {
    using Ts::operator()...;
};
template <typename... Ts> overloaded(Ts...) -> overloaded<Ts...>;

void log_user_properties(const char *prefix,
                         mqtt5_user_property_handle_t user_property)
{
    uint8_t count = esp_mqtt5_client_get_user_property_count(user_property);

    if (count == 0) {
        return;
    }

    auto items = std::make_unique<esp_mqtt5_user_property_item_t[]>(count);

    if (esp_mqtt5_client_get_user_property(user_property, items.get(), &count) !=
            ESP_OK) {
        return;
    }

    for (uint8_t i = 0; i < count; i++) {
        ESP_LOGI(TAG, "%s key=%s val=%s", prefix, items[i].key ? items[i].key : "",
                 items[i].value ? items[i].value : "");
        free(const_cast<char *>(items[i].key));
        free(const_cast<char *>(items[i].value));
    }

    esp_mqtt5_client_delete_user_property(user_property);
}

} // namespace

void conformance_mqtt_event_handler(void *, esp_event_base_t, int32_t event_id,
                                    void *event_data)
{
    auto *event = static_cast<esp_mqtt_event_handle_t>(event_data);

    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED session_present=%d",
                 event->session_present);

        if (event->property) {
            log_user_properties("CONNACK_USER_PROPERTY",
                                event->property->user_property);
        }

        break;

    case MQTT_EVENT_DISCONNECTED:
        if (event->property) {
            ESP_LOGW(TAG, "DISCONNECT_REASON=%d", event->reason_code);
        }

        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        if (event->error_handle &&
                event->error_handle->error_type == MQTT_ERROR_TYPE_SUBSCRIBE_FAILED) {
            ESP_LOGW(TAG, "MQTT_EVENT_SUBSCRIBE_FAILED msg_id=%d", event->msg_id);
        } else {
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        }

        if (event->data_len > 0 && event->data) {
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED data_len=%d return_code=0x%02x",
                     event->data_len,
                     static_cast<unsigned>(static_cast<uint8_t>(event->data[0])));
        }

        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);

        if (event->data_len > 0 && event->data) {
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED data_len=%d reason_code=0x%02x",
                     event->data_len,
                     static_cast<unsigned>(static_cast<uint8_t>(event->data[0])));
        }

        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        if (event->topic && event->topic_len > 0) {
            ESP_LOGI(TAG, "MQTT_EVENT_DATA topic=%.*s qos=%d len=%d offset=%d total=%d",
                     event->topic_len, event->topic, event->qos, event->data_len,
                     event->current_data_offset, event->total_data_len);
        } else {
            ESP_LOGI(TAG, "MQTT_EVENT_DATA qos=%d len=%d offset=%d total=%d",
                     event->qos, event->data_len,
                     event->current_data_offset, event->total_data_len);
        }

        if (event->data && event->data_len > 0) {
            ESP_LOGI(TAG, "MQTT_EVENT_DATA_PAYLOAD %.*s", event->data_len, event->data);
        }

        if (event->property) {
            if (event->property->payload_format_indicator) {
                ESP_LOGI(TAG, "DATA_PROP payload_format_indicator=1");
            }

            if (event->property->content_type &&
                    event->property->content_type_len > 0) {
                ESP_LOGI(TAG, "DATA_PROP content_type=%.*s",
                         event->property->content_type_len,
                         event->property->content_type);
            }

            if (event->property->response_topic &&
                    event->property->response_topic_len > 0) {
                ESP_LOGI(TAG, "DATA_PROP response_topic=%.*s",
                         event->property->response_topic_len,
                         event->property->response_topic);
            }

            if (event->property->correlation_data &&
                    event->property->correlation_data_len > 0) {
                ESP_LOGI(TAG, "DATA_PROP correlation_data=%.*s",
                         event->property->correlation_data_len,
                         event->property->correlation_data);
            }

            if (event->property->subscribe_id > 0) {
                ESP_LOGI(TAG, "DATA_PROP subscribe_id=%d",
                         event->property->subscribe_id);
            }

            log_user_properties("DATA_PROP user_property",
                                event->property->user_property);
        }

        if (event->current_data_offset + event->data_len == event->total_data_len) {
            ESP_LOGI(TAG, "MQTT_EVENT_DATA_COMPLETE msg_id=%d total=%d",
                     event->msg_id, event->total_data_len);
        }

        break;

    case MQTT_EVENT_ERROR:
        if (event->error_handle) {
            if (event->error_handle->error_type ==
                    MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ESP_LOGW(TAG, "MQTT_EVENT_ERROR CONNECTION_REFUSED code=%d",
                         event->error_handle->connect_return_code);
            } else if (event->error_handle->error_type ==
                       MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "MQTT_EVENT_ERROR TCP_TRANSPORT");
            } else {
                ESP_LOGE(TAG, "MQTT_EVENT_ERROR type=%d",
                         event->error_handle->error_type);
            }
        } else {
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR (no error_handle)");
        }

        break;

    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

namespace
{

using json = nlohmann::json;
using namespace nlohmann::literals;

template <typename T>
struct field_table_entry {
    using Config = T;
    std::string_view field_name;
    void (*set)(owned_config<T> &, const json &);
};

template <typename E>
concept field_table = std::same_as<E, field_table_entry<typename E::Config>>;

template <typename P>
concept member_object_pointer = std::is_member_object_pointer_v<P>;

template <member_object_pointer auto member>
void set_scalar(auto &config, const json &value)
{
    using member_type = std::remove_reference_t<decltype((*config.data).*member)>;

    if constexpr(std::is_same_v<member_type, const char *>) {
        const std::string &stored = config.data_storage.emplace_back(value.get<std::string>());
        (*config.data).*member = stored.c_str();
    } else {
        (*config.data).*member = value.get<member_type>();
    }
}

constexpr field_table_entry<esp_mqtt5_connection_property_config_t> connection_property_fields_table[] = {
    {"session_expiry_interval", &set_scalar<&esp_mqtt5_connection_property_config_t::session_expiry_interval>},
    {"receive_maximum", &set_scalar<&esp_mqtt5_connection_property_config_t::receive_maximum>},
    {"topic_alias_maximum", &set_scalar<&esp_mqtt5_connection_property_config_t::topic_alias_maximum>},
    {"maximum_packet_size", &set_scalar<&esp_mqtt5_connection_property_config_t::maximum_packet_size>},
    {"will_delay_interval", &set_scalar<&esp_mqtt5_connection_property_config_t::will_delay_interval>},
};

constexpr field_table_entry<esp_mqtt5_publish_property_config_t> publish_property_fields_table[] = {
    {"message_expiry_interval", &set_scalar<&esp_mqtt5_publish_property_config_t::message_expiry_interval>},
    {"payload_format_indicator", &set_scalar<&esp_mqtt5_publish_property_config_t::payload_format_indicator>},
    {"topic_alias", &set_scalar<&esp_mqtt5_publish_property_config_t::topic_alias>},
    {"content_type", &set_scalar<&esp_mqtt5_publish_property_config_t::content_type>},
    {"response_topic", &set_scalar<&esp_mqtt5_publish_property_config_t::response_topic>},
};

constexpr field_table_entry<esp_mqtt5_subscribe_property_config_t> subscribe_property_fields_table[] = {
    {"subscribe_id", &set_scalar<&esp_mqtt5_subscribe_property_config_t::subscribe_id>},
    {"no_local_flag", &set_scalar<&esp_mqtt5_subscribe_property_config_t::no_local_flag>},
    {"retain_as_published_flag", &set_scalar<&esp_mqtt5_subscribe_property_config_t::retain_as_published_flag>},
    {"retain_handle", &set_scalar<&esp_mqtt5_subscribe_property_config_t::retain_handle>},
    {"is_share_subscribe", &set_scalar<&esp_mqtt5_subscribe_property_config_t::is_share_subscribe>},
    {"share_name", &set_scalar<&esp_mqtt5_subscribe_property_config_t::share_name>},
};

constexpr field_table_entry<esp_mqtt5_disconnect_property_config_t> disconnect_property_fields_table[] = {
    {"session_expiry_interval", &set_scalar<&esp_mqtt5_disconnect_property_config_t::session_expiry_interval>},
    {"disconnect_reason", &set_scalar<&esp_mqtt5_disconnect_property_config_t::disconnect_reason>},
};

[[nodiscard]] auto find_field(std::ranges::forward_range auto const &table, std::string_view name)
-> const std::ranges::range_value_t<decltype(table)> *
requires field_table<std::ranges::range_value_t<decltype(table)>>
{
    for (const auto &entry : table) {
        if (entry.field_name == name) {
            return &entry;
        }
    }

    return nullptr;
}

void build_from_table(auto &cfg, std::ranges::forward_range auto const &table, const json &node)
requires field_table<std::ranges::range_value_t<decltype(table)>>
{
    for (const auto &[field_name, value] : node.items()) {
        const auto *field = find_field(table, field_name);

        if (!field) {
            ESP_LOGW(TAG, "json config: unknown field name '%s', skipping", field_name.c_str());
            continue;
        }

        try {
            field->set(cfg, value);
        } catch (const json::exception &e) {
            ESP_LOGE(TAG, "json config: field '%s' has an unexpected JSON type (%s)", field_name.c_str(), e.what());
            throw;
        }
    }
}

void apply_if_present(const json &node, const json::json_pointer &ptr, const std::function<void(const json &)> &assign)
{
    if (!node.contains(ptr)) {
        return;
    }

    try {
        assign(node.at(ptr));
    } catch (const json::exception &e) {
        ESP_LOGE(TAG, "json config: '%s' has an unexpected JSON type (%s)", ptr.to_string().c_str(), e.what());
        throw;
    }
}

[[nodiscard]] parsed_config build_mqtt_config(const json &node)
{
    unique_mqtt_config cfg{std::make_unique<esp_mqtt_client_config_t>(), {}};
    apply_if_present(node, "/broker/address/uri"_json_pointer, [&](const json & v) {
        const std::string &uri = cfg.data_storage.emplace_back(v.get<std::string>());
        cfg.data->broker.address.uri = uri.c_str();
    });
    apply_if_present(node, "/credentials/client_id"_json_pointer, [&](const json & v) {
        const std::string &client_id = cfg.data_storage.emplace_back(v.get<std::string>());
        cfg.data->credentials.client_id = client_id.c_str();
    });
    apply_if_present(node, "/session/keepalive"_json_pointer, [&](const json & v) {
        cfg.data->session.keepalive = v.get<int>();
    });
    apply_if_present(node, "/session/disable_clean_session"_json_pointer, [&](const json & v) {
        cfg.data->session.disable_clean_session = v.get<bool>();
    });
    apply_if_present(node, "/session/protocol_ver"_json_pointer, [&](const json & v) {
        cfg.data->session.protocol_ver = v.get<esp_mqtt_protocol_ver_t>();
    });
    apply_if_present(node, "/network/disable_auto_reconnect"_json_pointer, [&](const json & v) {
        cfg.data->network.disable_auto_reconnect = v.get<bool>();
    });
    return cfg;
}

[[nodiscard]] parsed_config build_connect_property(const json &node)
{
    unique_connection_property_config cfg{std::make_unique<esp_mqtt5_connection_property_config_t>(), {}};
    build_from_table(cfg, connection_property_fields_table, node);
    return cfg;
}

[[nodiscard]] parsed_config build_publish_property(const json &node)
{
    unique_publish_property_config cfg{std::make_unique<esp_mqtt5_publish_property_config_t>(), {}};
    build_from_table(cfg, publish_property_fields_table, node);
    return cfg;
}

[[nodiscard]] parsed_config build_subscribe_property(const json &node)
{
    unique_subscribe_property_config cfg{std::make_unique<esp_mqtt5_subscribe_property_config_t>(), {}};
    build_from_table(cfg, subscribe_property_fields_table, node);
    return cfg;
}

[[nodiscard]] parsed_config build_disconnect_property(const json &node)
{
    unique_disconnect_property_config cfg{std::make_unique<esp_mqtt5_disconnect_property_config_t>(), {}};
    build_from_table(cfg, disconnect_property_fields_table, node);
    return cfg;
}

struct config_builder {
    std::string_view config_name;
    parsed_config(*build)(const json &);
};

// The blob's single top-level key names the category directly, so this
// table doubles as both the category detector and the dispatch table.
constexpr config_builder config_entries[] = {
    {"mqtt_config", &build_mqtt_config},
    {"connect_property", &build_connect_property},
    {"publish_property", &build_publish_property},
    {"subscribe_property", &build_subscribe_property},
    {"disconnect_property", &build_disconnect_property},
};

[[nodiscard]] auto decode_base64(std::string_view b64)
-> std::expected<std::vector<uint8_t>, esp_err_t>
{
    size_t out_len = 0;
    auto *base64_data = reinterpret_cast<const unsigned char *>(b64.data());
    mbedtls_base64_decode(nullptr, 0, &out_len, base64_data, b64.size());

    if (out_len == 0) {
        ESP_LOGE(TAG, "json config: base64 length probe failed");
        return std::unexpected(ESP_ERR_INVALID_ARG);
    }

    std::vector<uint8_t> buf(out_len);
    size_t written = 0;
    int rc = mbedtls_base64_decode(buf.data(), buf.size(), &written, base64_data, b64.size());

    if (rc != 0) {
        ESP_LOGE(TAG, "json config: base64 decode failed (%d)", rc);
        return std::unexpected(ESP_ERR_INVALID_ARG);
    }

    buf.resize(written);
    return buf;
}

[[nodiscard]] auto parse_json(std::vector<uint8_t> decoded) -> std::expected<json, esp_err_t>
{
    try {
        json doc = json::parse(decoded.begin(), decoded.end());

        if (!doc.is_object()) {
            ESP_LOGE(TAG, "json config: expected a JSON object");
            return std::unexpected(ESP_ERR_INVALID_ARG);
        }

        return doc;
    } catch (const json::parse_error &e) {
        ESP_LOGE(TAG, "json config: %s", e.what());
        return std::unexpected(ESP_ERR_INVALID_ARG);
    }
}

[[nodiscard]] std::expected<parsed_config, esp_err_t> build_command_config(const json &command)
{
    const auto builder = std::ranges::find_if(config_entries, [&](const config_builder & category) {
        return command.contains(category.config_name);
    });

    if (builder == std::ranges::end(config_entries)) {
        ESP_LOGE(TAG, "json config: no recognized category present");
        return std::unexpected(ESP_ERR_INVALID_ARG);
    }

    const json &node = command[builder->config_name];

    if (!node.is_object()) {
        ESP_LOGE(TAG, "json config: category '%.*s' must be a JSON object",
                 static_cast<int>(builder->config_name.size()), builder->config_name.data());
        return std::unexpected(ESP_ERR_INVALID_ARG);
    }

    try {
        return builder->build(node);
    } catch (const json::exception &) {
        return std::unexpected(ESP_ERR_INVALID_ARG);
    }
}

} // namespace

[[nodiscard]] std::expected<parsed_config, esp_err_t>
conformance_parse_json_config(std::string_view command_b64)
{
    return decode_base64(command_b64)
           .and_then(parse_json)
           .and_then(build_command_config);
}

void conformance_apply_config(command_context_t *ctx, parsed_config config)
{
    std::visit(
    overloaded{
        [&](unique_mqtt_config & client_config)
        {
            if (client_config.data->credentials.client_id) {
                ESP_LOGI(TAG, "client_id=%s", client_config.data->credentials.client_id);
            }

            esp_mqtt_set_config(ctx->mqtt_client, client_config.data.get());
        },
        [&](unique_connection_property_config & property_config)
        {
            esp_mqtt5_client_set_connect_property(ctx->mqtt_client, property_config.data.get());
        },
        [&](unique_publish_property_config & property_config)
        {
            esp_mqtt5_client_set_publish_property(ctx->mqtt_client, property_config.data.get());
        },
        [&](unique_subscribe_property_config & property_config)
        {
            // esp-mqtt5 keeps a pointer to *subscribe_property* itself (not a
            // copy) for use at the next subscribe, so share_name's backing
            // storage has to live in ctx, not in this (about to be destroyed)
            // parsed config.
            ctx->subscribe_property = *property_config.data;
            ctx->subscribe_share_name = property_config.data_storage.empty() ? std::string{} : std::move(property_config.data_storage.front());
            ctx->subscribe_property.share_name = ctx->subscribe_share_name.c_str();
            esp_mqtt5_client_set_subscribe_property(ctx->mqtt_client,
                                                    &ctx->subscribe_property);
        },
        [&](unique_disconnect_property_config & c)
        {
            esp_mqtt5_client_set_disconnect_property(ctx->mqtt_client, c.data.get());
        },
    },
    config);
}
