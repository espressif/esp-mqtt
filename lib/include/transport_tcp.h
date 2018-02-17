/*
 * This file is subject to the terms and conditions defined in
 * file 'LICENSE', which is part of this source code package.
 * Tuan PM <tuanpm at live dot com>
 */
#ifndef _TRANSPORT_TCP_H_
#define _TRANSPORT_TCP_H_

#include "transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief      Create TCP transport
 *
 * @return
 *  - transport
 *  - NULL
 */
transport_handle_t transport_tcp_init();


#ifdef __cplusplus
}
#endif

#endif
