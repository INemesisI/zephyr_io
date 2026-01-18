/*
 * Packet I/O Sample - Protocol Module
 *
 * Processes packets by adding protocol headers and forwarding
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <zephyr_io/flow/flow.h>

/* Declare packet sinks and sources for external use */
FLOW_SINK_DECLARE(protocol_outbound_sink);     /* Receives from sensors */
FLOW_SINK_DECLARE(protocol_inbound_sink);      /* Receives from TCP server */
FLOW_SOURCE_DECLARE(protocol_outbound_source); /* Sends to TCP */
FLOW_SOURCE_DECLARE(protocol_inbound_source);  /* Sends processed inbound data */

#endif /* PROTOCOL_H */