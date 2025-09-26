/*
 * Packet I/O Sample - Network Module
 *
 * Handles TCP network transmission
 */

#ifndef NETWORK_H
#define NETWORK_H

#include <zephyr_io/swift_io/swift_io.h>

/* Declare packet sink for external use */
SWIFT_IO_SINK_DECLARE(tcp_sink);

#endif /* NETWORK_H */