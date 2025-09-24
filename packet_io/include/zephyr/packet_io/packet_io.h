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

/* ============================ Type Definitions ============================ */

/* Forward declarations */
struct packet_sink;
struct packet_event_queue;

/**
 * @brief Packet handler callback signature
 *
 * @note The handler MUST NOT call net_buf_unref() on the buffer.
 *       The framework automatically manages buffer references.
 *
 * @param sink The sink that received the packet
 * @param buf The packet buffer (do not unref)
 */
typedef void (*packet_handler_t)(struct packet_sink *sink, struct net_buf *buf);

/** @brief Packet source structure */
struct packet_source {
#ifdef CONFIG_PACKET_IO_NAMES
	/** Name of the source */
	const char *name;
#endif
	/** List of connections to sinks */
	sys_dlist_t sinks;
	/** Lock protecting the connection list */
	struct k_spinlock lock;
#ifdef CONFIG_PACKET_IO_STATS
	/** Number of send operations attempted */
	atomic_t send_count;
	/** Total number of successful queue operations across all sinks */
	atomic_t queued_total;
#endif
};

/** @brief Packet event for queued delivery */
struct packet_event {
	/** Sink that received this packet */
	struct packet_sink *sink;
	/** The packet buffer */
	struct net_buf *buf;
};

/** @brief Packet event queue for managing message queues */
struct packet_event_queue {
	/** Message queue for packet events */
	struct k_msgq *msgq;
#ifdef CONFIG_PACKET_IO_NAMES
	/** Name of the queue */
	const char *name;
#endif
#ifdef CONFIG_PACKET_IO_STATS
	/** Number of events processed */
	atomic_t processed_count;
#endif
};

