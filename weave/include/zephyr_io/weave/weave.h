/*
 * Copyright (c) 2024 Zephyr IO Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_IO_WEAVE_H_
#define ZEPHYR_IO_WEAVE_H_

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include <zephyr/toolchain/common.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Weave Message Passing Framework
 * @defgroup weave Weave Message Passing
 * @ingroup zephyr_io
 * @{
 */

/** Message types */
enum weave_msg_type {
	WEAVE_MSG_REQUEST = 0,
	WEAVE_MSG_REPLY = 1,
	WEAVE_MSG_SIGNAL = 2,
	WEAVE_MSG_ERROR = 3,
};

/* Forward declarations */
struct weave_method;
struct weave_method_port;
struct weave_signal;
struct weave_signal_handler;
struct weave_message;

/**
 * @brief Method handler function type
 *
 * @param user_data User data associated with the method
 * @param request Request data
 * @param reply Reply buffer
 * @return 0 on success, negative errno on error
 */
typedef int (*weave_method_handler_t)(void *user_data, const void *request, void *reply);

/**
 * @brief Signal handler function type
 *
 * @param user_data User data associated with the handler
 * @param event Signal event data
 */
typedef void (*weave_signal_handler_t)(void *user_data, const void *event);

/**
 * @brief Message structure passed through k_msgq
 *
 * Note: Only a pointer to this structure is passed through k_msgq
 */
struct weave_message {
	/* Message metadata */
	enum weave_msg_type type;

	/* Target method (for requests) or signal handler (for signals) */
	union {
		struct weave_method *method;
		struct weave_signal_handler *handler;
	} target;

	/* Payload */
	const void *data;
	void *reply_data;
	size_t data_size;
	size_t reply_size;

	/* For synchronous calls */
	struct k_sem *completion;
	int *result;
};

/**
 * @brief Method definition (service-side)
 */
struct weave_method {
	const char *name;
	weave_method_handler_t handler;
	size_t request_size;
	size_t reply_size;
	struct k_msgq *queue; /* Optional message queue for deferred execution */
	void *user_data;      /* User data passed to handler */
};

/**
 * @brief Method call port (client-side)
 */
struct weave_method_port {
	const char *name;
	struct weave_method *target_method; /* Wired at compile time */
	size_t request_size;
	size_t reply_size;
};

/**
 * @brief Signal emission port
 */
struct weave_signal {
	const char *name;
	size_t event_size;
	/* Linked list of handlers */
	sys_slist_t handlers;
};

/**
 * @brief Signal handler (slot)
 */
struct weave_signal_handler {
	/* Node for linking into signal's handler list */
	sys_snode_t node;
	const char *name;
	weave_signal_handler_t handler;
	struct k_msgq *queue; /* Optional message queue for deferred execution */
	void *user_data;      /* User data passed to handler */
};

/**
 * @brief Message context for method calls and signals
 */
struct weave_message_context {
	struct weave_message message;

	/* Completion tracking - part of allocated memory to survive timeout */
	struct k_sem completion;
	int result;
	atomic_t refcount; /* Reference count for safe memory management */

	/* Track allocated buffers for cleanup */
	void *allocated_data;
	void *allocated_reply_data;
};

/* Connection structures for compile-time wiring */
struct weave_method_connection {
	struct weave_method_port *port;
	struct weave_method *method;
};

struct weave_signal_connection {
	struct weave_signal *signal;
	struct weave_signal_handler *handler;
};

/* ============================================================================
 * INTERNAL HELPER MACROS
 * ============================================================================ */

/**
 * @brief Generate type-safe wrapper for method handlers
 */
#define Z_WEAVE_METHOD_WRAPPER(_name, _handler, _req_type, _rep_type)                              \
	static int _weave_method_##_name##_wrapper(void *user_data, const void *request,           \
						   void *reply)                                    \
	{                                                                                          \
		return _handler(user_data, (const _req_type *)request, (_rep_type *)reply);        \
	}

/**
 * @brief Generate type-safe wrapper for signal handlers
 */
#define Z_WEAVE_SIGNAL_WRAPPER(_name, _handler, _event_type)                                       \
	static void _weave_handler_##_name##_wrapper(void *user_data, const void *event)           \
	{                                                                                          \
		_handler(user_data, (const _event_type *)event);                                   \
	}

/* ============================================================================
 * METHOD MACROS - Following flow's DECLARE/DEFINE pattern
 * ============================================================================ */

/**
 * @brief Declare a method (forward declaration)
 *
 * @param name Method variable name
 */
#define WEAVE_METHOD_DECLARE(name) extern struct weave_method name

/**
 * @brief Method initializer for static initialization
 *
 * @param _name Method name (for debug)
 * @param _handler Handler function wrapper
 * @param _queue Message queue (or NULL)
 * @param _user_data User data
 * @param _req_size Request size
 * @param _rep_size Reply size
 */
