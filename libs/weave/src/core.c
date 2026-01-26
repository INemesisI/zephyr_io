/*
 * Copyright (c) 2026 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <weave/core.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(weave_core, CONFIG_WEAVE_LOG_LEVEL);

/* ============================ Internal Helpers ============================ */

/**
 * @brief Deliver a message to a single sink with lifecycle management
 *
 * Handles ref (with optional filtering) and unref for both immediate and queued modes.
 * For immediate: calls handler then unref.
 * For queued: stores ops in event, unref happens later in weave_process_messages.
 *
 * @return 0 on success, -EACCES if filtered, -ENOBUFS if queue full
 */
static int sink_deliver(struct weave_sink *sink, void *ptr, const struct weave_payload_ops *ops,
			k_timeout_t timeout)
{
	if (!sink || !sink->handler || !ptr) {
		return -EINVAL;
	}

	/* Take reference (and optionally filter) before delivery */
	if (ops && ops->ref) {
		int ret = ops->ref(ptr, sink);
		if (ret < 0) {
			return ret; /* Filtered out or error */
		}
	}

	if (sink->queue == NULL) {
		/* Immediate mode - call handler directly */
		sink->handler(ptr, sink->user_data);
		/* Release reference after immediate handling */
		if (ops && ops->unref) {
			ops->unref(ptr);
		}
		return 0;
	}

	/* Queued mode - put event in queue with ops for later unref */
	struct weave_event event = {
		.sink = sink,
		.ptr = ptr,
		.ops = ops,
	};

	int ret = k_msgq_put(sink->queue, &event, timeout);
	if (ret != 0) {
		/* Release reference on failure */
		if (ops && ops->unref) {
			ops->unref(ptr);
		}
		LOG_DBG("Queue full, dropped message");
		return -ENOBUFS;
	}

	return 0;
}

/* ============================ Public API ============================ */

int weave_source_emit(struct weave_source *source, void *ptr, k_timeout_t timeout)
{
	if (!source || !ptr) {
		return -EINVAL;
	}

	k_timepoint_t deadline = sys_timepoint_calc(timeout);
	int delivered = 0;
	struct weave_connection *conn;

	LOG_DBG("emit: source=%p, sinks empty=%d", source, sys_slist_is_empty(&source->sinks));

	SYS_SLIST_FOR_EACH_CONTAINER(&source->sinks, conn, node) {
		/* Without ops, we can only deliver to one sink (no ref counting) */
		if (!source->ops && delivered > 0) {
			return -EINVAL;
		}

		k_timeout_t remaining = sys_timepoint_timeout(deadline);
		int ret = sink_deliver(conn->sink, ptr, source->ops, remaining);
		if (ret == 0) {
			delivered++;
		}
	}

	LOG_DBG("Emitted to %d sinks", delivered);

	return delivered;
}

int weave_sink_send(struct weave_sink *sink, void *ptr, const struct weave_payload_ops *ops,
		    k_timeout_t timeout)
{
	if (!sink) {
		return -EINVAL;
	}

	return sink_deliver(sink, ptr, ops, timeout);
}

int weave_process_messages(struct k_msgq *queue, k_timeout_t timeout)
{
	if (!queue) {
		return -EINVAL;
	}

	int processed = 0;
	struct weave_event event;
	k_timepoint_t deadline = sys_timepoint_calc(timeout);

	/* Wait for first message with caller's timeout, then drain remaining */
	k_timeout_t remaining = sys_timepoint_timeout(deadline);
	while (k_msgq_get(queue, &event, remaining) == 0) {
		struct weave_sink *sink = event.sink;

		if (sink && sink->handler) {
			sink->handler(event.ptr, sink->user_data);

			/* Use event's ops for unref (same ops that did ref) */
			if (event.ops && event.ops->unref) {
				event.ops->unref(event.ptr);
			}
			processed++;
		}

		/* Use remaining time for subsequent messages */
		remaining = sys_timepoint_timeout(deadline);
	}

	return processed;
}

/* ============================ Initialization ============================ */

/**
 * @brief Initialize weave connections at boot
 *
 * Walks all static connections and wires them to sources.
 */
static int weave_init(void)
{
	int count = 0;

	STRUCT_SECTION_FOREACH(weave_connection, conn) {
		if (!conn->source || !conn->sink) {
			continue;
		}

		sys_slist_append(&conn->source->sinks, &conn->node);
		LOG_DBG("Wired: source=%p -> sink=%p", conn->source, conn->sink);
		count++;
	}

	LOG_DBG("Weave initialized with %d connections", count);
	return 0;
}

SYS_INIT(weave_init, POST_KERNEL, CONFIG_WEAVE_INIT_PRIORITY);
