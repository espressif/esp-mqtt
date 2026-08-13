/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "platform.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "soc/soc_caps.h"
#include "esp_random.h"
#if CONFIG_IDF_TARGET_LINUX
#include <sys/time.h>
#else
#include "esp_timer.h"
#endif
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

static const char *TAG = "platform";

#define MAX_ID_STRING (32)

#if defined SOC_WIFI_SUPPORTED
#define MAC_TYPE ESP_MAC_WIFI_STA
#elif defined SOC_EMAC_SUPPORTED
#define MAC_TYPE ESP_MAC_ETH
#elif defined SOC_IEEE802154_SUPPORTED
#define MAC_TYPE ESP_MAC_IEEE802154
#endif

char *platform_create_id_string(void)
{
    char *id_string = calloc(1, MAX_ID_STRING);
    ESP_MEM_CHECK(TAG, id_string, return NULL);
#ifndef MAC_TYPE
    ESP_LOGW(TAG, "Soc doesn't provide MAC, client could be disconnected in case of device with same name in the broker.");
    sprintf(id_string, "esp_mqtt_client_id");
#else
    uint8_t mac[6];
    esp_read_mac(mac, MAC_TYPE);
    sprintf(id_string, "ESP32_%02x%02X%02X", mac[3], mac[4], mac[5]);
#endif
    return id_string;
}

int platform_random(int max)
{
    return esp_random() % max;
}

uint64_t platform_tick_get_ms(void)
{
#if CONFIG_IDF_TARGET_LINUX
    /* IDF linux does not always provide a linked esp_timer implementation. */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000);
#else
    return esp_timer_get_time() / (int64_t)1000;
#endif
}
