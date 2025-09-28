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
struct weave_module;
struct weave_method;
struct weave_method_port;
struct weave_signal;
struct weave_signal_handler;
struct weave_message;

/**
 * @brief Method handler function type
 *
 * @param module Module instance
 * @param request Request data
 * @param reply Reply buffer
 * @return 0 on success, negative errno on error
 */
typedef int (*weave_method_handler_t)(void *module, const void *request, void *reply);

/**
 * @brief Signal handler function type
 *
 * @param module Module instance receiving the signal
 * @param event Signal event data
 */
typedef void (*weave_signal_handler_t)(void *module, const void *event);

/**
 * @brief Message structure passed through k_msgq
 *
 * Note: Only a pointer to this structure is passed through k_msgq
 */
struct weave_message {
	/* Message metadata */
	enum weave_msg_type type;

	/* Target method (for requests) */
	struct weave_method *method;

	/* Payload */
	const void *request_data;
	void *reply_data;
	size_t request_size;
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
	struct weave_module *module; /* Owning module */
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
	struct weave_module *module; /* Owning module */
};

/**
 * @brief Weave module structure
 *
 * Modules can integrate with existing threads or run standalone
 */
struct weave_module {
	const char *name;

	/* Message queue for incoming requests (optional) */
	struct k_msgq *request_queue;

	/* Module private data */
	void *private_data;
};

/**
 * @brief Message context for method calls and signals
 *
 * Only the header is allocated from slab, data buffers are allocated
 * from heap based on actual size needed
 */
struct weave_message_context {
	struct weave_message message;

	/* Completion tracking - part of allocated memory to survive timeout */
	struct k_sem completion;
	int result;
	atomic_t refcount; /* Reference count for safe memory management */

