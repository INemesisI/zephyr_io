/*
 * Copyright (c) 2024 Zephyr IO Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr_io/weave/weave.h>
#include <zephyr/init.h>
#include <string.h>
#include <stddef.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(weave, CONFIG_WEAVE_LOG_LEVEL);

/* Memory slab for message headers - thread-safe, fixed-size allocation */
K_MEM_SLAB_DEFINE(message_slab, sizeof(struct weave_message_context),
		  CONFIG_WEAVE_MAX_PENDING_REQUESTS, __alignof__(struct weave_message_context));

/* Heap for variable-sized data buffers */
K_HEAP_DEFINE(weave_data_heap, CONFIG_WEAVE_DATA_HEAP_SIZE);


/**
 * @brief Free a message context and its allocated buffers
 */
static void weave_free_context(struct weave_message_context *ctx)
{
	if (!ctx) {
		return;
	}

	if (ctx->allocated_request_data) {
		k_heap_free(&weave_data_heap, ctx->allocated_request_data);
	}
	if (ctx->allocated_reply_data) {
		k_heap_free(&weave_data_heap, ctx->allocated_reply_data);
	}
	k_mem_slab_free(&message_slab, (void *)ctx);
}

/**
 * @brief Decrement reference count and free context if it reaches zero
 *
 * @param ctx Message context to release
 * @return true if the context was freed, false otherwise
 */
static bool weave_context_put(struct weave_message_context *ctx)
{
	if (!ctx) {
		return false;
	}

	/* Atomically decrement and check if we're the last reference */
	if (atomic_dec(&ctx->refcount) == 1) {
		/* We were the last reference, free the context */
		weave_free_context(ctx);
		return true;
	}
	return false;
}

/**
 * @brief Create and initialize a message context
 *
 * @param msg_type Message type (WEAVE_MSG_REQUEST or WEAVE_MSG_SIGNAL)
 * @param refcount Initial reference count
 * @return Allocated context or NULL on failure
 */
static struct weave_message_context *weave_create_message_context(enum weave_msg_type msg_type,
								   int refcount)
{
	struct weave_message_context *ctx;

	/* Allocate context from slab */
	if (k_mem_slab_alloc(&message_slab, (void **)&ctx, K_NO_WAIT) != 0) {
		LOG_WRN("Message pool exhausted");
		return NULL;
	}

	/* Initialize the context */
	memset(ctx, 0, sizeof(*ctx));
	ctx->message.type = msg_type;
	atomic_set(&ctx->refcount, refcount);

	/* Initialize sync primitives for requests */
	if (msg_type == WEAVE_MSG_REQUEST) {
		k_sem_init(&ctx->completion, 0, 1);
		ctx->result = -ETIMEDOUT;
	}

	return ctx;
}

/**
 * @brief Setup request data for a message
 *
 * @param ctx Message context
 * @param data Source data to copy
 * @param size Size of data
 * @return 0 on success, -ENOMEM on failure
 */
static int weave_setup_request_buffer(struct weave_message_context *ctx, const void *data,
				     size_t size)
{
	if (!ctx) {
		return -EINVAL;
	}

	if (data && size > 0) {
		ctx->allocated_request_data = k_heap_alloc(&weave_data_heap, size, K_NO_WAIT);
		if (!ctx->allocated_request_data) {
			LOG_WRN("Data heap exhausted for request (%zu bytes)", size);
			return -ENOMEM;
		}
		memcpy(ctx->allocated_request_data, data, size);
		ctx->message.request_data = ctx->allocated_request_data;
	}
	ctx->message.request_size = size;
	return 0;
}

/**
 * @brief Setup reply buffer for a message
 *
 * @param ctx Message context
 * @param size Size of reply buffer
 * @return 0 on success, -ENOMEM on failure
 */
static int weave_setup_reply_buffer(struct weave_message_context *ctx, size_t size)
{
	if (!ctx) {
		return -EINVAL;
	}

	if (size > 0) {
		ctx->allocated_reply_data = k_heap_alloc(&weave_data_heap, size, K_NO_WAIT);
		if (!ctx->allocated_reply_data) {
			LOG_WRN("Data heap exhausted for reply (%zu bytes)", size);
			return -ENOMEM;
		}
		ctx->message.reply_data = ctx->allocated_reply_data;
	}
	ctx->message.reply_size = size;
	return 0;
}

/**
 * @brief Queue a message to a target module
 *
 * @param ctx Message context (will be freed on failure)
 * @param queue Target message queue
 * @param timeout Queue timeout
 * @return 0 on success, error code on failure
 */
static int weave_queue_message(struct weave_message_context *ctx, struct k_msgq *queue,
				k_timeout_t timeout)
{
	if (!ctx || !queue) {
		return -EINVAL;
	}

	struct weave_message *msg_ptr = &ctx->message;
	int ret = k_msgq_put(queue, &msg_ptr, timeout);
	if (ret != 0) {
		LOG_DBG("Failed to queue message: %d", ret);
		/* Release all references on failure */
		int refs = atomic_get(&ctx->refcount);
		for (int i = 0; i < refs; i++) {
			weave_context_put(ctx);
		}
	}
	return ret;
}

