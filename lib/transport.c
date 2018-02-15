#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "transport.h"

// static const char *TAG = "TRANSPORT";
/**
 * Transport layer structure, which will provide functions, basic properties for transport types
 */
struct transport_item_t {
    int port;
    int socket;                                                                             /*!< Socket to use in this transport */
    char *tag;                                                                              /*!< Tag name */
    void *context;                                                                          /*!< Context data */
    void *data;                                                                             /*!< Additional transport data */
    connect_func _connect;  /*!< Connect function of this transport */
    io_func _read;          /*!< Read */
    io_func _write;         /*!< Write */
    trans_func _close;                                                /*!< Close */
    poll_func _poll_read;                            /*!< Poll and read */
    poll_func _poll_write;                           /*!< Poll and write */
    trans_func _destroy;                                              /*!< Destroy and free transport */
    STAILQ_ENTRY(transport_item_t) next;
};


/**
 * This list will hold all transport available
 */
STAILQ_HEAD(transport_list_t, transport_item_t);


transport_list_handle_t transport_list_init()
{
    transport_list_handle_t head = calloc(1, sizeof(struct transport_list_t));
    assert(head);
    STAILQ_INIT(head);
    return head;
}

esp_err_t transport_list_add(transport_list_handle_t head, transport_handle_t t, const char *scheme)
{
    if (head == NULL || t == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    t->tag = calloc(1, strlen(scheme) + 1);
    assert(t->tag);
    strcpy(t->tag, scheme);
    STAILQ_INSERT_TAIL(head, t, next);
    return ESP_OK;
}

transport_handle_t transport_list_get_transport(transport_list_handle_t head, const char *tag)
{
    if (!head) {
        return NULL;
    }
    if (tag == NULL) {
        return STAILQ_FIRST(head);
    }
    transport_handle_t item;
    STAILQ_FOREACH(item, head, next) {
        if (strcasecmp(item->tag, tag) == 0) {
            return item;
        }
    }
    return NULL;
}


esp_err_t transport_list_destroy(transport_list_handle_t head)
{
    transport_list_clean(head);
    free(head);
    return ESP_OK;
}

esp_err_t transport_list_clean(transport_list_handle_t head)
{
    transport_handle_t item = STAILQ_FIRST(head);
    transport_handle_t tmp;
    while (item != NULL) {
        tmp = STAILQ_NEXT(item, next);
        if (item->_destroy) {
            item->_destroy(item);
        }
        transport_destroy(item);
        item = tmp;
    }
    STAILQ_INIT(head);
    return ESP_OK;
}

transport_handle_t transport_init()
{
    transport_handle_t t = calloc(1, sizeof(struct transport_item_t));
    assert(t);
    return t;
}

int transport_destroy(transport_handle_t t)
{
    if (t->tag) {
        free(t->tag);
    }
    free(t);
    return 0;
}

int transport_connect(transport_handle_t t, const char *host, int port, int timeout_ms)
{
    int ret = -1;
    if (t->_connect) {
        return t->_connect(t, host, port, timeout_ms);
    }
    return ret;
}

int transport_read(transport_handle_t t, char *buffer, int len, int timeout_ms)
{
    if (t->_read) {
        return t->_read(t, buffer, len, timeout_ms);
    }
    return -1;
}

int transport_write(transport_handle_t t, char *buffer, int len, int timeout_ms)
{
    if (t->_write) {
        return t->_write(t, buffer, len, timeout_ms);
    }
    return -1;
}

int transport_poll_read(transport_handle_t t, int timeout_ms)
{
    if (t->_poll_read) {
        return t->_poll_read(t, timeout_ms);
    }
    return -1;
}

int transport_poll_write(transport_handle_t t, int timeout_ms)
{
    if (t->_poll_write) {
        return t->_poll_write(t, timeout_ms);
    }
    return -1;
}

int transport_close(transport_handle_t t)
{
    if (t->_close) {
        return t->_close(t);
    }
    return 0;
}

void *transport_get_data(transport_handle_t t)
{
    return t->data;
}

esp_err_t transport_set_data(transport_handle_t t, void *data)
{
    t->data = data;
    return ESP_OK;
}

esp_err_t transport_set_func(transport_handle_t t,
                             connect_func _connect,
                             io_func _read,
                             io_func _write,
                             trans_func _close,
                             poll_func _poll_read,
                             poll_func _poll_write,
                             trans_func _destroy)
{
    t->_connect = _connect;
    t->_read = _read;
    t->_write = _write;
    t->_close = _close;
    t->_poll_read = _poll_read;
    t->_poll_write = _poll_write;
    t->_destroy = _destroy;
    return ESP_OK;
}
int transport_get_default_port(transport_handle_t t)
{
    return t->port;
}
esp_err_t transport_set_default_port(transport_handle_t t, int port)
{
    t->port = port;
    return ESP_OK;
}

