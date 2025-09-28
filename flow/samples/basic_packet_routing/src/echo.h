/*
 * Packet I/O Sample - Echo Module
 *
 * Echo Server - receives packets and echoes them back
 */

#ifndef ECHO_H
#define ECHO_H

#include <zephyr_io/flow/flow.h>

/* Declare packet sink and echo source for external use */
FLOW_SINK_DECLARE(echo_sink);
FLOW_SOURCE_DECLARE(echo_source);

#endif /* ECHO_H */