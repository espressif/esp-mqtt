/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <cinttypes>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <variant>

#include "esp_system.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "esp_log.h"
#include "mqtt_conformance.hpp"

namespace
{

constexpr auto TAG = "mqtt_conformance";

command_context_t command_context;
subscribe_args_t subscribe_args;
unsubscribe_args_t unsubscribe_args;
publish_args_t publish_args;
json_config_args_t init_args;
json_config_args_t config_args;

#define RETURN_ON_PARSE_ERROR(args) do { \
    int nerrors = arg_parse(argc, argv, (void **) &(args)); \
    if (nerrors != 0) { \
        arg_print_errors(stderr, (args).end, argv[0]); \
        return 1; \
    }} while(0)

[[nodiscard]] bool client_available()
{
    if (!command_context.mqtt_client) {
        ESP_LOGE(TAG, "MQTT client not initialized, call init first");
        return false;
    }

    return true;
}

int do_init(int argc, char **argv)
{
    RETURN_ON_PARSE_ERROR(init_args);

    if (command_context.mqtt_client) {
        ESP_LOGW(TAG, "MQTT client already initialized");
        return 0;
    }

    auto mqtt_config = []() -> unique_mqtt_config {
        auto parsed_config = conformance_parse_json_config(init_args.b64->sval[0]);

        if (!parsed_config) {
            ESP_LOGE(TAG, "Invalid JSON config");
            return {};
        }

        auto *mqtt_config = std::get_if<unique_mqtt_config>(&*parsed_config);

        if (!mqtt_config)
        {
            return {};
        }

        return std::move(*mqtt_config);
    }();

    if (!mqtt_config.data) {
        ESP_LOGE(TAG, "init requires mqtt client config");
        return 1;
    }

    command_context.mqtt_client = esp_mqtt_client_init(mqtt_config.data.get());

    if (!command_context.mqtt_client) {
        ESP_LOGE(TAG, "Failed to initialize client");
        return 1;
    }

    esp_mqtt_client_register_event(command_context.mqtt_client,
                                   static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID),
                                   conformance_mqtt_event_handler, nullptr);
    ESP_LOGI(TAG, "Mqtt client initialized");
    return 0;
}

int do_config(int argc, char **argv)
{
    RETURN_ON_PARSE_ERROR(config_args);

    if (!client_available()) {
        return 1;
    }

    auto cfg = conformance_parse_json_config(config_args.b64->sval[0]);

    if (!cfg) {
        ESP_LOGE(TAG, "Failed to parse json config");
        return 1;
    }

    conformance_apply_config(&command_context, std::move(*cfg));
    return 0;
}

int do_start(int argc, char **argv)
{
    if (!client_available()) {
        return 1;
    }

    if (esp_mqtt_client_start(command_context.mqtt_client) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start mqtt client task");
        return 1;
    }

    ESP_LOGI(TAG, "Mqtt client started");
    return 0;
}

int do_stop(int argc, char **argv)
{
    if (!client_available()) {
        return 1;
    }

    if (esp_mqtt_client_stop(command_context.mqtt_client) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop mqtt client task");
        return 1;
    }

    ESP_LOGI(TAG, "Mqtt client stopped");
    return 0;
}

int do_disconnect(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (!client_available()) {
        return 1;
    }

    if (esp_mqtt_client_disconnect(command_context.mqtt_client) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to request disconnection");
        return 1;
    }

    ESP_LOGI(TAG, "Mqtt client disconnected");
    return 0;
}

int do_reconnect(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (!client_available()) {
        return 1;
    }

    if (esp_mqtt_client_reconnect(command_context.mqtt_client) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to request reconnection");
        return 1;
    }

    ESP_LOGI(TAG, "Mqtt client will reconnect");
    return 0;
}

