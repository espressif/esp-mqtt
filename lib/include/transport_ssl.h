
#ifndef _TRANSPORT_SSL_H_
#define _TRANSPORT_SSL_H_

#include "transport.h"

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief      Create new SSL transport
 *
 * @return
 *  - transport
 *  - NULL
 */
transport_handle_t transport_ssl_init();

/**
 * @brief      Set SSL certification data (as PEM format)
 *
 * @param      t     ssl transport
 * @param[in]  data  The pem data
 * @param[in]  len   The length
 */
void transport_ssl_set_cert_data(transport_handle_t t, const char *data, int len);


#ifdef __cplusplus
}
#endif
#endif