/**
 * @brief Common helper for queueing async messages (methods and signals)
 *
 * @param msg_type Type of message (WEAVE_MSG_REQUEST or WEAVE_MSG_SIGNAL)
 * @param handler Method or signal handler to invoke
 * @param queue Target message queue
 * @param data Request/event data
 * @param data_size Size of request/event data
 * @param reply Reply buffer (NULL for signals)
 * @param reply_size Reply buffer size (0 for signals)
 * @param timeout Queue timeout (also used for completion wait on methods)
 * @return 0 on success, negative errno on error, or result from method call
 */
static int weave_queue_async_message(enum weave_msg_type msg_type, void *handler,
				     struct k_msgq *queue, const void *data,
				     size_t data_size, void *reply, size_t reply_size,
				     k_timeout_t timeout)
{
	struct weave_message_context *ctx;
	int ret;

	/* Initial refcount is always 1 - we'll increment later for requests */
	int initial_refcount = 1;

	/* Create message context */
	ctx = weave_create_message_context(msg_type, initial_refcount);
	if (!ctx) {
		LOG_WRN("Signal pool exhausted, dropping signal");
		return -ENOMEM;
	}

	/* Setup request/event data */
	ret = weave_setup_request_buffer(ctx, data, data_size);
	if (ret != 0) {
		weave_free_context(ctx);
		LOG_WRN("Failed to setup signal data");
		return ret;
	}

	/* Setup reply buffer if needed (only for requests) */
	if (reply && msg_type == WEAVE_MSG_REQUEST) {
		ret = weave_setup_reply_buffer(ctx, reply_size);
		if (ret != 0) {
			weave_free_context(ctx);
			return ret;
		}
	}

	/* Setup message fields */
	ctx->message.method = (struct weave_method *)handler;
	if (msg_type == WEAVE_MSG_REQUEST) {
		ctx->message.completion = &ctx->completion;
		ctx->message.result = &ctx->result;
		/* Increment refcount for requests (sender keeps a reference) */
		atomic_inc(&ctx->refcount);
	}

	/* Queue the message */
	ret = weave_queue_message(ctx, queue, timeout);
	if (ret != 0) {
		return ret;
	}

	/* For signals, we're done - fire and forget */
	if (msg_type == WEAVE_MSG_SIGNAL) {
		return 0;
	}

	/* For methods, wait for completion */
	ret = k_sem_take(&ctx->completion, timeout);
	if (ret != 0) {
		LOG_DBG("Request timed out");
		/* Release our reference - receiver may still be processing */
		weave_context_put(ctx);
		return -ETIMEDOUT;
	}

	/* Copy reply if successful */
	if (ctx->result == 0 && reply && ctx->message.reply_size > 0) {
		memcpy(reply, ctx->message.reply_data,
		       MIN(ctx->message.reply_size, reply_size));
	}

	/* Save result before releasing our reference */
	int result = ctx->result;

	/* Release our reference */
	weave_context_put(ctx);
	return result;
}

/**
 * @brief Call a method on another module
 */
int weave_call_method(struct weave_method_port *port, const void *request, size_t request_size,
		      void *reply, size_t reply_size, k_timeout_t timeout)
{
	struct weave_method *method;
	struct weave_module *target;

	if (!port || !port->target_method) {
		LOG_ERR("Invalid method port or unconnected port");
		return -EINVAL;
	}

	method = port->target_method;
	target = method->module;

	/* Target module must exist for a valid method */
	if (!target) {
		LOG_ERR("Method has no parent module");
		return -EINVAL;
	}

	/* Validate request size - must be at least as large as expected */
	if (request_size < port->request_size) {
		LOG_ERR("Request buffer too small: provided %zu, need at least %zu", request_size,
			port->request_size);
		return -EINVAL;
	}

	/* Validate reply size if reply buffer provided */
	if (reply && reply_size < port->reply_size) {
		LOG_ERR("Reply buffer too small: provided %zu, need at least %zu", reply_size,
			port->reply_size);
		return -EINVAL;
	}

	/* Validate NULL request only allowed if size is 0 */
	if (!request && request_size > 0) {
		LOG_ERR("NULL request with non-zero size %zu", request_size);
		return -EINVAL;
	}

	/* Direct call if no message queue */
	if (!target->request_queue) {
		/* Direct execution in caller's context */
		LOG_DBG("Direct call to method %s", method->name);
		return method->handler(target, request, reply);
	}

	/* Async call through message queue */
	return weave_queue_async_message(WEAVE_MSG_REQUEST, method, target->request_queue,
					  request, port->request_size, reply, port->reply_size,
					  timeout);
}

/**
 * @brief Emit a signal to all handlers
 */