int do_destroy(int argc, char **argv)
{
    if (!command_context.mqtt_client) {
        return 0;
    }

    esp_mqtt_client_unregister_event(command_context.mqtt_client,
                                     static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID),
                                     conformance_mqtt_event_handler);
    esp_mqtt_client_destroy(command_context.mqtt_client);
    command_context.mqtt_client = nullptr;
    command_context.subscribe_property = {};
    command_context.subscribe_share_name.clear();
    ESP_LOGI(TAG, "mqtt client for tests destroyed");
    return 0;
}

int do_subscribe(int argc, char **argv)
{
    RETURN_ON_PARSE_ERROR(subscribe_args);

    if (!client_available()) {
        return 1;
    }

    int msg_id = esp_mqtt_client_subscribe(command_context.mqtt_client, subscribe_args.topic->sval[0],
                                           subscribe_args.qos->ival[0]);

    if (msg_id < 0) {
        ESP_LOGE(TAG, "Subscribe failed, msg_id=%d", msg_id);
        return 1;
    }

    ESP_LOGI(TAG, "Subscribe requested, msg_id=%d", msg_id);
    return 0;
}

int do_unsubscribe(int argc, char **argv)
{
    RETURN_ON_PARSE_ERROR(unsubscribe_args);

    if (!client_available()) {
        return 1;
    }

    int msg_id = esp_mqtt_client_unsubscribe(command_context.mqtt_client, unsubscribe_args.topic->sval[0]);

    if (msg_id < 0) {
        ESP_LOGE(TAG, "Unsubscribe failed, msg_id=%d", msg_id);
        return 1;
    }

    ESP_LOGI(TAG, "Unsubscribe requested, msg_id=%d", msg_id);
    return 0;
}

int do_publish(int argc, char **argv)
{
    RETURN_ON_PARSE_ERROR(publish_args);

    if (!client_available()) {
        return 1;
    }

    const char *pattern = publish_args.pattern->sval[0];
    int repetitions = publish_args.pattern_repetitions->ival[0];

    if (repetitions < 0) {
        ESP_LOGE(TAG, "Invalid pattern repetitions");
        return 1;
    }

    size_t pattern_len = std::strlen(pattern);
    size_t payload_len = pattern_len * static_cast<size_t>(repetitions);
    std::unique_ptr<char[]> payload;

    if (payload_len > 0) {
        payload = std::make_unique<char[]>(payload_len);

        for (int i = 0; i < repetitions; i++) {
            std::memcpy(payload.get() + static_cast<size_t>(i) * pattern_len, pattern, pattern_len);
        }
    }

    int msg_id;

    if (publish_args.enqueue->ival[0]) {
        msg_id = esp_mqtt_client_enqueue(command_context.mqtt_client, publish_args.topic->sval[0],
                                         payload.get(), payload_len,
                                         publish_args.qos->ival[0], publish_args.retain->ival[0], true);
    } else {
        msg_id = esp_mqtt_client_publish(command_context.mqtt_client, publish_args.topic->sval[0],
                                         payload.get(), payload_len,
                                         publish_args.qos->ival[0], publish_args.retain->ival[0]);
    }

    if (msg_id < 0) {
        ESP_LOGE(TAG, "Publish failed, msg_id=%d", msg_id);
        return 1;
    }

    ESP_LOGI(TAG, "Publish requested, msg_id=%d", msg_id);
    return 0;
}

