/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "include/mqtt_utils.h"

char *mqtt_create_string(const char *ptr, int len)
{
    if (len <= 0) {
        return NULL;
    }

    char *ret = calloc(1, len + 1);

    if (ret == NULL) {
        return NULL;
    }

    memcpy(ret, ptr, len);
    return ret;
}

int esp_mqtt_decode_percent_encoded_string(char *uri)
{
    if (uri == NULL) {
        return -1;
    }

    char *write_ptr = uri;
    size_t uri_len = strlen(uri);

    for (intptr_t i = 0; i < uri_len; i++, write_ptr++) {
        if (uri[i] == '%') {
            if (!(isxdigit((unsigned char) uri[i + 1]) && isxdigit((unsigned char) uri[i + 2]))) {
                // having non [0-9a-fA-F] characters after % is illegal in URI
                return -1;
            }

            char hexvalue[3] = {0, 0, 0};
            memcpy(hexvalue, uri + i + 1, 2);
            *write_ptr = (char) strtol(hexvalue, NULL, 16);
            i += 2;
        } else {
            *write_ptr = uri[i];
        }
    }

    *write_ptr = '\0';
    return (int)(write_ptr - uri);
}
