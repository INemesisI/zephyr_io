/*
 * Packet I/O Sample - Processor Module
 *
 * Processes packets by adding headers and forwarding
 */

#ifndef PROCESSOR_H
#define PROCESSOR_H

#include <zephyr_io/swift_io/swift_io.h>

/* Declare packet sink and source for external use */
SWIFT_IO_SINK_DECLARE(processor_sink);
SWIFT_IO_SOURCE_DECLARE(processor_source);

#endif /* PROCESSOR_H */