/*
 * Packet I/O Sample - Processor Module
 *
 * Processes packets by adding headers and forwarding
 */

#ifndef PROCESSOR_H
#define PROCESSOR_H

#include <zephyr_io/flow/flow.h>

/* Declare packet sink and source for external use */
FLOW_SINK_DECLARE(processor_sink);
FLOW_SOURCE_DECLARE(processor_source);

#endif /* PROCESSOR_H */