/*
 * This file is subject to the terms and conditions defined in
 * file 'LICENSE', which is part of this source code package.
 * Tuan PM <tuanpm at live dot com>
 */
#ifndef _PLATFORM_H__
#define _PLATFORM_H__

#include "sdkconfig.h"

//Support ESP32
#  ifdef ESP_PLATFORM
#    ifdef CONFIG_TARGET_PLATFORM_ESP8266
#include "platform_esp8266_idf.h"
#    else
#include "platform_esp32_idf.h"
#    endif
#  endif

#endif
