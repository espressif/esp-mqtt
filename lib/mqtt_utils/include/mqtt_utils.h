/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif // #ifdef __cplusplus

char *mqtt_create_string(const char *ptr, int len);
int esp_mqtt_decode_percent_encoded_string(char *uri);

#ifdef __cplusplus
}
#endif // #ifdef __cplusplus
