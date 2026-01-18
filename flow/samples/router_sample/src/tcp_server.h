/*
 * Flow Router Sample - TCP Server Module
 *
 * Generic TCP server that forwards packets bidirectionally
 */

#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <zephyr_io/flow/flow.h>

/* TCP server port */
#define TCP_SERVER_PORT 4242

/* Declare flow sink for packets to send to TCP client */
FLOW_SINK_DECLARE(tcp_sink);

/* Declare flow source for packets received from TCP client */
FLOW_SOURCE_DECLARE(tcp_rx_source);

#endif /* TCP_SERVER_H */