/*
 * Copyright (c) 2026 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Weave Core API
 *
 * Weave Core - Foundation for message passing
 *
 * The Weave Core subsystem provides generic message passing with sources and sinks.
 * It serves as the foundation for:
 * - Packet (net_buf routing)
 * - Call (RPC)
 * - Observable (stateful pub/sub)
 */

#ifndef ZEPHYR_INCLUDE_WEAVE_CORE_H_
#define ZEPHYR_INCLUDE_WEAVE_CORE_H_

#include <zephyr/kernel.h>
#include <zephyr/sys/slist.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup weave_core_apis Weave Core APIs
 * @ingroup os_services
 * @{
 */

/* ============================ Constants ============================ */

/** @brief Immediate mode - no queue, handler runs in sender context */
#define WV_IMMEDIATE NULL

/** @brief No lifecycle operations */
#define WV_NO_OPS NULL

/* ============================ Type Definitions ============================ */

/* Forward declarations */
struct weave_sink;
struct weave_source;

/**
 * @brief Handler function signature
 *
 * @param ptr Payload pointer
 * @param user_data User data from sink definition
 */
typedef void (*weave_handler_t)(void *ptr, void *user_data);

/**
 * @brief Payload operations for lifecycle management
 *
 * Pluggable callbacks for pointer lifecycle. Enables different buffer types:
 * - net_buf with ref counting (Packet)
 * - mem_slab with custom management
 * - Simple pointers with no lifecycle
 */
struct weave_payload_ops {
	/** Called before delivery to take reference and optionally filter.
	 *  Return 0 on success (ref taken), negative to skip this sink. */
	int (*ref)(void *ptr, struct weave_sink *sink);
	/** Called after handler or on failure to release reference */
	void (*unref)(void *ptr);
};

/**
 * @brief Weave source structure
 *
 * A source emits messages to connected sinks (one-to-many distribution).
 */
struct weave_source {
	/** List of connections to sinks */
	sys_slist_t sinks;
	/** Payload lifecycle operations (ref/unref/filter) */
	const struct weave_payload_ops *ops;
	/** Lock protecting the sink list */
	struct k_spinlock lock;
};

/**
 * @brief Weave sink structure
 *
 * A sink receives messages, either immediately (in sender's context)
 * or queued (deferred to processing thread).
 */
struct weave_sink {
	/** Handler function */
	weave_handler_t handler;
	/** User data passed to handler */
	void *user_data;
	/** Message queue (NULL = immediate mode) */
	struct k_msgq *queue;
	/** Payload lifecycle operations (for direct send) */
	const struct weave_payload_ops *ops;
};

/**
 * @brief Connection between source and sink
 *
 * Static compile-time wiring via iterable sections.
 */
struct weave_connection {
	/** Pointer to the source */
	struct weave_source *source;
	/** Pointer to the sink */
	struct weave_sink *sink;
	/** Node for source's sink list */
	sys_snode_t node;
};

/**
 * @brief Event structure for queued delivery
 *
 * Minimal event passed through message queue.
 */
struct weave_event {
	/** Target sink */
	struct weave_sink *sink;
	/** Payload pointer */
	void *ptr;
};

/* ============================ Macros ============================ */

/**
 * @brief Static initializer for weave source
 *
 * @param _name Source name (for static init)
 * @param _ops Signal ops pointer
 */
#define WEAVE_SOURCE_INITIALIZER(_name, _ops)                                                      \
	{                                                                                          \
		.sinks = SYS_SLIST_STATIC_INIT(&_name.sinks), .ops = (_ops), .lock = {},           \
	}

/**
 * @brief Static initializer for weave sink
 *
 * @param _handler Handler function
 * @param _queue Message queue pointer (NULL/WV_IMMEDIATE for immediate)
 * @param _user_data User data passed to handler
 * @param _ops Signal ops pointer (NULL/WV_NO_OPS if none)
 */
#define WEAVE_SINK_INITIALIZER(_handler, _queue, _user_data, _ops)                                 \
	{                                                                                          \
		.handler = (_handler), .user_data = (void *)(_user_data), .queue = (_queue),       \
		.ops = (_ops),                                                                     \
	}

/**
 * @brief Define a weave source
 *
 * @param _name Source variable name
 * @param _ops Signal ops pointer (can be NULL/WV_NO_OPS)
 */
#define WEAVE_SOURCE_DEFINE(_name, _ops)                                                           \
	struct weave_source _name = WEAVE_SOURCE_INITIALIZER(_name, _ops)

/**
 * @brief Declare a weave source (for header files)
 *
 * @param _name Source variable name
 */
#define WEAVE_SOURCE_DECLARE(_name) extern struct weave_source _name

/**
 * @brief Define a weave sink
 *
 * @param _name Sink variable name
 * @param _handler Handler function
 * @param _queue Message queue (WV_IMMEDIATE for immediate mode, or &queue for queued)
 * @param _user_data User data pointer (NULL if unused)
 * @param _ops Signal ops pointer (WV_NO_OPS if none)
 */
#define WEAVE_SINK_DEFINE(_name, _handler, _queue, _user_data, _ops)                               \
	struct weave_sink _name = WEAVE_SINK_INITIALIZER(_handler, _queue, _user_data, _ops)

/**
 * @brief Declare a weave sink (for header files)
 *
 * @param _name Sink variable name
 */
#define WEAVE_SINK_DECLARE(_name) extern struct weave_sink _name

/**
 * @brief Define a message queue for weave events
 *
 * @param _name Queue variable name
 * @param _depth Maximum number of events
 */
#define WEAVE_MSGQ_DEFINE(_name, _depth) K_MSGQ_DEFINE(_name, sizeof(struct weave_event), _depth, 4)

/**
 * @brief Connect a source to a sink (compile-time)
 *
 * Creates a static connection placed in iterable section.
 *
 * @param _source Pointer to source
 * @param _sink Pointer to sink
 */
#define WEAVE_CONNECT(_source, _sink)                                                              \
	static STRUCT_SECTION_ITERABLE(weave_connection, CONCAT(__weave_conn_, __COUNTER__)) = {   \
		.source = (_source),                                                               \
		.sink = (_sink),                                                                   \
	}

/* ============================ Function APIs ============================ */

/**
 * @brief Emit a message to all connected sinks
 *
 * Distributes the message to all sinks connected to the source.
 * For each sink, if the source has ops:
 * - Filter is checked (if provided)
 * - Ref is called before delivery
 * - Unref is called after handler or on failure
 *
 * The timeout is distributed across sinks - if one blocks, remaining
 * sinks get the remaining time.
 *
 * @param source Pointer to the source
 * @param ptr Payload pointer
 * @param timeout Maximum time for all deliveries
 *
 * @return Number of successful deliveries, or negative errno
 */
int weave_source_emit(struct weave_source *source, void *ptr, k_timeout_t timeout);

/**
 * @brief Send a message directly to a sink
 *
 * Point-to-point delivery bypassing source routing.
 * Uses the sink's ops if provided, otherwise no lifecycle management.
 *
 * @param sink Pointer to the sink
 * @param ptr Payload pointer
 * @param timeout Maximum time for delivery
 *
 * @return 0 on success, negative errno on failure
 */
int weave_sink_send(struct weave_sink *sink, void *ptr, k_timeout_t timeout);

/**
 * @brief Process messages from a queue
 *
 * Waits for the first message with the given timeout, then drains
 * all remaining available messages without blocking.
 * Calls each sink's handler and manages lifecycle via ops.
 *
 * @param queue Pointer to the message queue
 * @param timeout Maximum time to wait for the first message
 *
 * @return Number of messages processed, or negative errno on error
 */
int weave_process_messages(struct k_msgq *queue, k_timeout_t timeout);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_WEAVE_CORE_H_ */