void register_commands()
{
    init_args.b64 = arg_str1(nullptr, nullptr, "<base64_json>", "JSON config blob (base64-encoded)");
    init_args.end = arg_end(1);
    const esp_console_cmd_t init = {
        .command = "init",
        .help = "Initialize mqtt client with base64-encoded JSON config",
        .hint = nullptr,
        .func = &do_init,
        .argtable = &init_args,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    config_args.b64 = arg_str1(nullptr, nullptr, "<base64_json>", "JSON config blob (base64-encoded)");
    config_args.end = arg_end(1);
    const esp_console_cmd_t config_cmd = {
        .command = "config",
        .help = "Apply base64-encoded JSON config to initialized client",
        .hint = nullptr,
        .func = &do_config,
        .argtable = &config_args,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    const esp_console_cmd_t start = {
        .command = "start",
        .help = "Start mqtt client",
        .hint = nullptr,
        .func = &do_start,
        .argtable = nullptr,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    const esp_console_cmd_t stop = {
        .command = "stop",
        .help = "Stop mqtt client",
        .hint = nullptr,
        .func = &do_stop,
        .argtable = nullptr,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    const esp_console_cmd_t disconnect = {
        .command = "disconnect",
        .help = "Disconnect mqtt client",
        .hint = nullptr,
        .func = &do_disconnect,
        .argtable = nullptr,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    const esp_console_cmd_t reconnect = {
        .command = "reconnect",
        .help = "Reconnect mqtt client",
        .hint = nullptr,
        .func = &do_reconnect,
        .argtable = nullptr,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    const esp_console_cmd_t destroy = {
        .command = "destroy",
        .help = "Destroy mqtt client",
        .hint = nullptr,
        .func = &do_destroy,
        .argtable = nullptr,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    subscribe_args.topic = arg_str1(nullptr, nullptr, "<topic>", "Subscribe topic");
    subscribe_args.qos = arg_int1(nullptr, nullptr, "<qos>", "Subscribe qos");
    subscribe_args.end = arg_end(1);
    unsubscribe_args.topic = arg_str1(nullptr, nullptr, "<topic>", "Unsubscribe topic");
    unsubscribe_args.end = arg_end(1);
    publish_args.topic = arg_str1(nullptr, nullptr, "<topic>", "Publish topic");
    publish_args.pattern = arg_str1(nullptr, nullptr, "<pattern>", "Payload pattern");
    publish_args.pattern_repetitions = arg_int1(nullptr, nullptr, "<pattern repetitions>", "Number of pattern repetitions");
    publish_args.qos = arg_int1(nullptr, nullptr, "<qos>", "Publish qos");
    publish_args.retain = arg_int1(nullptr, nullptr, "<retain>", "Publish retain flag");
    publish_args.enqueue = arg_int1(nullptr, nullptr, "<enqueue>", "0=publish,1=enqueue");
    publish_args.end = arg_end(1);
    const esp_console_cmd_t subscribe = {
        .command = "subscribe",
        .help = "Subscribe to a topic",
        .hint = nullptr,
        .func = &do_subscribe,
        .argtable = &subscribe_args,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    const esp_console_cmd_t unsubscribe = {
        .command = "unsubscribe",
        .help = "Unsubscribe from a topic",
        .hint = nullptr,
        .func = &do_unsubscribe,
        .argtable = &unsubscribe_args,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    const esp_console_cmd_t publish = {
        .command = "publish",
        .help = "Publish a message",
        .hint = nullptr,
        .func = &do_publish,
        .argtable = &publish_args,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&init));
    ESP_ERROR_CHECK(esp_console_cmd_register(&config_cmd));
    ESP_ERROR_CHECK(esp_console_cmd_register(&start));
    ESP_ERROR_CHECK(esp_console_cmd_register(&stop));
    ESP_ERROR_CHECK(esp_console_cmd_register(&disconnect));
    ESP_ERROR_CHECK(esp_console_cmd_register(&reconnect));
    ESP_ERROR_CHECK(esp_console_cmd_register(&destroy));
    ESP_ERROR_CHECK(esp_console_cmd_register(&subscribe));
    ESP_ERROR_CHECK(esp_console_cmd_register(&unsubscribe));
    ESP_ERROR_CHECK(esp_console_cmd_register(&publish));
}

} // namespace

extern "C" void app_main(void)
{
    constexpr size_t max_line = 512;
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());
    register_commands();
    esp_console_register_help_command();
    esp_console_repl_t *repl = nullptr;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "mqtt>";
    repl_config.max_cmdline_length = max_line;
    repl_config.task_stack_size = 12288;
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