#define WEAVE_METHOD_INITIALIZER(_name, _handler, _queue, _user_data, _req_size, _rep_size)        \
	{                                                                                          \
		.name = STRINGIFY(_name), .handler = _handler, .request_size = _req_size,          \
				  .reply_size = _rep_size, .queue = _queue,                        \
				  .user_data = _user_data                                          \
	}

/**
 * @brief Define a method with immediate execution
 *
 * Usage:
 *   WEAVE_METHOD_DEFINE_IMMEDIATE(my_method, handler, struct req, struct rep)
 *   WEAVE_METHOD_DEFINE_IMMEDIATE(my_method, handler, struct req, struct rep, user_data)
 *
 * @param _name Method variable name
 * @param _handler Handler function
 * @param _req_type Request structure type
 * @param _rep_type Reply structure type
 * @param ... Optional user data (defaults to NULL)
 */
#define WEAVE_METHOD_DEFINE_IMMEDIATE(...)                                                         \
	UTIL_CAT(Z_WEAVE_METHOD_IMMEDIATE_, NUM_VA_ARGS(__VA_ARGS__))(__VA_ARGS__)

#define Z_WEAVE_METHOD_IMMEDIATE_4(_name, _handler, _req_type, _rep_type)                          \
	Z_WEAVE_METHOD_WRAPPER(_name, _handler, _req_type, _rep_type)                              \
	struct weave_method _name =                                                                \
		WEAVE_METHOD_INITIALIZER(_name, _weave_method_##_name##_wrapper, NULL, NULL,       \
					 sizeof(_req_type), sizeof(_rep_type))

#define Z_WEAVE_METHOD_IMMEDIATE_5(_name, _handler, _req_type, _rep_type, _user_data)              \
	Z_WEAVE_METHOD_WRAPPER(_name, _handler, _req_type, _rep_type)                              \
	struct weave_method _name =                                                                \
		WEAVE_METHOD_INITIALIZER(_name, _weave_method_##_name##_wrapper, NULL, _user_data, \
					 sizeof(_req_type), sizeof(_rep_type))

/**
 * @brief Define a method with queued execution
 *
 * Usage:
 *   WEAVE_METHOD_DEFINE_QUEUED(my_method, handler, queue, struct req, struct rep)
 *   WEAVE_METHOD_DEFINE_QUEUED(my_method, handler, queue, struct req, struct rep, user_data)
 *
 * @param _name Method variable name
 * @param _handler Handler function
 * @param _queue Message queue for deferred execution
 * @param _req_type Request structure type
 * @param _rep_type Reply structure type
 * @param ... Optional user data (defaults to NULL)
 */
#define WEAVE_METHOD_DEFINE_QUEUED(...)                                                            \
	UTIL_CAT(Z_WEAVE_METHOD_QUEUED_, NUM_VA_ARGS(__VA_ARGS__))(__VA_ARGS__)

#define Z_WEAVE_METHOD_QUEUED_5(_name, _handler, _queue, _req_type, _rep_type)                     \
	Z_WEAVE_METHOD_WRAPPER(_name, _handler, _req_type, _rep_type)                              \
	struct weave_method _name =                                                                \
		WEAVE_METHOD_INITIALIZER(_name, _weave_method_##_name##_wrapper, _queue, NULL,     \
					 sizeof(_req_type), sizeof(_rep_type))

#define Z_WEAVE_METHOD_QUEUED_6(_name, _handler, _queue, _req_type, _rep_type, _user_data)         \
	Z_WEAVE_METHOD_WRAPPER(_name, _handler, _req_type, _rep_type)                              \
	struct weave_method _name =                                                                \
		WEAVE_METHOD_INITIALIZER(_name, _weave_method_##_name##_wrapper, _queue,           \
					 _user_data, sizeof(_req_type), sizeof(_rep_type))

/* ============================================================================
 * METHOD PORT MACROS (Client-side)
 * ============================================================================ */

/**
 * @brief Declare a method port
 */
#define WEAVE_METHOD_PORT_DECLARE(name) extern struct weave_method_port name

/**
 * @brief Define a method port for calling methods
 *
 * @param name Port variable name
 * @param req_type Request structure type
 * @param rep_type Reply structure type
 */
#define WEAVE_METHOD_PORT_DEFINE(_port_name, req_type, rep_type)                                   \
	struct weave_method_port _port_name = {.name = STRINGIFY(_port_name),                      \
								 .target_method = NULL,            \
								 .request_size = sizeof(req_type), \
								 .reply_size = sizeof(rep_type)}

/* ============================================================================
 * SIGNAL MACROS
 * ============================================================================ */

