/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Flow public API
 *
 * Flow - Fast Lightweight Object Wiring
 *
 * The Flow subsystem provides zero-copy distribution of net_buf
 * packets from sources to multiple sinks using reference counting.
 */

#ifndef ZEPHYR_INCLUDE_FLOW_H_
#define ZEPHYR_INCLUDE_FLOW_H_

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/buf.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup flow_apis Flow APIs
 * @ingroup os_services
 * @{
 */

/* ============================ Type Definitions ============================ */

/* Forward declarations */
struct flow_sink;
struct flow_event_queue;

/** @brief Special packet ID that matches any packet */
#define FLOW_PACKET_ID_ANY 0xFFFF

/**
 * @brief Flow IO handler callback signature
 *
 * @note The handler MUST NOT call net_buf_unref() on the buffer.
 *       The framework automatically manages buffer references.
 *
 * @param sink The sink that received the packet
 * @param buf The packet buffer (do not unref)
 */
typedef void (*flow_handler_t)(struct flow_sink *sink, struct net_buf *buf);

/** @brief Flow IO source structure */
struct flow_source {
#ifdef CONFIG_FLOW_NAMES
	/** Name of the source */
	const char *name;
#endif
	/** Packet ID to stamp on outgoing packets (FLOW_PACKET_ID_ANY = don't stamp)
	 */
	uint16_t packet_id;
	/** List of connections to sinks */
	sys_slist_t connections;
	/** Lock protecting the connection list */
	struct k_spinlock lock;
#ifdef CONFIG_FLOW_STATS
	/** Number of send operations attempted */
	atomic_t send_count;
	/** Total number of successful queue operations across all sinks */
	atomic_t queued_total;
#endif
};

/** @brief Flow IO event for queued delivery */
struct flow_event {
	/** Sink that received this packet */
	struct flow_sink *sink;
	/** The packet buffer */
	struct net_buf *buf;
};

/** @brief Flow IO event queue for managing message queues */
struct flow_event_queue {
	/** Message queue for packet events */
	struct k_msgq *msgq;
#ifdef CONFIG_FLOW_NAMES
	/** Name of the queue */
	const char *name;
#endif
#ifdef CONFIG_FLOW_STATS
	/** Number of events processed */
	atomic_t processed_count;
#endif
};

/** @brief Flow IO sink structure */
struct flow_sink {
#ifdef CONFIG_FLOW_NAMES
	/** Name of the sink */
	const char *name;
#endif
	/** Packet ID to accept (FLOW_PACKET_ID_ANY = accept all) */
	uint16_t accept_id;
	/** Execution mode */
	enum {
		SINK_MODE_IMMEDIATE, /**< Execute handler immediately in source context */
		SINK_MODE_QUEUED,    /**< Queue for later processing */
	} mode;
	/** Handler function (used for IMMEDIATE and QUEUED modes) */
	flow_handler_t handler;
	/** User data passed to handler */
	void *user_data;
	/** Message queue for QUEUED mode */
	struct k_msgq *msgq;
#ifdef CONFIG_FLOW_STATS
	/** Number of packets handled by this sink */
	atomic_t handled_count;
	/** Number of packets dropped (queue full) */
	atomic_t dropped_count;
#endif
};

/** @brief Connection between source and sink */
struct flow_connection {
	/** Pointer to the source */
	struct flow_source *source;
	/** Pointer to the sink */
	struct flow_sink *sink;
	/** Node for source's connection list */
	sys_snode_t node;
};

/* Only connections need to be in iterable sections for wiring at init time */

/* ============================ Declaration Macros ============================
 */

/**
 * @brief Declare a Flow IO source
 *
 * This macro declares an extern reference to a Flow IO source defined
 * elsewhere.
 *
 * @param _name Name of the source variable
 */
#define FLOW_SOURCE_DECLARE(_name) extern struct flow_source _name

/**
 * @brief Declare a Flow IO sink
 *
 * This macro declares an extern reference to a Flow IO sink defined elsewhere.
 *
 * @param _name Name of the sink variable
 */
#define FLOW_SINK_DECLARE(_name) extern struct flow_sink _name

/* ============================ Initializer Macros ============================
 */

/**
 * @brief Static initializer for packet source
 *
 * @param _name Name of the source (used for list initialization)
 * @param _id Packet ID to stamp on outgoing packets (FLOW_PACKET_ID_ANY for no
 * stamping)
 */
#define FLOW_SOURCE_INITIALIZER(_name, _id)                                                        \
	{                                                                                          \
		IF_ENABLED(CONFIG_FLOW_NAMES, (.name = #_name, ))                                  \
			.packet_id = (_id),                                                        \
		      .connections = SYS_SLIST_STATIC_INIT(&_name.connections), .lock = {},        \
		      IF_ENABLED(CONFIG_FLOW_STATS,                                                \
				 (.send_count = ATOMIC_INIT(0), .queued_total = ATOMIC_INIT(0)))   \
	}

/**
 * @brief Static initializer for packet sinks
 *
 * @param _name Name identifier of the sink (gets stringified for debug names)
 * @param _mode Sink mode (SINK_MODE_IMMEDIATE or SINK_MODE_QUEUED)
 * @param _handler Handler function to call for each packet
 * @param _msgq Message queue pointer (NULL for immediate mode)
 * @param _user_data User data to pass to handler
 * @param _id Packet ID to accept (FLOW_PACKET_ID_ANY for all)
 */
#define FLOW_SINK_INITIALIZER(_name, _mode, _handler, _msgq, _user_data, _id)                      \
	{                                                                                          \
		IF_ENABLED(CONFIG_FLOW_NAMES, (.name = #_name, ))                                  \
			.accept_id = (_id),                                                        \
		      .mode = (_mode), .handler = (_handler), .msgq = (_msgq),                     \
		      .user_data = (_user_data),                                                   \
		      IF_ENABLED(CONFIG_FLOW_STATS, (.handled_count = ATOMIC_INIT(0),              \
						     .dropped_count = ATOMIC_INIT(0)))             \
	}

/* ============================ Definition Macros ============================
 */

/* -------------------- Source Definitions -------------------- */

/**
 * @brief Define a packet source
 *
 * This macro defines and initializes a packet source. The source is
 * non-static so it can be referenced from other files.
 *
 * @param _name Name of the source variable
 */
#define FLOW_SOURCE_DEFINE(_name)                                                                  \
	struct flow_source _name = FLOW_SOURCE_INITIALIZER(_name, FLOW_PACKET_ID_ANY);

/* -------------------- Sink Definitions -------------------- */

/**
 * @brief Define a packet sink with immediate execution
 *
 * This macro defines a sink that executes its handler immediately
 * in the source's context when a packet is received.
 *
 * @param _name Name of the sink variable
 * @param _handler Handler function to call for each packet
 * @param ... Optional user data (defaults to NULL)
 */
#define FLOW_SINK_DEFINE_IMMEDIATE(...)                                                            \
	UTIL_CAT(Z_FLOW_SINK_IMMEDIATE_, NUM_VA_ARGS(__VA_ARGS__))(__VA_ARGS__)

#define Z_FLOW_SINK_IMMEDIATE_2(_name, _handler)                                                   \
	struct flow_sink _name = FLOW_SINK_INITIALIZER(_name, SINK_MODE_IMMEDIATE, _handler, NULL, \
						       NULL, FLOW_PACKET_ID_ANY)

#define Z_FLOW_SINK_IMMEDIATE_3(_name, _handler, _data)                                            \
	struct flow_sink _name = FLOW_SINK_INITIALIZER(_name, SINK_MODE_IMMEDIATE, _handler, NULL, \
						       _data, FLOW_PACKET_ID_ANY)

/**
 * @brief Define a packet sink with queued execution
 *
 * This macro defines a sink that queues packets for later processing.
 * The sink must be associated with a packet event queue.
 *
 * @param _name Name of the sink variable
 * @param _handler Handler function to call for each packet
 * @param _queue Pointer to flow_event_queue to use
 * @param ... Optional user data (defaults to NULL)
 */
#define FLOW_SINK_DEFINE_QUEUED(...)                                                               \
	UTIL_CAT(Z_FLOW_SINK_QUEUED_, NUM_VA_ARGS(__VA_ARGS__))(__VA_ARGS__)

#define Z_FLOW_SINK_QUEUED_3(_name, _handler, _queue)                                              \
	struct flow_sink _name = FLOW_SINK_INITIALIZER(_name, SINK_MODE_QUEUED, _handler,          \
						       &(_queue##_msgq), NULL, FLOW_PACKET_ID_ANY)

#define Z_FLOW_SINK_QUEUED_4(_name, _handler, _queue, _data)                                       \
	struct flow_sink _name = FLOW_SINK_INITIALIZER(                                            \
		_name, SINK_MODE_QUEUED, _handler, &(_queue##_msgq), _data, FLOW_PACKET_ID_ANY)

/* -------------------- Routed Source Definitions -------------------- */

/**
 * @brief Define a routed packet source with specific packet ID
 *
 * This source stamps all outgoing packets with the specified packet ID.
 *
 * @param _name Name of the source variable
 * @param _id Packet ID to stamp on outgoing packets
 */
#define FLOW_SOURCE_DEFINE_ROUTED(_name, _id)                                                      \
	struct flow_source _name = FLOW_SOURCE_INITIALIZER(_name, _id)

/* -------------------- Routed Sink Definitions -------------------- */

/**
 * @brief Define a routed sink that accepts specific packet ID
 *
 * This sink only processes packets with matching packet ID.
 *
 * @param _name Name of the sink variable
 * @param _handler Handler function
 * @param _id Packet ID to accept (or FLOW_PACKET_ID_ANY for all)
 * @param ... Optional user data (defaults to NULL)
 */
#define FLOW_SINK_DEFINE_ROUTED_IMMEDIATE(...)                                                     \
	UTIL_CAT(Z_FLOW_SINK_ROUTED_IMMEDIATE_, NUM_VA_ARGS(__VA_ARGS__))(__VA_ARGS__)

#define Z_FLOW_SINK_ROUTED_IMMEDIATE_3(_name, _handler, _id)                                       \
	struct flow_sink _name =                                                                   \
		FLOW_SINK_INITIALIZER(_name, SINK_MODE_IMMEDIATE, _handler, NULL, NULL, _id)

#define Z_FLOW_SINK_ROUTED_IMMEDIATE_4(_name, _handler, _id, _data)                                \
	struct flow_sink _name =                                                                   \
		FLOW_SINK_INITIALIZER(_name, SINK_MODE_IMMEDIATE, _handler, NULL, _data, _id)

/**
 * @brief Define a routed queued sink with packet ID filter
 *
 * @param _name Name of the sink variable
 * @param _handler Handler function
 * @param _queue Event queue to use
 * @param _id Packet ID to accept
 * @param ... Optional user data (defaults to NULL)
 */
#define FLOW_SINK_DEFINE_ROUTED_QUEUED(...)                                                        \
	UTIL_CAT(Z_FLOW_SINK_ROUTED_QUEUED_, NUM_VA_ARGS(__VA_ARGS__))(__VA_ARGS__)

#define Z_FLOW_SINK_ROUTED_QUEUED_4(_name, _handler, _queue, _id)                                  \
	struct flow_sink _name = FLOW_SINK_INITIALIZER(_name, SINK_MODE_QUEUED, _handler,          \
						       &(_queue##_msgq), NULL, _id)

#define Z_FLOW_SINK_ROUTED_QUEUED_5(_name, _handler, _queue, _id, _data)                           \
	struct flow_sink _name = FLOW_SINK_INITIALIZER(_name, SINK_MODE_QUEUED, _handler,          \
						       &(_queue##_msgq), _data, _id)

/* -------------------- Event Queue Definition -------------------- */

/**
 * @brief Define a message queue for packet events
 *
 * This macro defines a message queue specifically for packet events.
 * Multiple sinks can share this queue for coordinated processing.
 *
 * @param _name Name of the event queue
 * @param _size Maximum number of events in the queue
 */
#define FLOW_EVENT_QUEUE_DEFINE(_name, _size)                                                      \
	K_MSGQ_DEFINE(_name##_msgq, sizeof(struct flow_event), _size, 4);                          \
	struct flow_event_queue _name = {                                                          \
		.msgq = &_name##_msgq,                                                             \
		IF_ENABLED(CONFIG_FLOW_NAMES, (.name = #_name, ))                                  \
			IF_ENABLED(CONFIG_FLOW_STATS, (.processed_count = ATOMIC_INIT(0), ))}

/* -------------------- Connection Definition -------------------- */

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
 *   FLOW_CONNECT(&my_source, &my_sink);
 *   FLOW_CONNECT(&router.network_outbound, &tcp_sink);
 */
#define FLOW_CONNECT(_source_ptr, _sink_ptr)                                                       \
	static STRUCT_SECTION_ITERABLE(flow_connection, CONCAT(__connection_, __COUNTER__)) = {    \
		.source = (_source_ptr),                                                           \
		.sink = (_sink_ptr),                                                               \
	}

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
 * @param timeout Maximum time to wait for all sinks (K_NO_WAIT, K_FOREVER, or
 * timeout)
 *
 * @return Number of sinks that successfully received the packet
 */
int flow_source_send(struct flow_source *src, struct net_buf *buf, k_timeout_t timeout);

/**
 * @brief Send a packet from source to all connected sinks (consuming reference)
 *
 * This function sends a net_buf packet to all sinks connected to the
 * source. The function CONSUMES the caller's reference to the buffer.
 * Each successfully queued sink gets its own reference.
 *
 * This is more efficient than flow_source_send() when the caller
 * doesn't need the buffer after sending, as it avoids an extra
 * ref/unref cycle.
 *
 * The timeout represents the total time the send operation may take.
 * If a sink blocks and the timeout expires, remaining sinks are
 * attempted with K_NO_WAIT to ensure all sinks get a chance.
 *
 * @param src Pointer to the packet source
 * @param buf Pointer to the net_buf to send (reference IS consumed)
 * @param timeout Maximum time to wait for all sinks (K_NO_WAIT, K_FOREVER, or
 * timeout)
 *
 * @return Number of sinks that successfully received the packet
 */
int flow_source_send_consume(struct flow_source *src, struct net_buf *buf, k_timeout_t timeout);

/**
 * @brief Process a single event from a packet event queue
 *
 * This function retrieves and processes one packet event from the queue.
 * The sink's handler is called with the packet, then the buffer is
 * unreferenced.
 *
 * @param queue Pointer to the packet event queue
 * @param timeout Maximum time to wait for an event
 *
 * @return 0 on success, -EAGAIN if no event available, negative errno on error
 */
int flow_event_process(struct flow_event_queue *queue, k_timeout_t timeout);

/**
 * @brief Deliver a packet directly to a sink
 *
 * This function delivers a packet to a single sink, handling both IMMEDIATE
 * and QUEUED execution modes. For IMMEDIATE mode, the handler is called
 * directly. For QUEUED mode, the packet event is placed in the sink's
 * message queue.
 *
 * The function takes a new reference to the buffer for the sink, so the
 * caller's reference is preserved (NOT consumed). The caller must still unref
 * their buffer after calling this function. The sink's handler should NOT call
 * net_buf_unref() as the framework manages buffer references.
 *
 * @param sink Pointer to the packet sink
 * @param buf Pointer to the net_buf to deliver (reference is NOT consumed)
 * @param timeout Maximum time to wait for queuing (only used for QUEUED mode)
 *
 * @return 0 on success, negative errno on error:
 *         -EINVAL if sink or buf is NULL
 *         -ENOSYS if QUEUED mode but no message queue configured
 *         -ENOBUFS if message queue is full
 *         -ENOTSUP if sink mode is invalid
 */
int flow_sink_deliver(struct flow_sink *sink, struct net_buf *buf, k_timeout_t timeout);

/**
 * @brief Deliver a packet directly to a sink (consuming reference)
 *
 * This function delivers a packet to a single sink, handling both IMMEDIATE
 * and QUEUED execution modes. For IMMEDIATE mode, the handler is called
 * directly. For QUEUED mode, the packet event is placed in the sink's
 * message queue.
 *
 * This function CONSUMES the caller's reference to the buffer. The caller
 * should NOT unref the buffer after calling this function. This is more
 * efficient than flow_sink_deliver() when the caller doesn't need the
 * buffer after delivery, as it avoids an extra ref/unref cycle.
 *
 * @param sink Pointer to the packet sink
 * @param buf Pointer to the net_buf to deliver (reference IS consumed)
 * @param timeout Maximum time to wait for queuing (only used for QUEUED mode)
 *
 * @return 0 on success, negative errno on error:
 *         -EINVAL if sink or buf is NULL
 *         -ENOSYS if QUEUED mode but no message queue configured
 *         -ENOBUFS if message queue is full
 *         -ENOTSUP if sink mode is invalid
 */
int flow_sink_deliver_consume(struct flow_sink *sink, struct net_buf *buf, k_timeout_t timeout);

#ifdef CONFIG_FLOW_RUNTIME_OBSERVERS
/**
 * @brief Create a runtime connection between source and sink
 *
 * @note This function is only available when CONFIG_FLOW_RUNTIME_OBSERVERS is
 * enabled.
 *
 * Creates a runtime connection between the specified source and sink.
 * The connection is allocated from an internal pool of size
 * CONFIG_FLOW_RUNTIME_CONNECTION_POOL_SIZE.
 *
 * @param source Pointer to the source to connect from
 * @param sink Pointer to the sink to connect to
 *
 * @return 0 on success
 * @retval -EINVAL if source or sink is NULL
 * @retval -EALREADY if source and sink are already connected
 * @retval -ENOMEM if the connection pool is exhausted
 */
int flow_runtime_connect(struct flow_source *source, struct flow_sink *sink);

/**
 * @brief Disconnect a runtime connection between source and sink
 *
 * @note This function is only available when CONFIG_FLOW_RUNTIME_OBSERVERS is
 * enabled.
 *
 * Removes the runtime connection between the specified source and sink,
 * returning the connection slot to the internal pool.
 *
 * @param source Pointer to the source to disconnect from
 * @param sink Pointer to the sink to disconnect from
 *
 * @return 0 on success
 * @retval -EINVAL if source or sink is NULL
 * @retval -ENOENT if no connection exists between source and sink
 */
int flow_runtime_disconnect(struct flow_source *source, struct flow_sink *sink);
#endif /* CONFIG_FLOW_RUNTIME_OBSERVERS */

#ifdef CONFIG_FLOW_STATS
/**
 * @brief Get source statistics
 *
 * @note This function is only available when CONFIG_FLOW_STATS is enabled.
 *
 * @param src Pointer to the packet source
 * @param send_count Output: number of send operations attempted (can be NULL)
 * @param queued_total Output: total successful queue operations across all
 * sinks (can be NULL)
 */
void flow_source_get_stats(struct flow_source *src, uint32_t *send_count, uint32_t *queued_total);

/**
 * @brief Get sink statistics
 *
 * @note This function is only available when CONFIG_FLOW_STATS is enabled.
 *
 * @param sink Pointer to the packet sink
 * @param handled_count Output: number of packets handled (can be NULL)
 * @param dropped_count Output: number of packets dropped (can be NULL)
 */
void flow_sink_get_stats(struct flow_sink *sink, uint32_t *handled_count, uint32_t *dropped_count);

/**
 * @brief Reset source statistics
 *
 * @note This function is only available when CONFIG_FLOW_STATS is enabled.
 *
 * @param src Pointer to the packet source
 */
void flow_source_reset_stats(struct flow_source *src);

/**
 * @brief Reset sink statistics
 *
 * @note This function is only available when CONFIG_FLOW_STATS is enabled.
 *
 * @param sink Pointer to the packet sink
 */
void flow_sink_reset_stats(struct flow_sink *sink);
#endif /* CONFIG_FLOW_STATS */

/**
 * @brief Set packet ID in a buffer
 *
 * Stores the packet ID in the buffer's user data area.
 *
 * @param buf Pointer to the network buffer
 * @param packet_id Packet ID to set
 * @return 0 on success, -ENOBUFS if buffer has insufficient user data space
 */
static inline int flow_packet_id_set(struct net_buf *buf, uint16_t packet_id)
{
	if (!buf || buf->user_data_size < sizeof(uint16_t)) {
		return -ENOBUFS;
	}
	uint16_t *pkt_id = (uint16_t *)net_buf_user_data(buf);
	*pkt_id = packet_id;
	return 0;
}

/**
 * @brief Get packet ID from a buffer
 *
 * Retrieves the packet ID from the buffer's user data area.
 *
 * @param buf Pointer to the network buffer
 * @param packet_id Pointer to store the packet ID
 * @return 0 on success, -ENOBUFS if buffer has insufficient user data space,
 *         -EINVAL if buf or packet_id is NULL
 */
static inline int flow_packet_id_get(struct net_buf *buf, uint16_t *packet_id)
{
	if (!buf || !packet_id) {
		return -EINVAL;
	}
	if (buf->user_data_size < sizeof(uint16_t)) {
		*packet_id = FLOW_PACKET_ID_ANY; /* Default to ANY if no space */
		return -ENOBUFS;
	}
	uint16_t *pkt_id = (uint16_t *)net_buf_user_data(buf);
	*packet_id = *pkt_id;
	return 0;
}

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_FLOW_H_ */