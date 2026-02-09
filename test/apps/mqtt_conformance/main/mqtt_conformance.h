/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#pragma once

#include "mqtt_client.h"

struct arg_int;
struct arg_str;
struct arg_end;

typedef struct {
    esp_mqtt_client_handle_t mqtt_client;
} command_context_t;

typedef struct {
    struct arg_str *uri;
    struct arg_end *end;
} set_uri_args_t;

typedef struct {
    struct arg_str *topic;
    struct arg_int *qos;
    struct arg_end *end;
} subscribe_args_t;

typedef struct {
    struct arg_str *topic;
    struct arg_str *pattern;
    struct arg_int *pattern_repetitions;
    struct arg_int *qos;
    struct arg_int *retain;
    struct arg_int *enqueue;
    struct arg_end *end;
} publish_args_t;

void conformance_register_event_handlers(command_context_t *ctx);
void conformance_unregister_event_handlers(command_context_t *ctx);
void conformance_configure_client(command_context_t *ctx);
void conformance_set_broker_uri(command_context_t *ctx, const char *uri);
