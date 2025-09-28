/*
 * TCP Server Module Header
 *
 * Provides TCP server functionality for the packet router sample.
 * Handles client connections and packet transmission/reception.
 */

#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/net/buf.h>
#include <zephyr_io/flow/flow.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief TCP server configuration
 */
#define TCP_SERVER_PORT  8080
#define RECV_BUFFER_SIZE 1024

/**
 * @brief TCP server packet I/O interfaces
 *
 * These are the packet sources and sinks provided by the TCP server
 * for connecting to the router system.
 */
FLOW_SOURCE_DECLARE(tcp_server_source); /* TCP receive - packets from network */
FLOW_SINK_DECLARE(tcp_server_sink);     /* TCP transmit - packets to network */

#ifdef __cplusplus
}
#endif

#endif /* TCP_SERVER_H */