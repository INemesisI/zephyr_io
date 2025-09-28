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

	if (ctx->allocated_data) {
		k_heap_free(&weave_data_heap, ctx->allocated_data);
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
 * @param msg_type Message type
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
 * @brief Setup data buffer for a message
 *
 * @param ctx Message context
 * @param data Source data to copy
 * @param size Size of data
 * @return 0 on success, -ENOMEM on failure
 */
static int weave_setup_data_buffer(struct weave_message_context *ctx, const void *data, size_t size)
{
	if (!ctx) {
		return -EINVAL;
	}

	if (data && size > 0) {
		ctx->allocated_data = k_heap_alloc(&weave_data_heap, size, K_NO_WAIT);
		if (!ctx->allocated_data) {
			LOG_WRN("Data heap exhausted (%zu bytes)", size);
			return -ENOMEM;
		}
		memcpy(ctx->allocated_data, data, size);
		ctx->message.data = ctx->allocated_data;
	}
	ctx->message.data_size = size;
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
 * @brief Execute a method immediately in the caller's context
 *
 * @param method Method to execute
 * @param request Request data
 * @param request_size Size of request
 * @param reply Reply buffer
 * @param reply_size Size of reply buffer
 * @return Result from method handler
 */
static int weave_execute_method_immediate(struct weave_method *method, const void *request,
					  size_t request_size, void *reply, size_t reply_size)
{
	if (!method) {
		return -EINVAL;
	}

	if (!method->handler) {
		LOG_ERR("Method has no handler");
		return -ENOTSUP;
	}

	/* Validate parameters */
	if (request_size > 0 && !request) {
		LOG_ERR("NULL request with non-zero size");
		return -EINVAL;
	}

	/* Validate sizes */
	if (request_size < method->request_size) {
		LOG_ERR("Request too small: got %zu, need %zu", request_size, method->request_size);
		return -EINVAL;
	}

	if (reply && reply_size < method->reply_size) {
		LOG_ERR("Reply buffer too small: got %zu, need %zu", reply_size,
			method->reply_size);
		return -EINVAL;
	}

	/* Call handler directly */
	return method->handler(method->user_data, request, reply);
}

/**
 * @brief Queue a message to a message queue
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
 * @brief Queue a method call for deferred execution
 *
 * @param method Method to call
 * @param request Request data
 * @param request_size Size of request
 * @param reply Reply buffer
 * @param reply_size Size of reply buffer
 * @param timeout Timeout for queueing and completion
 * @return Result from method or error
 */
static int weave_execute_method_queued(struct weave_method *method, const void *request,
				       size_t request_size, void *reply, size_t reply_size,
				       k_timeout_t timeout)
{
	struct weave_message_context *ctx;
	int ret;

	if (!method || !method->queue) {
		return -EINVAL;
	}

	/* Validate parameters */
	if (request_size > 0 && !request) {
		LOG_ERR("NULL request with non-zero size");
		return -EINVAL;
	}

	/* Validate sizes upfront */
	if (request_size < method->request_size) {
		LOG_ERR("Request too small: got %zu, need %zu", request_size, method->request_size);
		return -EINVAL;
	}

	if (reply && reply_size < method->reply_size) {
		LOG_ERR("Reply buffer too small: got %zu, need %zu", reply_size,
			method->reply_size);
		return -EINVAL;
	}

	/* Create message context with refcount 2 (sender + receiver) */
	ctx = weave_create_message_context(WEAVE_MSG_REQUEST, 2);
	if (!ctx) {
		return -ENOMEM;
	}

	/* Setup request data */
	ret = weave_setup_data_buffer(ctx, request, request_size);
	if (ret != 0) {
		weave_free_context(ctx);
		return ret;
	}

	/* Setup reply buffer */
	if (reply) {
		ret = weave_setup_reply_buffer(ctx, reply_size);
		if (ret != 0) {
			weave_free_context(ctx);
			return ret;
		}
	}

	/* Setup message fields */
	ctx->message.target.method = method;
	ctx->message.completion = &ctx->completion;
	ctx->message.result = &ctx->result;

	/* Queue the message */
	ret = weave_queue_message(ctx, method->queue, timeout);
	if (ret != 0) {
		return ret;
	}

	/* Wait for completion */
	ret = k_sem_take(&ctx->completion, timeout);
	if (ret != 0) {
		LOG_DBG("Request timed out");
		/* Release our reference - receiver may still be processing */
		weave_context_put(ctx);
		return -ETIMEDOUT;
	}

	/* Copy reply if successful */
	if (ctx->result == 0 && reply && ctx->message.reply_size > 0) {
		memcpy(reply, ctx->message.reply_data, MIN(ctx->message.reply_size, reply_size));
	}

	/* Save result before releasing our reference */
	int result = ctx->result;

	/* Release our reference */
	weave_context_put(ctx);
	return result;
}

/**
 * @brief Call a method (public API)
 */
int weave_call_method(struct weave_method_port *port, const void *request, size_t request_size,
		      void *reply, size_t reply_size, k_timeout_t timeout)
{
	struct weave_method *method;

	if (!port || !port->target_method) {
		LOG_ERR("Invalid method port or unconnected port");
		return -EINVAL;
	}

	method = port->target_method;

	/* Validate port sizes match method sizes */
	if (port->request_size != method->request_size) {
		LOG_ERR("Port request size mismatch: port=%zu, method=%zu", port->request_size,
			method->request_size);
		return -EINVAL;
	}

	if (port->reply_size != method->reply_size) {
		LOG_ERR("Port reply size mismatch: port=%zu, method=%zu", port->reply_size,
			method->reply_size);
		return -EINVAL;
	}

	/* Execute immediately or queue based on method configuration */
	if (weave_method_is_immediate(method)) {
		return weave_execute_method_immediate(method, request, request_size, reply,
						      reply_size);
	} else {
		return weave_execute_method_queued(method, request, request_size, reply, reply_size,
						   timeout);
	}
}

/**
 * @brief Execute a signal handler immediately
 */
static void weave_execute_signal_immediate(struct weave_signal_handler *handler, const void *event)
{
	if (handler && handler->handler) {
		handler->handler(handler->user_data, event);
	}
}

/**
 * @brief Queue a signal to a handler's queue
 */
static int weave_queue_signal(struct weave_signal_handler *handler, const void *event,
			      size_t event_size)
{
	struct weave_message_context *ctx;
	int ret;

	if (!handler || !handler->queue) {
		return -EINVAL;
	}

	/* Create message context with refcount 1 (receiver only) */
	ctx = weave_create_message_context(WEAVE_MSG_SIGNAL, 1);
	if (!ctx) {
		LOG_WRN("Signal pool exhausted");
		return -ENOMEM;
	}

	/* Setup event data */
	ret = weave_setup_data_buffer(ctx, event, event_size);
	if (ret != 0) {
		weave_free_context(ctx);
		return ret;
	}

	/* Setup message fields */
	ctx->message.target.handler = handler;

	/* Queue the message with no wait (signals are fire-and-forget) */
	ret = weave_queue_message(ctx, handler->queue, K_NO_WAIT);

	return ret;
}

/**
 * @brief Emit a signal to all connected handlers
 */
int weave_emit_signal(struct weave_signal *signal, const void *event)
{
	struct weave_signal_handler *handler;
	sys_snode_t *node;
	int queued_count = 0;
	int immediate_count = 0;

	if (!signal) {
		return -EINVAL;
	}

	/* Iterate through all connected handlers */
	SYS_SLIST_FOR_EACH_NODE(&signal->handlers, node) {
		handler = CONTAINER_OF(node, struct weave_signal_handler, node);

		if (handler->queue) {
			/* Queued execution */
			if (weave_queue_signal(handler, event, signal->event_size) == 0) {
				queued_count++;
			}
		} else {
			/* Immediate execution */
			weave_execute_signal_immediate(handler, event);
			immediate_count++;
		}
	}

	LOG_DBG("Signal emitted: %d immediate, %d queued", immediate_count, queued_count);
	return 0;
}

/**
 * @brief Process a single message from a queue
 */
int weave_process_message(struct k_msgq *queue, k_timeout_t timeout)
{
	struct weave_message *msg;
	struct weave_message_context *ctx;
	int ret;

	if (!queue) {
		return -EINVAL;
	}

	/* Get message pointer from queue */
	ret = k_msgq_get(queue, &msg, timeout);
	if (ret != 0) {
		return ret == -EAGAIN ? -EAGAIN : ret;
	}

	/* Get the containing context */
	ctx = CONTAINER_OF(msg, struct weave_message_context, message);

	/* Process based on message type */
	switch (msg->type) {
	case WEAVE_MSG_REQUEST: {
		struct weave_method *method = msg->target.method;
		if (method && method->handler) {
			/* Execute the method */
			*msg->result =
				method->handler(method->user_data, msg->data, msg->reply_data);
		} else if (method && !method->handler) {
			/* Method exists but has no handler */
			*msg->result = -ENOTSUP;
		} else {
			/* No method at all */
			*msg->result = -EINVAL;
		}
		/* Signal completion */
		k_sem_give(msg->completion);
		break;
	}

	case WEAVE_MSG_SIGNAL: {
		struct weave_signal_handler *handler = msg->target.handler;
		if (handler && handler->handler) {
			/* Execute the signal handler */
			handler->handler(handler->user_data, msg->data);
		}
		break;
	}

	default:
		LOG_ERR("Unknown message type: %d", msg->type);
		break;
	}

	/* Release the receiver's reference */
	weave_context_put(ctx);
	return 0;
}

/**
 * @brief Process all pending messages in a queue
 */
int weave_process_all_messages(struct k_msgq *queue)
{
	int count = 0;
	int ret;

	if (!queue) {
		return 0;
	}

	/* Process all available messages */
	while ((ret = weave_process_message(queue, K_NO_WAIT)) == 0) {
		count++;
	}

	return count;
}

/**
 * @brief Initialize the weave subsystem
 */
int weave_init(void)
{
	/* Wire method connections */
	STRUCT_SECTION_FOREACH(weave_method_connection, conn) {
		if (conn->port && conn->method) {
			conn->port->target_method = conn->method;
			LOG_DBG("Connected method port %s to method %s",
				conn->port->name ? conn->port->name : "(unnamed)",
				conn->method->name ? conn->method->name : "(unnamed)");
		}
	}

	/* Wire signal connections */
	STRUCT_SECTION_FOREACH(weave_signal_connection, conn) {
		if (conn->signal && conn->handler) {
			sys_slist_append(&conn->signal->handlers, &conn->handler->node);
			LOG_DBG("Connected signal %s to handler %s",
				conn->signal->name ? conn->signal->name : "(unnamed)",
				conn->handler->name ? conn->handler->name : "(unnamed)");
		}
	}

	LOG_INF("Weave initialized");
	return 0;
}

/* Initialize during POST_KERNEL phase */
SYS_INIT(weave_init, POST_KERNEL, CONFIG_WEAVE_INIT_PRIORITY);