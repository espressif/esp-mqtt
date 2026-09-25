/*
 * SPDX-FileCopyrightText: 2015-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _MQTT_SUPPORTED_FEATURES_H_
#define _MQTT_SUPPORTED_FEATURES_H_

#if __has_include("esp_idf_version.h")
#include "esp_idf_version.h"
#endif

/**
 * @brief This header defines supported features of IDF which mqtt module
 *        could use depending on specific version of ESP-IDF.
 *        In case "esp_idf_version.h" were not found, all additional
 *        features would be disabled
 */

#ifdef ESP_IDF_VERSION

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
// Features supported in 5.5.0
#define MQTT_SUPPORTED_FEATURE_CIPHERSUITES_LIST
#endif

#endif /* ESP_IDF_VERSION */
#endif // _MQTT_SUPPORTED_FEATURES_H_
