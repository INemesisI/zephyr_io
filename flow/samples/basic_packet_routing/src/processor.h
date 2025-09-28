/*
 * Packet I/O Sample - Processor Module
 *
 * Processes packets by adding headers and forwarding
 */

#ifndef PROCESSOR_H
#define PROCESSOR_H

#include <zephyr_io/flow/flow.h>

/* Declare packet sinks and sources for external use */
FLOW_SINK_DECLARE(processor_outbound_sink);     /* Receives from sensors */
FLOW_SINK_DECLARE(processor_inbound_sink);      /* Receives echoed packets from TCP */
FLOW_SOURCE_DECLARE(processor_outbound_source); /* Sends to TCP */
FLOW_SOURCE_DECLARE(processor_inbound_source);  /* Sends to validators */

#endif /* PROCESSOR_H */