	/* Track allocated buffers for cleanup */
	void *allocated_request_data;
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

/**
 * @brief Register a method (service-side)
 *
 * Define your handler function first, then use this macro to register it.
 *
 * @param name Method name for wiring (e.g., read_sensor)
 * @param handler_fn Handler function name
 * @param req_type Request type (e.g., struct read_sensor_request)
 * @param rep_type Reply type (e.g., struct read_sensor_reply)
 *
 * Example:
 *   int my_read_sensor(struct weave_module *module,
 *                      const struct read_sensor_request *request,
 *                      struct read_sensor_reply *reply) { ... }
 *
 *   WEAVE_METHOD_REGISTER(read_sensor, my_read_sensor,
 *                        struct read_sensor_request,
 *                        struct read_sensor_reply);
 */
#define WEAVE_METHOD_REGISTER(name, handler_fn, req_type, rep_type)                                \
	/* Type-safe wrapper that casts void* to proper types */                                   \
	static int _weave_method_##name##_wrapper(void *module, const void *request, void *reply)  \
	{                                                                                          \
		return handler_fn((struct weave_module *)module, (const req_type *)request,        \
				  (rep_type *)reply);                                              \
	}                                                                                          \
	/* Method descriptor for runtime wiring */                                                 \
	struct weave_method name = {#name, _weave_method_##name##_wrapper, sizeof(req_type),       \
				    sizeof(rep_type), NULL}

/**
 * @brief Define a method call port (client-side)
 *
 * @param name Port name (e.g., call_read_temperature)
 * @param req_type Request type
 * @param rep_type Reply type
 */
#define WEAVE_METHOD_PORT_DEFINE(name, req_type, rep_type)                                         \
	struct weave_method_port name = {#name, NULL, sizeof(req_type), sizeof(rep_type)}

/**
 * @brief Connect a method port to a method
 *
 * @param port_name Method port (e.g., call_read_temperature)
 * @param method_name Target method (e.g., read_temperature)
 *
 * Note: Size matching is enforced at runtime during initialization.
 * The types specified in WEAVE_METHOD_DEFINE and WEAVE_METHOD_PORT_DEFINE
 * ensure compile-time type safety.
 */
#define WEAVE_METHOD_CONNECT(port_name, method_name)                                               \
	static const TYPE_SECTION_ITERABLE(                                                        \
		struct weave_method_connection, _CONCAT(__weave_method_conn_, __LINE__),           \
		weave_method_connection, _CONCAT(__weave_method_conn_, __LINE__)) = {              \
		.port = &port_name, .method = &method_name}

/**
 * @brief Define a signal emission port
 *
 * @param name Signal name (e.g., temperature_changed)
 * @param event_type Event data type (e.g., struct temperature_changed_event)
 */
#define WEAVE_SIGNAL_DEFINE(signal_name, event_type)                                               \
	struct weave_signal signal_name = {.name = #signal_name,                                   \
					   .event_size = sizeof(event_type),                       \
					   .handlers =                                             \
						   SYS_SLIST_STATIC_INIT(&(signal_name.handlers))}

/**
 * @brief Register a signal handler (slot)
 *
 * Define your handler function first, then use this macro to register it.
 *
 * @param name Handler name for wiring (e.g., on_threshold_exceeded)
 * @param handler_fn Handler function name
 * @param event_type Event data type (e.g., struct threshold_exceeded_event)
 *
 * Example:
 *   void handle_threshold(struct weave_module *module,
 *                        const struct threshold_exceeded_event *event) { ... }
 *
 *   WEAVE_SIGNAL_HANDLER_REGISTER(on_threshold_exceeded, handle_threshold,
 *                                 struct threshold_exceeded_event);
 */
#define WEAVE_SIGNAL_HANDLER_REGISTER(name, handler_fn, event_type)                                \
	/* Type-safe wrapper that casts void* to proper types */                                   \
	static void _weave_handler_##name##_wrapper(void *module, const void *event)               \
	{                                                                                          \
		handler_fn((struct weave_module *)module, (const event_type *)event);              \
	}                                                                                          \
	/* Handler descriptor for runtime wiring */                                                \
	struct weave_signal_handler name = {                                                       \
		{NULL},                          /* node */                                        \
		#name,                           /* name */                                        \
		_weave_handler_##name##_wrapper, /* handler */                                     \
		NULL                             /* module */                                      \
	}

/**
 * @brief Connect a signal to a handler
 *
 * @param signal_name Signal (e.g., temperature_changed)
 * @param handler_name Handler (e.g., on_temperature_changed)
 */
#define WEAVE_SIGNAL_CONNECT(signal_name, handler_name)                                            \
	static const TYPE_SECTION_ITERABLE(                                                        \
		struct weave_signal_connection, _CONCAT(__weave_signal_conn_, __LINE__),           \
		weave_signal_connection, _CONCAT(__weave_signal_conn_, __LINE__)) = {              \
		.signal = &signal_name, .handler = &handler_name}

/**
 * @brief Define a module with optional message queue
 *
 * @param _name Module variable name
 * @param _queue Message queue (or NULL for direct execution)
 */
#define WEAVE_MODULE_DEFINE(_name, _queue)                                                         \
	struct weave_module _name = {                                                              \
		.name = STRINGIFY(_name), .request_queue = _queue,                                 \
	};                                                                                         \
	static TYPE_SECTION_ITERABLE(struct weave_module *, _name##_ptr, weave_modules,            \
				     _name##_ptr) = &_name

/**
 * @brief Define a message queue for a module
 */
#define WEAVE_MSGQ_DEFINE(_name, _max_msgs)                                                        \
	K_MSGQ_DEFINE(_name, sizeof(struct weave_message *), _max_msgs, 4)

/* Runtime API */

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
	return method && method->module && !method->module->request_queue;
}

/**
 * @brief Check if a method will be queued for deferred execution
 *
 * @param method Method to check
 * @return true if method will be queued, false if immediate
 */
static inline bool weave_method_is_queued(const struct weave_method *method)
{
	return method && method->module && method->module->request_queue != NULL;
}

/**
 * @brief Check if a method port will queue its calls
 *
 * @param port Method port to check
 * @return true if calls through this port will be queued, false if immediate
 */
static inline bool weave_port_will_queue(const struct weave_method_port *port)
{
	if (!port || !port->target_method) {
		return false;
	}
	return weave_method_is_queued(port->target_method);
}

/**
 * @brief Emit a signal to all handlers
 *
 * Can be called from ISR context when using K_NO_WAIT
 *
 * @param signal Signal port
 * @param event Event data
 * @return 0 on success, negative errno on error
 */
int weave_emit_signal(struct weave_signal *signal, const void *event);

/**
 * @brief Process a message received by a module
 *
 * Helper function that modules call from their threads to process messages
 *
 * @param module Module instance
 * @param msg Message to process
 */
void weave_process_message(struct weave_module *module, struct weave_message *msg);

/**
 * @brief Process all pending messages in a module's queue
 *
 * Helper for modules to drain their message queue
 *
 * @param module Module instance
 * @return Number of messages processed
 */
int weave_process_all_messages(struct weave_module *module);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_IO_WEAVE_H_ */