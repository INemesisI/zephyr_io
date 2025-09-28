/*
 * Packet I/O Sample - Network Module
 *
 * Handles TCP network transmission
 */

#ifndef NETWORK_H
#define NETWORK_H

#include <zephyr_io/flow/flow.h>

/* Declare packet sink for external use */
FLOW_SINK_DECLARE(tcp_sink);

#endif /* NETWORK_H */