int weave_emit_signal(struct weave_signal *signal, const void *event)
{
	int sent_count = 0;

	if (!signal) {
		return -EINVAL;
	}

	LOG_DBG("Emitting signal %s", signal->name);

	/* Send to all handlers in the linked list */
	struct weave_signal_handler *handler;
	SYS_SLIST_FOR_EACH_CONTAINER(&signal->handlers, handler, node) {
		struct weave_module *target = handler->module;

		if (!target) {
			LOG_WRN("Signal handler has no parent module");
			continue;
		}

		/* Direct execution if no message queue */
		if (!target->request_queue) {
			handler->handler(target, event);
			sent_count++;
			continue;
		}

		/* Queue signal for async processing */
		int ret = weave_queue_async_message(WEAVE_MSG_SIGNAL, handler, target->request_queue,
						     event, signal->event_size, NULL, 0,
						     K_NO_WAIT);
		if (ret == 0) {
			sent_count++;
		}
	}

	LOG_DBG("Signal sent to %d handlers", sent_count);
	return 0;
}

/**
 * @brief Process a message received by a module
 */
void weave_process_message(struct weave_module *module, struct weave_message *msg)
{
	if (!module || !msg) {
		return;
	}

	LOG_DBG("Module %s processing %s message", module->name,
		msg->type == WEAVE_MSG_REQUEST ? "request" : "signal");

	switch (msg->type) {
	case WEAVE_MSG_REQUEST: {
		/* Get the context for cleanup */
		struct weave_message_context *ctx =
			(struct weave_message_context *)((char *)msg -
							 offsetof(struct weave_message_context,
								  message));

		/* Execute method directly */
		int result = -ENOTSUP;

		if (msg->method && msg->method->handler) {
			result = msg->method->handler(module, msg->request_data, msg->reply_data);
		} else {
			LOG_WRN("No handler for method");
		}

		/* Handle synchronous completion for requests */
		if (msg->type == WEAVE_MSG_REQUEST && msg->completion) {
			/* Write result first, before any synchronization */
			if (msg->result) {
				*msg->result = result;
			}

			/* Signal the caller if they're still waiting */
			k_sem_give(msg->completion);
		}

		/* Release receiver's reference */
		weave_context_put(ctx);
	} break;

	case WEAVE_MSG_SIGNAL: {
		/* Process signal - handler stored in method field */
		struct weave_signal_handler *handler = (struct weave_signal_handler *)msg->method;

		if (handler && handler->handler) {
			handler->handler(module, msg->request_data);
		}

		/* Release the signal's reference */
		struct weave_message_context *ctx =
			(struct weave_message_context *)((char *)msg -
							 offsetof(struct weave_message_context,
								  message));
		weave_context_put(ctx);
	} break;

	default:
		LOG_WRN("Unknown message type %d", msg->type);
		break;
	}
}

/**
 * @brief Process all pending messages in a module's queue
 */
int weave_process_all_messages(struct weave_module *module)
{
	struct weave_message *msg;
	int count = 0;

	if (!module || !module->request_queue) {
		return 0;
	}

	/* Process all pending messages */
	while (k_msgq_get(module->request_queue, &msg, K_NO_WAIT) == 0) {
		weave_process_message(module, msg);
		count++;
	}

	return count;
}

/**
 * @brief Wire all connections
 */
static int weave_wire_connections(void)
{
	/* Wire method connections */
	TYPE_SECTION_FOREACH(struct weave_method_connection, weave_method_connection, conn) {
		if (conn->port && conn->method) {
			/* Validate size matching */
			if (conn->port->request_size != conn->method->request_size) {
				LOG_ERR("Request size mismatch: port %s (%zu) != method %s (%zu)",
					conn->port->name, conn->port->request_size,
					conn->method->name, conn->method->request_size);
				return -EINVAL;
			}
			if (conn->port->reply_size != conn->method->reply_size) {
				LOG_ERR("Reply size mismatch: port %s (%zu) != method %s (%zu)",
					conn->port->name, conn->port->reply_size,
					conn->method->name, conn->method->reply_size);
				return -EINVAL;
			}

			conn->port->target_method = conn->method;
			LOG_INF("Wired method port %s to method %s", conn->port->name,
				conn->method->name);
		}
	}

	/* Build handler lists for signals */
	TYPE_SECTION_FOREACH(struct weave_signal_connection, weave_signal_connection, conn) {
		if (conn->signal && conn->handler) {
			/* Add handler to signal's linked list */
			sys_slist_append(&conn->signal->handlers, &conn->handler->node);
			LOG_INF("Connected signal %s to handler %s", conn->signal->name,
				conn->handler->name);
		}
	}

	return 0;
}

/**
 * @brief Initialize the Weave framework
 */
int weave_init(void)
{
	int ret;

	LOG_INF("Weave framework initializing");

	/* Log registered modules */
	TYPE_SECTION_FOREACH(struct weave_module *, weave_modules, module_ptr) {
		struct weave_module *module = *module_ptr;
		LOG_INF("Registered module: %s", module->name);
	}

	/* Wire all connections */
	ret = weave_wire_connections();
	if (ret != 0) {
		LOG_ERR("Failed to wire connections: %d", ret);
		return ret;
	}

	LOG_INF("Weave framework initialized");
	return 0;
}

SYS_INIT(weave_init, APPLICATION, CONFIG_WEAVE_INIT_PRIORITY);