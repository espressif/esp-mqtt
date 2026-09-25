/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "mqtt_client_priv.h"

void test_mqtt_client_enter_reconnect_wait(esp_mqtt_client_handle_t client)
{
    static int mqtt_task;

    client->run = true;
    client->state = MQTT_STATE_WAIT_RECONNECT;
    client->task_handle = (TaskHandle_t)&mqtt_task;
}
