/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/buf.h>
#include <zephyr/sys/__assert.h>
#include <zephyr_io/flow/flow.h>

LOG_MODULE_REGISTER(flow, CONFIG_FLOW_LOG_LEVEL);

/* ============================ Buffer Allocation ============================ */

struct net_buf *flow_buf_alloc(struct flow_buf_pool *pool, k_timeout_t timeout)
{
	if (!pool) {
		return NULL;
	}

	struct net_buf *buf = net_buf_alloc(pool->pool, timeout);

	if (!buf) {
		return NULL;
	}

	/* Initialize metadata */
	struct flow_buf_metadata *meta = (struct flow_buf_metadata *)net_buf_user_data(buf);

	meta->packet_id = FLOW_PACKET_ID_ANY;
	meta->flags = 0;
	meta->counter = (uint16_t)atomic_inc(&pool->counter); /* Auto-increment */
#ifdef CONFIG_FLOW_BUF_TIMESTAMP_HIRES
	meta->cycles = k_cycle_get_64();
#else
	meta->ticks = k_uptime_ticks();
#endif

	return buf;
}

struct net_buf *flow_buf_alloc_with_id(struct flow_buf_pool *pool, uint8_t packet_id,
				       k_timeout_t timeout)
{
	struct net_buf *buf = flow_buf_alloc(pool, timeout);

	if (buf) {
		flow_buf_set_id(buf, packet_id);
	}
	return buf;
}

/* ============================ Flow Event Handling ============================ */

/* Helper function to execute handler and manage buffer lifecycle */
int flow_event_handler(struct flow_sink *sink, struct net_buf *buf)
{
	/* Validate inputs */
	if (!sink || !buf || !sink->handler) {
		if (buf) {
			net_buf_unref(buf);
		}
		return -EINVAL;
	}

	/* Validate buffer reference count */
	if (buf->ref == 0) {
		return -EINVAL;
	}

	/* Execute the handler */
	sink->handler(sink, buf);

#ifdef CONFIG_FLOW_STATS
	/* Update sink statistics */
	atomic_inc(&sink->handled_count);
#endif

	/* Check that the buffer still has a reference before unreffing */
	if (buf->ref == 0) {
		/* This should never happen - handler modified ref count */
		LOG_ERR("Buffer ref count is zero after handler execution!");
		return -EFAULT;
	}

	net_buf_unref(buf);

	return 0;
}

int flow_sink_deliver_ref(struct flow_sink *sink, struct net_buf *buf_ref, k_timeout_t timeout)
{
	int ret = 0;

	if (!sink || !buf_ref || !sink->handler) {
		return -EINVAL;
	}

	/* Check packet ID filter */
	if (sink->accept_id != FLOW_PACKET_ID_ANY) {
		uint8_t packet_id = FLOW_PACKET_ID_ANY;

		flow_buf_get_id(buf_ref, &packet_id);
		/* Skip if packet ID doesn't match and packet isn't broadcast */
		if (packet_id != sink->accept_id) {
			return -ENOTSUP;
		}
	}

	struct net_buf *ref = net_buf_ref(buf_ref); /* Reference for the sink */

	switch (sink->mode) {
	case SINK_MODE_IMMEDIATE:
		ret = flow_event_handler(sink, ref);
		break;
	case SINK_MODE_QUEUED:
		/* Queue packet event for later processing */
		if (!sink->msgq) {
			net_buf_unref(ref);
			return -ENOSYS;
		}
		struct flow_event event = {
			.sink = sink,
			.buf = ref,
		};

		ret = k_msgq_put(sink->msgq, &event, timeout);
		if (ret != 0) {
			/* Failed to queue - clean up the buffer */
			net_buf_unref(ref);
#ifdef CONFIG_FLOW_STATS
			atomic_inc(&sink->dropped_count);
#endif
			/* Return more specific error code */
			if (ret == -ENOMSG) {
				return -ENOBUFS;
			}
		}
		break;
	default:
		/* Unsupported mode */
		net_buf_unref(ref);
		return -ENOTSUP;
	}
	return ret;
}

int flow_sink_deliver(struct flow_sink *sink, struct net_buf *buf, k_timeout_t timeout)
{
	if (!buf) {
		return flow_sink_deliver_ref(sink, NULL, timeout);
	}

	int ret = flow_sink_deliver_ref(sink, buf, timeout);
	net_buf_unref(buf);
	return ret;
}

