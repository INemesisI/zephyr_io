/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/packet_io/packet_io.h>
#include <zephyr/net/buf.h>
#include <zephyr/init.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(packet_io, CONFIG_PACKET_IO_LOG_LEVEL);

/* Declare the iterable section boundaries */
STRUCT_SECTION_START_EXTERN(packet_connection);
STRUCT_SECTION_END_EXTERN(packet_connection);

int packet_source_send(struct packet_source *src, struct net_buf *buf, k_timeout_t timeout)
{
	k_spinlock_key_t key;
	struct packet_connection *conn;
	int delivered = 0;
	k_timepoint_t end = sys_timepoint_calc(timeout);

	/* Handle NULL parameters gracefully */
	if (src == NULL || buf == NULL) {
		return 0;
	}

#ifdef CONFIG_PACKET_IO_STATS
	atomic_inc(&src->msg_count);
#endif

	/* Fast path: no sinks */
	if (sys_dlist_is_empty(&src->sinks)) {
		LOG_DBG("Source %p has no sinks", src);
		return 0;
	}

	key = k_spin_lock(&src->lock);

	/* Send to each connected sink */
	SYS_DLIST_FOR_EACH_CONTAINER(&src->sinks, conn, node) {
		struct net_buf *buf_ptr = buf;
		k_timeout_t remaining = sys_timepoint_timeout(end);

		/* If timeout expired, try remaining sinks with K_NO_WAIT */
		if (K_TIMEOUT_EQ(remaining, K_NO_WAIT) && !K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
			remaining = K_NO_WAIT;
		}

		/* Try sending with remaining timeout */
		if (k_msgq_put(&conn->sink->msgq, &buf_ptr, remaining) == 0) {
			/* Success - increment reference for this sink */
			buf_ptr = net_buf_ref(buf);
			delivered++;
#ifdef CONFIG_PACKET_IO_STATS
			atomic_inc(&conn->sink->received_count);
#endif
			LOG_DBG("Delivered to sink %p", conn->sink);
		} else if (conn->sink->drop_on_full) {
			/* Drop silently */
#ifdef CONFIG_PACKET_IO_STATS
			atomic_inc(&conn->sink->dropped_count);
#endif
			LOG_DBG("Dropped for sink %p (queue full)", conn->sink);
		} else {
			LOG_WRN("Failed to deliver to sink %p (queue full, no drop)",
				conn->sink);
		}
	}

	k_spin_unlock(&src->lock, key);

#ifdef CONFIG_PACKET_IO_STATS
	atomic_add(&src->delivered_count, delivered);
#endif

	LOG_DBG("Source %p delivered to %d sinks", src, delivered);
	return delivered;
}

int packet_source_send_consume(struct packet_source *src, struct net_buf *buf, k_timeout_t timeout)
{
	int ret;

	/* Call the non-consuming version */
	ret = packet_source_send(src, buf, timeout);

	/* Always consume the caller's reference */
	if (buf != NULL) {
		net_buf_unref(buf);
	}

	return ret;
}

#ifdef CONFIG_PACKET_IO_STATS
void packet_source_get_stats(struct packet_source *src,
			      uint32_t *msg_count,
			      uint32_t *delivered_count)
{
	__ASSERT_NO_MSG(src != NULL);

	if (msg_count) {
		*msg_count = atomic_get(&src->msg_count);
	}
	if (delivered_count) {
		*delivered_count = atomic_get(&src->delivered_count);
	}
}

void packet_sink_get_stats(struct packet_sink *sink,
			   uint32_t *received_count,
			   uint32_t *dropped_count)
{
	__ASSERT_NO_MSG(sink != NULL);

	if (received_count) {
		*received_count = atomic_get(&sink->received_count);
	}
	if (dropped_count) {
		*dropped_count = atomic_get(&sink->dropped_count);
	}
}

void packet_source_reset_stats(struct packet_source *src)
{
	__ASSERT_NO_MSG(src != NULL);

	atomic_clear(&src->msg_count);
	atomic_clear(&src->delivered_count);
}

void packet_sink_reset_stats(struct packet_sink *sink)
{
	__ASSERT_NO_MSG(sink != NULL);

	atomic_clear(&sink->received_count);
	atomic_clear(&sink->dropped_count);
}
#endif /* CONFIG_PACKET_IO_STATS */

static int packet_io_init(void)
{
	int connection_count = 0;

	LOG_INF("Initializing packet I/O");

	/* Walk all connections and wire them */
	STRUCT_SECTION_FOREACH(packet_connection, conn) {
		/* Skip dummy connection */
		if (conn->source == NULL || conn->sink == NULL) {
			continue;
		}
		
		/* Add connection to source's list */
		sys_dlist_append(&conn->source->sinks, &conn->node);
		connection_count++;

		LOG_DBG("Connected source %p to sink %p",
			conn->source, conn->sink);
	}

	LOG_INF("Packet I/O initialized with %d connections", connection_count);
	return 0;
}

SYS_INIT(packet_io_init, POST_KERNEL, CONFIG_PACKET_IO_PRIORITY);