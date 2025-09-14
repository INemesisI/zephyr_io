/*
 * Packet I/O Sample - Processor Module
 *
 * Processes packets by adding headers and forwarding
 */

#ifndef PROCESSOR_H
#define PROCESSOR_H

#include <zephyr/packet_io/packet_io.h>

/* Declare packet sink and source for external use */
PACKET_SINK_DECLARE(processor_sink);
PACKET_SOURCE_DECLARE(processor_source);

#endif /* PROCESSOR_H */