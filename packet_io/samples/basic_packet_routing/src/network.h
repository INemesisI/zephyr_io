/*
 * Packet I/O Sample - Network Module
 *
 * Handles TCP network transmission
 */

#ifndef NETWORK_H
#define NETWORK_H

#include <zephyr/packet_io/packet_io.h>

/* Declare packet sink for external use */
PACKET_SINK_DECLARE(tcp_sink);

#endif /* NETWORK_H */