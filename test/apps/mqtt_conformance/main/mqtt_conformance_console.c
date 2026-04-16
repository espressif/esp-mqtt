/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <inttypes.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "esp_system.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "esp_log.h"
#include "mqtt_conformance.h"

static const char *TAG = "mqtt_conformance";

static command_context_t command_context;
static set_uri_args_t set_uri_args;
static subscribe_args_t subscribe_args;
static publish_args_t publish_args;

#define RETURN_ON_PARSE_ERROR(args) do { \
    int nerrors = arg_parse(argc, argv, (void **) &(args)); \
    if (nerrors != 0) { \
        arg_print_errors(stderr, (args).end, argv[0]); \
        return 1; \
    }} while(0)

static int require_client(void)
{
    if (!command_context.mqtt_client) {
        ESP_LOGE(TAG, "MQTT client not initialized, call init first");
        return 1;
    }

    return 0;
}

static int do_init(int argc, char **argv)
{
    if (command_context.mqtt_client) {
        ESP_LOGW(TAG, "MQTT client already initialized");
        return 0;
    }

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://127.0.0.1:1234",
        .network.disable_auto_reconnect = true,
#if CONFIG_MQTT_PROTOCOL_5
        .session.protocol_ver = MQTT_PROTOCOL_V_5,
#endif
    };
    command_context.mqtt_client = esp_mqtt_client_init(&mqtt_cfg);

    if (!command_context.mqtt_client) {
        ESP_LOGE(TAG, "Failed to initialize client");
        return 1;
    }

    conformance_configure_client(&command_context);
    conformance_register_event_handlers(&command_context);
    ESP_LOGI(TAG, "Mqtt client initialized");
    return 0;
}

static int do_set_uri(int argc, char **argv)
{
    RETURN_ON_PARSE_ERROR(set_uri_args);

    if (require_client() != 0) {
        return 1;
    }

    conformance_set_broker_uri(&command_context, set_uri_args.uri->sval[0]);
    ESP_LOGI(TAG, "Broker URI updated to %s", set_uri_args.uri->sval[0]);
    return 0;
}

static int do_start(int argc, char **argv)
{
    if (require_client() != 0) {
        return 1;
    }

    if (esp_mqtt_client_start(command_context.mqtt_client) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start mqtt client task");
        return 1;
    }

    ESP_LOGI(TAG, "Mqtt client started");
    return 0;
}

static int do_stop(int argc, char **argv)
{
    if (require_client() != 0) {
        return 1;
    }

    if (esp_mqtt_client_stop(command_context.mqtt_client) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop mqtt client task");
        return 1;
    }

    ESP_LOGI(TAG, "Mqtt client stopped");
    return 0;
}

static int do_destroy(int argc, char **argv)
{
    if (!command_context.mqtt_client) {
        return 0;
    }

    conformance_unregister_event_handlers(&command_context);
    esp_mqtt_client_destroy(command_context.mqtt_client);
    command_context.mqtt_client = NULL;
    ESP_LOGI(TAG, "mqtt client for tests destroyed");
    return 0;
}

