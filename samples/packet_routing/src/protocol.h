/*
 * Packet Routing Sample - Protocol Module
 *
 * Processes packets by adding protocol headers and forwarding
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <weave/packet.h>

/* Declare packet sinks and sources for external use */
WEAVE_PACKET_SINK_DECLARE(protocol_outbound_sink);     /* Receives from sensors */
WEAVE_PACKET_SINK_DECLARE(protocol_inbound_sink);      /* Receives from TCP server */
WEAVE_PACKET_SOURCE_DECLARE(protocol_outbound_source); /* Sends to TCP */
WEAVE_PACKET_SOURCE_DECLARE(protocol_inbound_source);  /* Sends processed inbound data */

#endif /* PROTOCOL_H */
