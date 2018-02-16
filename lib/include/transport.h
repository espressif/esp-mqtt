#ifndef _TRANSPORT_H_
#define _TRANSPORT_H_

#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef struct transport_list_t* transport_list_handle_t;
typedef struct transport_item_t* transport_handle_t;

typedef int (*connect_func)(transport_handle_t t, const char *host, int port, int timeout_ms);
typedef int (*io_func)(transport_handle_t t, char *buffer, int len, int timeout_ms);
typedef int (*trans_func)(transport_handle_t t);
typedef int (*poll_func)(transport_handle_t t, int timeout_ms);

transport_list_handle_t transport_list_init();
esp_err_t transport_list_destroy(transport_list_handle_t head);
esp_err_t transport_list_add(transport_list_handle_t head, transport_handle_t t, const char *scheme);
esp_err_t transport_list_clean(transport_list_handle_t head);
transport_handle_t transport_list_get_transport(transport_list_handle_t head, const char *tag);

transport_handle_t transport_init();
int transport_destroy(transport_handle_t t);
int transport_get_default_port(transport_handle_t t);
esp_err_t transport_set_default_port(transport_handle_t t, int port);

/**
 * @brief      Transport connection function, to make a connection to server
 *
 * @param      t           Transport to use
 * @param[in]  host        Hostname
 * @param[in]  port        Port
 * @param[in]  timeout_ms  The timeout milliseconds
 *
 * @return
 * - socket for will use by this transport
 * - (-1) if there are any errors
 */
int transport_connect(transport_handle_t t, const char *host, int port, int timeout_ms);

/**
 * @brief      Transport read function
 *
 * @param      t           Transport to use
 * @param      buffer      The buffer
 * @param[in]  len         The length
 * @param[in]  timeout_ms  The timeout milliseconds
 *
 * @return
 *  - Number of bytes was read
 *  - (-1) if there are any errors
 */
int transport_read(transport_handle_t t, char *buffer, int len, int timeout_ms);
int transport_poll_read(transport_handle_t t, int timeout_ms);

/**
 * @brief      Transport write function
 *
 * @param      t           transport
 * @param      buffer      The buffer
 * @param[in]  len         The length
 * @param[in]  timeout_ms  The timeout milliseconds
 *
 * @return
*  - Number of bytes was written
 *  - (-1) if there are any errors
 */
int transport_write(transport_handle_t t, char *buffer, int len, int timeout_ms);
int transport_poll_write(transport_handle_t t, int timeout_ms);

/**
 * @brief      Transport close
 *
 * @param      t     transport
 *
 * @return
 * - 0 if ok
 * - (-1) if there are any errors
 */
int transport_close(transport_handle_t t);
void *transport_get_data(transport_handle_t t);
esp_err_t transport_set_data(transport_handle_t t, void *data);
esp_err_t transport_set_func(transport_handle_t t,
                             connect_func _connect,
                             io_func _read,
                             io_func _write,
                             trans_func _close,
                             poll_func _poll_read,
                             poll_func _poll_write,
                             trans_func _destroy);
#ifdef __cplusplus
}
#endif
#endif
