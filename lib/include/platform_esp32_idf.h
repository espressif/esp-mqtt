
#ifndef _ESP_PLATFORM_H__
#define _ESP_PLATFORM_H__

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "rom/queue.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"

#define mem_assert(x) assert(x)

char *platform_create_id_string();
int platform_random(int max);
int platform_tick_get_ms();
#endif