static int do_subscribe(int argc, char **argv)
{
    RETURN_ON_PARSE_ERROR(subscribe_args);

    if (require_client() != 0) {
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

static int do_publish(int argc, char **argv)
{
    RETURN_ON_PARSE_ERROR(publish_args);

    if (require_client() != 0) {
        return 1;
    }

    const char *pattern = publish_args.pattern->sval[0];
    int repetitions = publish_args.pattern_repetitions->ival[0];
    size_t pattern_len = strlen(pattern);
    size_t payload_len = pattern_len * (size_t)repetitions;
    char *payload = NULL;

    if (repetitions < 0) {
        ESP_LOGE(TAG, "Invalid pattern repetitions");
        return 1;
    }

    if (payload_len > 0) {
        payload = malloc(payload_len);

        if (!payload) {
            ESP_LOGE(TAG, "Failed to allocate payload");
            return 1;
        }

        for (int i = 0; i < repetitions; i++) {
            memcpy(payload + (size_t)i * pattern_len, pattern, pattern_len);
        }
    }

    int msg_id;

    if (publish_args.enqueue->ival[0]) {
        msg_id = esp_mqtt_client_enqueue(command_context.mqtt_client, publish_args.topic->sval[0], payload, payload_len,
                                         publish_args.qos->ival[0], publish_args.retain->ival[0], true);
    } else {
        msg_id = esp_mqtt_client_publish(command_context.mqtt_client, publish_args.topic->sval[0], payload, payload_len,
                                         publish_args.qos->ival[0], publish_args.retain->ival[0]);
    }

    free(payload);

    if (msg_id < 0) {
        ESP_LOGE(TAG, "Publish failed, msg_id=%d", msg_id);
        return 1;
    }

    ESP_LOGI(TAG, "Publish requested, msg_id=%d", msg_id);
    return 0;
}

static void register_common_commands(void)
{
    const esp_console_cmd_t init = {
        .command = "init",
        .help = "Initialize mqtt client",
        .hint = NULL,
        .func = &do_init,
    };
    const esp_console_cmd_t set_uri = {
        .command = "set_uri",
        .help = "Set broker URI",
        .hint = NULL,
        .func = &do_set_uri,
        .argtable = &set_uri_args,
    };
    const esp_console_cmd_t start = {
        .command = "start",
        .help = "Start mqtt client",
        .hint = NULL,
        .func = &do_start,
    };
    const esp_console_cmd_t stop = {
        .command = "stop",
        .help = "Stop mqtt client",
        .hint = NULL,
        .func = &do_stop,
    };
    const esp_console_cmd_t destroy = {
        .command = "destroy",
        .help = "Destroy mqtt client",
        .hint = NULL,
        .func = &do_destroy,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&init));
    ESP_ERROR_CHECK(esp_console_cmd_register(&set_uri));
    ESP_ERROR_CHECK(esp_console_cmd_register(&start));
    ESP_ERROR_CHECK(esp_console_cmd_register(&stop));
    ESP_ERROR_CHECK(esp_console_cmd_register(&destroy));
}

static void register_pubsub_commands(void)
{
    set_uri_args.uri = arg_str1(NULL, NULL, "<uri>", "Broker URI");
    set_uri_args.end = arg_end(1);
    subscribe_args.topic = arg_str1(NULL, NULL, "<topic>", "Subscribe topic");
    subscribe_args.qos = arg_int1(NULL, NULL, "<qos>", "Subscribe qos");
    subscribe_args.end = arg_end(1);
    publish_args.topic = arg_str1(NULL, NULL, "<topic>", "Publish topic");
    publish_args.pattern = arg_str1(NULL, NULL, "<pattern>", "Payload pattern");
    publish_args.pattern_repetitions = arg_int1(NULL, NULL, "<pattern repetitions>", "Number of pattern repetitions");
    publish_args.qos = arg_int1(NULL, NULL, "<qos>", "Publish qos");
    publish_args.retain = arg_int1(NULL, NULL, "<retain>", "Publish retain flag");
    publish_args.enqueue = arg_int1(NULL, NULL, "<enqueue>", "0=publish,1=enqueue");
    publish_args.end = arg_end(1);
    const esp_console_cmd_t subscribe = {
        .command = "subscribe",
        .help = "Subscribe to a topic",
        .hint = NULL,
        .func = &do_subscribe,
        .argtable = &subscribe_args,
    };
    const esp_console_cmd_t publish = {
        .command = "publish",
        .help = "Publish a message",
        .hint = NULL,
        .func = &do_publish,
        .argtable = &publish_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&subscribe));
    ESP_ERROR_CHECK(esp_console_cmd_register(&publish));
}

void app_main(void)
{
    static const size_t max_line = 256;
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("wifi", ESP_LOG_ERROR);
    esp_log_level_set("mqtt_client", ESP_LOG_INFO);
    esp_log_level_set("outbox", ESP_LOG_INFO);
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "mqtt>";
    repl_config.max_cmdline_length = max_line;
    esp_console_register_help_command();
    register_pubsub_commands();
    register_common_commands();
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