static int _flow_source_send(struct flow_source *src, struct net_buf *buf, k_timeout_t timeout)
{
	k_spinlock_key_t key;
	struct flow_connection *conn;
	int delivered = 0;
	k_timepoint_t end = sys_timepoint_calc(timeout);

#ifdef CONFIG_FLOW_STATS
	atomic_inc(&src->send_count);
#endif

	key = k_spin_lock(&src->lock);

	/* Send to each connected sink */
	SYS_SLIST_FOR_EACH_CONTAINER(&src->connections, conn, node) {
		k_timeout_t remaining = sys_timepoint_timeout(end);
		struct flow_sink *sink = conn->sink;

		/* Skip if sink is invalid */
		if (!sink) {
			continue;
		}

		/* Try delivering to this sink */
		if (flow_sink_deliver_ref(sink, buf, remaining) == 0) {
			delivered++;
		}
	}

	k_spin_unlock(&src->lock, key);

#ifdef CONFIG_FLOW_STATS
	atomic_add(&src->delivery_count, delivered);
#endif

	return delivered;
}
int flow_source_send_ref(struct flow_source *src, struct net_buf *buf_ref, k_timeout_t timeout)
{
	/* Validate parameters */
	if (!src || !buf_ref) {
		return -EINVAL;
	}

	return _flow_source_send(src, buf_ref, timeout);
}

int flow_source_send(struct flow_source *src, struct net_buf *buf, k_timeout_t timeout)
{
	int ret;

	/* Validate parameters */
	if (!src || !buf) {
		return -EINVAL;
	}

	/* Validate buffer has at least one reference */
	if (buf->ref == 0) {
		LOG_ERR("Buffer has zero reference count, cannot consume");
		return -EINVAL;
	}

	/* Warn about potential leaks from high reference counts */
	if (buf->ref > 10) {
		LOG_WRN("Buffer has high reference count (%d) - possible leak", buf->ref);
	}

	/* Call the non-consuming version */
	ret = flow_source_send_ref(src, buf, timeout);

	/* Consume the caller's reference */
	net_buf_unref(buf);

	return ret;
}

#ifdef CONFIG_FLOW_STATS
void flow_source_get_stats(struct flow_source *src, uint32_t *send_count, uint32_t *delivery_count)
{
	if (!src) {
		return;
	}

	if (send_count) {
		*send_count = atomic_get(&src->send_count);
	}
	if (delivery_count) {
		*delivery_count = atomic_get(&src->delivery_count);
	}
}

void flow_sink_get_stats(struct flow_sink *sink, uint32_t *handled_count, uint32_t *dropped_count)
{
	if (!sink) {
		return;
	}

	if (handled_count) {
		*handled_count = atomic_get(&sink->handled_count);
	}
	if (dropped_count) {
		*dropped_count = atomic_get(&sink->dropped_count);
	}
}

void flow_source_reset_stats(struct flow_source *src)
{
	if (!src) {
		return;
	}

	atomic_clear(&src->send_count);
	atomic_clear(&src->delivery_count);
}

void flow_sink_reset_stats(struct flow_sink *sink)
{
	if (!sink) {
		return;
	}

	atomic_clear(&sink->handled_count);
	atomic_clear(&sink->dropped_count);
}
#endif /* CONFIG_FLOW_STATS */

/* Process a single event from a packet event queue */
int flow_event_process(struct k_msgq *queue, k_timeout_t timeout)
{
	struct flow_event event;
	int ret;

	if (!queue || !queue) {
		return -EINVAL;
	}

	ret = k_msgq_get(queue, &event, timeout);
	if (ret != 0) {
		return -EAGAIN; /* No event available */
	}

	/* Execute the handler using the common helper */
	return flow_event_handler(event.sink, event.buf);
}

static int flow_init(void)
{
	int connection_count = 0;

	LOG_DBG("Initializing Flow");

	/* Walk all connections and wire them */
	STRUCT_SECTION_FOREACH(flow_connection, conn) {
		/* Validate connection */
		if (!conn->source || !conn->sink) {
			LOG_ERR("Invalid static connection: source=%p, sink=%p", conn->source,
				conn->sink);
			continue;
		}

		/* For static connections, we don't check if node is already linked
		 * because static initialization may not clear the next pointer */

		/* Add connection to source's list */
		sys_slist_append(&conn->source->connections, &conn->node);
		connection_count++;

		LOG_DBG("Connected source %p to sink %p", conn->source, conn->sink);
	}

	return 0;
}

SYS_INIT(flow_init, POST_KERNEL, CONFIG_FLOW_INIT_PRIORITY);
