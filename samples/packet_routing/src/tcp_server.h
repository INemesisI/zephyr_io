/*
 * Packet Routing Sample - TCP Server Module
 *
 * Generic TCP server that forwards packets bidirectionally
 */

#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <weave/packet.h>

/* TCP server port */
#define TCP_SERVER_PORT 4242

/* Declare packet sink for packets to send to TCP client */
WEAVE_PACKET_SINK_DECLARE(tcp_sink);

/* Declare packet source for packets received from TCP client */
WEAVE_PACKET_SOURCE_DECLARE(tcp_rx_source);

#endif /* TCP_SERVER_H */