/** @brief Packet sink structure */
struct packet_sink {
#ifdef CONFIG_PACKET_IO_NAMES
	/** Name of the sink */
	const char *name;
#endif
	/** Execution mode */
	enum {
		SINK_MODE_IMMEDIATE,  /**< Execute handler immediately in source context */
		SINK_MODE_QUEUED,     /**< Queue for later processing */
	} mode;
	/** Handler function (used for IMMEDIATE and QUEUED modes) */
	packet_handler_t handler;
	/** User data passed to handler */
	void *user_data;
	/** Message queue for QUEUED mode */
	struct k_msgq *msgq;
#ifdef CONFIG_PACKET_IO_STATS
	/** Number of packets handled by this sink */
	atomic_t handled_count;
	/** Number of packets dropped (queue full) */
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

/* Only connections need to be in iterable sections for wiring at init time */

/* ============================ Declaration Macros ============================ */

/**
 * @brief Declare a packet source
 *
 * This macro declares an extern reference to a packet source defined elsewhere.
 *
 * @param _name Name of the source variable
 */
#define PACKET_SOURCE_DECLARE(_name) extern struct packet_source _name

/**
 * @brief Declare a packet sink
 *
 * This macro declares an extern reference to a packet sink defined elsewhere.
 *
 * @param _name Name of the sink variable
 */
#define PACKET_SINK_DECLARE(_name) extern struct packet_sink _name

/* ============================ Definition Macros ============================ */

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
		IF_ENABLED(CONFIG_PACKET_IO_NAMES,                           \
			   (.name = #_name,))                                \
		.sinks = SYS_DLIST_STATIC_INIT(&_name.sinks),                \
		.lock = {},                                                   \
		IF_ENABLED(CONFIG_PACKET_IO_STATS,                           \
			   (.send_count = ATOMIC_INIT(0),                    \
			    .queued_total = ATOMIC_INIT(0)))              \
	};

/**
 * @brief Define a message queue for packet events
 *
 * This macro defines a message queue specifically for packet events.
 * Multiple sinks can share this queue for coordinated processing.
 *
 * @param _name Name of the event queue
 * @param _size Maximum number of events in the queue
 */
#define PACKET_EVENT_QUEUE_DEFINE(_name, _size)                               \
	K_MSGQ_DEFINE(_name##_msgq, sizeof(struct packet_event), _size, 4);   \
	struct packet_event_queue _name = {                               \
		.msgq = &_name##_msgq,                                        \
		IF_ENABLED(CONFIG_PACKET_IO_NAMES,                           \
			   (.name = #_name,))                                \
		IF_ENABLED(CONFIG_PACKET_IO_STATS,                           \
			   (.processed_count = ATOMIC_INIT(0),))             \
	}


/**
 * @brief Define a packet sink with immediate execution
 *
 * This macro defines a sink that executes its handler immediately
 * in the source's context when a packet is received.
 *
 * @param _name Name of the sink variable
 * @param _handler Handler function to call for each packet
 */
#define PACKET_SINK_DEFINE_IMMEDIATE(_name, _handler)                         \
	struct packet_sink _name = {                                          \
		IF_ENABLED(CONFIG_PACKET_IO_NAMES,                           \
			   (.name = #_name,))                                \
		.mode = SINK_MODE_IMMEDIATE,                                  \
		.handler = _handler,                                           \
		.user_data = NULL,                                             \
		.msgq = NULL,                                                  \
		IF_ENABLED(CONFIG_PACKET_IO_STATS,                           \
			   (.handled_count = ATOMIC_INIT(0),                 \
			    .dropped_count = ATOMIC_INIT(0)))                \
	};

/**
 * @brief Define a packet sink with queued execution
 *
 * This macro defines a sink that queues packets for later processing.
 * The sink must be associated with a packet event queue.
 *
 * @param _name Name of the sink variable
 * @param _handler Handler function to call for each packet
 * @param _queue Pointer to packet_event_queue to use
 */
#define PACKET_SINK_DEFINE_QUEUED(_name, _handler, _queue)            \
	struct packet_sink _name = {                                          \
		IF_ENABLED(CONFIG_PACKET_IO_NAMES,                           \
			   (.name = #_name,))                                \
		.mode = SINK_MODE_QUEUED,                                     \
		.handler = _handler,                                           \
		.user_data = NULL,                                             \
		.msgq = &(_queue##_msgq),                                     \
		IF_ENABLED(CONFIG_PACKET_IO_STATS,                           \
			   (.handled_count = ATOMIC_INIT(0),                 \
			    .dropped_count = ATOMIC_INIT(0)))                \
	};

/**
 * @brief Define a packet sink with user data
 *
 * @param _name Name of the sink variable
 * @param _handler Handler function to call for each packet
 * @param _msgq Pointer to message queue (struct k_msgq*) or NULL for immediate execution
 * @param _data User data to pass to handler
 */
#define PACKET_SINK_DEFINE_WITH_DATA(_name, _handler, _msgq, _data)  \
	struct packet_sink _name = {                                          \
		IF_ENABLED(CONFIG_PACKET_IO_NAMES,                           \
			   (.name = #_name,))                                \
		.mode = (_msgq) ? SINK_MODE_QUEUED : SINK_MODE_IMMEDIATE,     \
		.handler = _handler,                                           \
		.user_data = _data,                                            \
		.msgq = _msgq,                                                 \
		IF_ENABLED(CONFIG_PACKET_IO_STATS,                           \
			   (.handled_count = ATOMIC_INIT(0),                 \
			    .dropped_count = ATOMIC_INIT(0)))                \
	};

/**
 * @brief Connect a packet source to a packet sink
 *
 * This macro creates a static connection between a source and a sink.
 * The connection is established during system initialization.
 *
 * @param _source_ptr Pointer to the packet source
 * @param _sink_ptr Pointer to the packet sink
 *
 * Example usage:
 *   PACKET_CONNECT(&my_source, &my_sink);
 *   PACKET_CONNECT(&router.network_outbound, &tcp_sink);
 */
#define PACKET_CONNECT(_source_ptr, _sink_ptr)                                \
	static STRUCT_SECTION_ITERABLE(packet_connection,                     \
					CONCAT(__connection_, __COUNTER__)) = {       \
		.source = (_source_ptr),                                      \
		.sink = (_sink_ptr),                                           \
	}

/* Legacy macro for backward compatibility - will be deprecated */
#define PACKET_SOURCE_CONNECT(_source, _sink)                                 \
	PACKET_CONNECT(&_source, &_sink)

/* ============================ Function APIs ============================ */

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

/**
 * @brief Process a single event from a packet event queue
 *
 * This function retrieves and processes one packet event from the queue.
 * The sink's handler is called with the packet, then the buffer is unreferenced.
 *
 * @param queue Pointer to the packet event queue
 * @param timeout Maximum time to wait for an event
 *
 * @return 0 on success, -EAGAIN if no event available, negative errno on error
 */
int packet_event_process(struct packet_event_queue *queue, k_timeout_t timeout);

#ifdef CONFIG_PACKET_IO_RUNTIME_OBSERVERS
/**
 * @brief Connect a source and sink at runtime
 *
 * @note This function is only available when CONFIG_PACKET_IO_RUNTIME_OBSERVERS is enabled.
 *
 * The caller must provide a packet_connection structure with source
 * and sink fields initialized. This structure must remain valid
 * for as long as the connection exists.
 *
 * @warning The connection structure MUST be static or dynamically allocated,
 *          NOT a stack variable. The structure is linked into the source's
 *          connection list and must persist until removed.
 *
 * @param conn Pointer to connection structure with source/sink set
 *
 * @return 0 on success, negative errno on error
 */
int packet_connection_add(struct packet_connection *conn);

/**
 * @brief Disconnect a runtime connection
 *
 * @note This function is only available when CONFIG_PACKET_IO_RUNTIME_OBSERVERS is enabled.
 *
 * This removes the connection between source and sink. The connection
 * structure can be freed or reused after this call returns.
 *
 * @param conn Pointer to the connection to remove
 *
 * @return 0 on success, negative errno on error
 */
int packet_connection_remove(struct packet_connection *conn);
#endif /* CONFIG_PACKET_IO_RUNTIME_OBSERVERS */

#ifdef CONFIG_PACKET_IO_STATS
/**
 * @brief Get source statistics
 *
 * @note This function is only available when CONFIG_PACKET_IO_STATS is enabled.
 *
 * @param src Pointer to the packet source
 * @param send_count Output: number of send operations attempted (can be NULL)
 * @param queued_total Output: total successful queue operations across all sinks (can be NULL)
 */
void packet_source_get_stats(struct packet_source *src,
			      uint32_t *send_count,
			      uint32_t *queued_total);

/**
 * @brief Get sink statistics
 *
 * @note This function is only available when CONFIG_PACKET_IO_STATS is enabled.
 *
 * @param sink Pointer to the packet sink
 * @param handled_count Output: number of packets handled (can be NULL)
 * @param dropped_count Output: number of packets dropped (can be NULL)
 */
void packet_sink_get_stats(struct packet_sink *sink,
			   uint32_t *handled_count,
			   uint32_t *dropped_count);

/**
 * @brief Reset source statistics
 *
 * @note This function is only available when CONFIG_PACKET_IO_STATS is enabled.
 *
 * @param src Pointer to the packet source
 */
void packet_source_reset_stats(struct packet_source *src);

/**
 * @brief Reset sink statistics
 *
 * @note This function is only available when CONFIG_PACKET_IO_STATS is enabled.
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