/**
 * @brief Declare a signal
 */
#define WEAVE_SIGNAL_DECLARE(name) extern struct weave_signal name

/**
 * @brief Define a signal
 *
 * @param name Signal variable name
 * @param event_type Event structure type
 */
#define WEAVE_SIGNAL_DEFINE(_name, event_type)                                                     \
	struct weave_signal _name = {                                                              \
		.name = STRINGIFY(_name), .event_size = sizeof(event_type),                        \
				  .handlers = SYS_SLIST_STATIC_INIT(&(_name.handlers))}

/* ============================================================================
 * SIGNAL HANDLER MACROS
 * ============================================================================ */

/**
 * @brief Declare a signal handler
 */
#define WEAVE_SIGNAL_HANDLER_DECLARE(name) extern struct weave_signal_handler name

/**
 * @brief Signal handler initializer for static initialization
 *
 * @param _name Handler name (for debug)
 * @param _handler Handler function wrapper
 * @param _queue Message queue (or NULL)
 * @param _user_data User data
 */
#define WEAVE_SIGNAL_HANDLER_INITIALIZER(_name, _handler, _queue, _user_data)                      \
	{                                                                                          \
		.node = {NULL}, .name = STRINGIFY(_name), .handler = _handler, .queue = _queue,    \
						  .user_data = _user_data                          \
	}

/**
 * @brief Define a signal handler with immediate execution
 *
 * Usage:
 *   WEAVE_SIGNAL_HANDLER_DEFINE_IMMEDIATE(my_handler, handler_fn, struct event)
 *   WEAVE_SIGNAL_HANDLER_DEFINE_IMMEDIATE(my_handler, handler_fn, struct event, user_data)
 *
 * @param _name Handler variable name
 * @param _handler Handler function
 * @param _event_type Event structure type
 * @param ... Optional user data (defaults to NULL)
 */
#define WEAVE_SIGNAL_HANDLER_DEFINE_IMMEDIATE(...)                                                 \
	UTIL_CAT(Z_WEAVE_SIGNAL_HANDLER_IMMEDIATE_, NUM_VA_ARGS(__VA_ARGS__))(__VA_ARGS__)

