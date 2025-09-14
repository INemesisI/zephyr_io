/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Packet I/O public API
 *
 * The packet I/O subsystem provides zero-copy distribution of net_buf
 * packets from sources to multiple sinks using reference counting.
 */

#ifndef ZEPHYR_INCLUDE_PACKET_IO_H_
#define ZEPHYR_INCLUDE_PACKET_IO_H_

#include <zephyr/kernel.h>
#include <zephyr/net/buf.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/logging/log.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup packet_io_apis Packet I/O APIs
 * @ingroup os_services
 * @{
 */

/** @brief Packet source structure */
struct packet_source {
	/** List of connections to sinks */
	sys_dlist_t sinks;
	/** Lock protecting the connection list */
	struct k_spinlock lock;
#ifdef CONFIG_PACKET_IO_STATS
	/** Number of messages sent */
	atomic_t msg_count;
	/** Number of messages delivered to at least one sink */
	atomic_t delivered_count;
#endif
};

/** @brief Packet sink structure */
struct packet_sink {
	/** Message queue for received packets */
	struct k_msgq msgq;
	/** Buffer for message queue */
	uint8_t *buffer;
	/** Drop packets when queue is full */
	bool drop_on_full;
#ifdef CONFIG_PACKET_IO_STATS
	/** Number of packets received */
	atomic_t received_count;
	/** Number of packets dropped */
	atomic_t dropped_count;
#endif
};

/** @brief Connection between source and sink */
struct packet_connection {
	/** Pointer to the source */
	struct packet_source *source;
	/** Pointer to the sink */
	struct packet_sink *sink;
	/** Node for source's connection list */
	sys_dnode_t node;
};

/* Pointer wrapper structures for iterable sections */
struct packet_source_ptr {
	struct packet_source *ptr;
};

struct packet_sink_ptr {
	struct packet_sink *ptr;
};

/**
 * @brief Declare a packet source
 *
 * This macro declares an extern reference to a packet source defined elsewhere.
 *
 * @param _name Name of the source variable
 */
#define PACKET_SOURCE_DECLARE(_name) extern struct packet_source _name

/**
 * @brief Define a packet source
 *
 * This macro defines and initializes a packet source. The source is
 * non-static so it can be referenced from other files.
 *
 * @param _name Name of the source variable
 */
#define PACKET_SOURCE_DEFINE(_name)                                           \
	struct packet_source _name = {                                        \
		.sinks = SYS_DLIST_STATIC_INIT(&_name.sinks),                \
		.lock = {},                                                   \
		IF_ENABLED(CONFIG_PACKET_IO_STATS,                           \
			   (.msg_count = ATOMIC_INIT(0),                     \
			    .delivered_count = ATOMIC_INIT(0),))              \
	};                                                                    \
	static const STRUCT_SECTION_ITERABLE(packet_source_ptr,              \
					      _name##_ptr) = { .ptr = &_name }

/**
 * @brief Declare a packet sink
 *
 * This macro declares an extern reference to a packet sink defined elsewhere.
 *
 * @param _name Name of the sink variable
 */
#define PACKET_SINK_DECLARE(_name) extern struct packet_sink _name

/**
 * @brief Define a packet sink
 *
 * This macro defines and initializes a packet sink with an embedded
 * message queue. The sink is non-static so it can be referenced from other files.
 *
 * @param _name Name of the sink variable
 * @param _msg_count Maximum number of messages in the queue
 * @param _drop_on_full If true, drop new messages when queue is full
 */
#define PACKET_SINK_DEFINE(_name, _msg_count, _drop_on_full)                  \
	static char __aligned(4)                                              \
		_name##_buffer[(_msg_count) * sizeof(struct net_buf *)];      \
	struct packet_sink _name = {                                          \
		.msgq = Z_MSGQ_INITIALIZER(_name.msgq, _name##_buffer,        \
					    sizeof(struct net_buf *),         \
					    _msg_count),                       \
		.buffer = (uint8_t *)_name##_buffer,                          \
		.drop_on_full = _drop_on_full,                                \
		IF_ENABLED(CONFIG_PACKET_IO_STATS,                           \
			   (.received_count = ATOMIC_INIT(0),                \
			    .dropped_count = ATOMIC_INIT(0),))               \
	};                                                                    \
	static const STRUCT_SECTION_ITERABLE(packet_sink_ptr,                \
					      _name##_ptr) = { .ptr = &_name }

/**
 * @brief Connect a source to a sink
 *
 * This macro creates a static connection between a source and a sink.
 * The connection is established during system initialization.
 *
 * @param _source Source variable name
 * @param _sink Sink variable name
 */
#define PACKET_SOURCE_CONNECT(_source, _sink)                                 \
	static STRUCT_SECTION_ITERABLE(packet_connection,                     \
					__connection_##_source##_##_sink) = { \
		.source = &_source,                                           \
		.sink = &_sink,                                               \
	}

/**
 * @brief Send a packet from source to all connected sinks
 *
 * This function sends a net_buf packet to all sinks connected to the
 * source. The function does NOT consume the caller's reference.
 * Each successfully queued sink gets its own reference.
 *
 * The timeout represents the total time the send operation may take.
 * If a sink blocks and the timeout expires, remaining sinks are
 * attempted with K_NO_WAIT to ensure all sinks get a chance.
 *
 * @param src Pointer to the packet source
 * @param buf Pointer to the net_buf to send (reference is NOT consumed)
 * @param timeout Maximum time to wait for all sinks (K_NO_WAIT, K_FOREVER, or timeout)
 *
 * @return Number of sinks that successfully received the packet
 */
int packet_source_send(struct packet_source *src, struct net_buf *buf, k_timeout_t timeout);

/**
 * @brief Send a packet from source to all connected sinks (consuming reference)
 *
 * This function sends a net_buf packet to all sinks connected to the
 * source. The function CONSUMES the caller's reference to the buffer.
 * Each successfully queued sink gets its own reference.
 *
 * This is more efficient than packet_source_send() when the caller
 * doesn't need the buffer after sending, as it avoids an extra
 * ref/unref cycle.
 *
 * The timeout represents the total time the send operation may take.
 * If a sink blocks and the timeout expires, remaining sinks are
 * attempted with K_NO_WAIT to ensure all sinks get a chance.
 *
 * @param src Pointer to the packet source
 * @param buf Pointer to the net_buf to send (reference IS consumed)
 * @param timeout Maximum time to wait for all sinks (K_NO_WAIT, K_FOREVER, or timeout)
 *
 * @return Number of sinks that successfully received the packet
 */
int packet_source_send_consume(struct packet_source *src, struct net_buf *buf, k_timeout_t timeout);

#ifdef CONFIG_PACKET_IO_RUNTIME_OBSERVERS
/**
 * @brief Add a sink to a source at runtime
 *
 * @param src Pointer to the packet source
 * @param sink Pointer to the packet sink
 *
 * @return 0 on success, negative errno on error
 */
int packet_source_add_sink(struct packet_source *src,
			    struct packet_sink *sink);

/**
 * @brief Remove a sink from a source at runtime
 *
 * @param src Pointer to the packet source
 * @param sink Pointer to the packet sink
 *
 * @return 0 on success, negative errno on error
 */
int packet_source_remove_sink(struct packet_source *src,
			       struct packet_sink *sink);
#endif /* CONFIG_PACKET_IO_RUNTIME_OBSERVERS */

#ifdef CONFIG_PACKET_IO_STATS
/**
 * @brief Get source statistics
 *
 * @param src Pointer to the packet source
 * @param msg_count Output: number of messages sent (can be NULL)
 * @param delivered_count Output: number of successful deliveries (can be NULL)
 */
void packet_source_get_stats(struct packet_source *src,
			      uint32_t *msg_count,
			      uint32_t *delivered_count);

/**
 * @brief Get sink statistics
 *
 * @param sink Pointer to the packet sink
 * @param received_count Output: number of packets received (can be NULL)
 * @param dropped_count Output: number of packets dropped (can be NULL)
 */
void packet_sink_get_stats(struct packet_sink *sink,
			   uint32_t *received_count,
			   uint32_t *dropped_count);

/**
 * @brief Reset source statistics
 *
 * @param src Pointer to the packet source
 */
void packet_source_reset_stats(struct packet_source *src);

/**
 * @brief Reset sink statistics
 *
 * @param sink Pointer to the packet sink
 */
void packet_sink_reset_stats(struct packet_sink *sink);
#endif /* CONFIG_PACKET_IO_STATS */

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_PACKET_IO_H_ */