/*
 * This file is subject to the terms and conditions defined in
 * file 'LICENSE', which is part of this source code package.
 * Tuan PM <tuanpm at live dot com>
 */
#ifndef _MQTT_OUTOBX_H_
#define _MQTT_OUTOBX_H_
#include <stdint.h>
#include "platform.h"

#ifdef  __cplusplus
extern "C" {
#endif

typedef struct outbox_item {
    char *buffer;
    uint32_t len;
    uint16_t msg_id;
    int msg_type;
    uint32_t tick;
    int retry_count;
    bool pending;
    STAILQ_ENTRY(outbox_item) next;
} outbox_item_t;

STAILQ_HEAD(outbox_list_t, outbox_item);

typedef struct outbox_list_t * outbox_handle_t;
typedef outbox_item_t *outbox_item_handle_t;

outbox_handle_t outbox_init();
outbox_item_handle_t outbox_enqueue(outbox_handle_t outbox, uint8_t *data, uint32_t len, uint16_t msg_id, int msg_type, uint32_t tick);
outbox_item_handle_t outbox_dequeue(outbox_handle_t outbox);
outbox_item_handle_t outbox_get(outbox_handle_t outbox, uint16_t msg_id);
esp_err_t outbox_delete(outbox_handle_t outbox, uint16_t msg_id, int msg_type);
esp_err_t outbox_delete_msgid(outbox_handle_t outbox, uint16_t msg_id);
esp_err_t outbox_delete_msgtype(outbox_handle_t outbox, int msg_type);
esp_err_t outbox_delete_expired(outbox_handle_t outbox, uint32_t current_tick, uint32_t timeout);

esp_err_t outbox_set_pending(outbox_handle_t outbox, uint16_t msg_id);
uint32_t outbox_get_size(outbox_handle_t outbox);
esp_err_t outbox_cleanup(outbox_handle_t outbox, uint32_t max_size);
void outbox_destroy(outbox_handle_t outbox);

#ifdef  __cplusplus
}
#endif
#endif