#define Z_WEAVE_SIGNAL_HANDLER_IMMEDIATE_3(_name, _handler, _event_type)                           \
	Z_WEAVE_SIGNAL_WRAPPER(_name, _handler, _event_type)                                       \
	struct weave_signal_handler _name = WEAVE_SIGNAL_HANDLER_INITIALIZER(                      \
		_name, _weave_handler_##_name##_wrapper, NULL, NULL)

#define Z_WEAVE_SIGNAL_HANDLER_IMMEDIATE_4(_name, _handler, _event_type, _user_data)               \
	Z_WEAVE_SIGNAL_WRAPPER(_name, _handler, _event_type)                                       \
	struct weave_signal_handler _name = WEAVE_SIGNAL_HANDLER_INITIALIZER(                      \
		_name, _weave_handler_##_name##_wrapper, NULL, _user_data)

/**
 * @brief Define a signal handler with queued execution
 *
 * Usage:
 *   WEAVE_SIGNAL_HANDLER_DEFINE_QUEUED(my_handler, handler_fn, queue, struct event)
 *   WEAVE_SIGNAL_HANDLER_DEFINE_QUEUED(my_handler, handler_fn, queue, struct event, user_data)
 *
 * @param _name Handler variable name
 * @param _handler Handler function
 * @param _queue Message queue for deferred execution
 * @param _event_type Event structure type
 * @param ... Optional user data (defaults to NULL)
 */
#define WEAVE_SIGNAL_HANDLER_DEFINE_QUEUED(...)                                                    \
	UTIL_CAT(Z_WEAVE_SIGNAL_HANDLER_QUEUED_, NUM_VA_ARGS(__VA_ARGS__))(__VA_ARGS__)

#define Z_WEAVE_SIGNAL_HANDLER_QUEUED_4(_name, _handler, _queue, _event_type)                      \
	Z_WEAVE_SIGNAL_WRAPPER(_name, _handler, _event_type)                                       \
	struct weave_signal_handler _name = WEAVE_SIGNAL_HANDLER_INITIALIZER(                      \
		_name, _weave_handler_##_name##_wrapper, _queue, NULL)

#define Z_WEAVE_SIGNAL_HANDLER_QUEUED_5(_name, _handler, _queue, _event_type, _user_data)          \
	Z_WEAVE_SIGNAL_WRAPPER(_name, _handler, _event_type)                                       \
	struct weave_signal_handler _name = WEAVE_SIGNAL_HANDLER_INITIALIZER(                      \
		_name, _weave_handler_##_name##_wrapper, _queue, _user_data)

/* ============================================================================
 * MESSAGE QUEUE MACROS
 * ============================================================================ */

/**
 * @brief Define a message queue for methods/signals
 */
#define WEAVE_MSGQ_DEFINE(_name, _max_msgs)                                                        \
	K_MSGQ_DEFINE(_name, sizeof(struct weave_message *), _max_msgs, 4)

/* ============================================================================
 * CONNECTION MACROS
 * ============================================================================ */

/**
 * @brief Connect a method port to a method
 *
 * @param port_name Method port variable
 * @param method_name Target method variable
 */
#define WEAVE_METHOD_CONNECT(port_name, method_name)                                               \
	static const TYPE_SECTION_ITERABLE(                                                        \
		struct weave_method_connection, _CONCAT(__weave_method_conn_, __LINE__),           \
		weave_method_connection, _CONCAT(__weave_method_conn_, __LINE__)) = {              \
		.port = &port_name, .method = &method_name}

/**
 * @brief Connect a signal to a handler
 *
 * @param signal_name Signal variable
 * @param handler_name Handler variable
 */
#define WEAVE_SIGNAL_CONNECT(signal_name, handler_name)                                            \
	static const TYPE_SECTION_ITERABLE(                                                        \
		struct weave_signal_connection, _CONCAT(__weave_signal_conn_, __LINE__),           \
		weave_signal_connection, _CONCAT(__weave_signal_conn_, __LINE__)) = {              \
		.signal = &signal_name, .handler = &handler_name}

/* ============================================================================
 * RUNTIME API
 * ============================================================================ */

/**
 * @brief Initialize the Weave framework
 *
 * This is called automatically during system initialization.
 * @return 0 on success, negative errno on error
 */
int weave_init(void);

/**
 * @brief Call a method on another module
 *
 * @param port Method port to call
 * @param request Request data (can be NULL if request_size is 0)
 * @param request_size Size of request data
 * @param reply Reply buffer (can be NULL for fire-and-forget calls)
 * @param reply_size Size of reply buffer (ignored if reply is NULL)
 * @param timeout Maximum time to wait for reply
 * @return 0 on success, negative errno on error
 */
int weave_call_method(struct weave_method_port *port, const void *request, size_t request_size,
		      void *reply, size_t reply_size, k_timeout_t timeout);

/**
 * @brief Check if a method will execute immediately (in caller's context)
 *
 * @param method Method to check
 * @return true if method executes immediately, false if queued
 */
static inline bool weave_method_is_immediate(const struct weave_method *method)
{
	return method && method->queue == NULL;
}

/**
 * @brief Check if a method will be queued for deferred execution
 *
 * @param method Method to check
 * @return true if method will be queued, false if immediate
 */
static inline bool weave_method_is_queued(const struct weave_method *method)
{
	return method && method->queue != NULL;
}

/**
 * @brief Connect a method port to a method
 *
 * This establishes a connection between a method port and its target method.
 * The port and method must have matching request and reply sizes.
 *
 * @param port Method port to connect
 * @param method Target method
 * @return 0 on success, -EINVAL if sizes don't match
 */
static inline int weave_method_connect(struct weave_method_port *port, struct weave_method *method)
{
	if (!port || !method) {
		return -EINVAL;
	}

	/* Validate size compatibility */
	if (port->request_size != method->request_size || port->reply_size != method->reply_size) {
		return -EINVAL;
	}

	port->target_method = method;
	return 0;
}

/**
 * @brief Disconnect a method port
 *
 * @param port Method port to disconnect
 */
static inline void weave_method_disconnect(struct weave_method_port *port)
{
	if (port) {
		port->target_method = NULL;
	}
}

/**
 * @brief Check if a method port is connected
 *
 * @param port Method port to check
 * @return true if connected, false otherwise
 */
static inline bool weave_method_is_connected(const struct weave_method_port *port)
{
	return port && port->target_method != NULL;
}

/**
 * @brief Emit a signal to all handlers
 *
 * Can be called from ISR context when all handlers are immediate (no queues)
 *
 * @param signal Signal port
 * @param event Event data
 * @return 0 on success, negative errno on error
 */
int weave_emit_signal(struct weave_signal *signal, const void *event);

/**
 * @brief Process a single message from a queue
 *
 * Helper function for processing threads to handle queued methods/signals
 *
 * @param queue Message queue to process from
 * @param timeout Maximum time to wait for a message
 * @return 0 on success, -EAGAIN if no message, negative errno on error
 */
int weave_process_message(struct k_msgq *queue, k_timeout_t timeout);

/**
 * @brief Process all pending messages in a queue
 *
 * Helper for draining a message queue
 *
 * @param queue Message queue to process
 * @return Number of messages processed
 */
int weave_process_all_messages(struct k_msgq *queue);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_IO_WEAVE_H_ */