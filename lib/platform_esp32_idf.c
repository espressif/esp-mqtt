#include "platform.h"

#ifdef ESP_PLATFORM
#include "esp_system.h"
#include <sys/time.h>

char *platform_create_id_string()
{
    uint8_t mac[6];
    char *id_string = calloc(1, 32);
    mem_assert(id_string);
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    sprintf(id_string, "ESP32_%02x%02X%02X", mac[3], mac[4], mac[5]);
    return id_string;
}

int platform_random(int max)
{
    return esp_random()%max;
}

int platform_tick_get_ms()
{
    struct timeval te;
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000; // calculate milliseconds
    // printf("milliseconds: %lld\n", milliseconds);
    return milliseconds;
}
#endif
