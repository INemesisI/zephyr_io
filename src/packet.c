/*
 * Copyright (c) 2026 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Weave Packet - net_buf routing on top of Weave core
 */

#include <weave/packet.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(weave_packet, CONFIG_WEAVE_LOG_LEVEL);

/* ============================ net_buf Payload Ops ============================ */

/**
 * @brief Reference callback with optional ID filtering
 *
 * Filtering logic:
 * - If ctx->filter != WEAVE_PACKET_ID_ANY (0xFF), only packets with
 *   matching packet_id are accepted.
 * - If ctx->filter == WEAVE_PACKET_ID_ANY or packet has ID_ANY, all packets pass.
 */
static int packet_buf_ref(void *ptr, struct weave_sink *sink)
{
	struct net_buf *buf = (struct net_buf *)ptr;
	struct weave_packet_sink_ctx *ctx = (struct weave_packet_sink_ctx *)sink->user_data;

	/* Check if sink has a filter */
	if (ctx && ctx->filter != WEAVE_PACKET_ID_ANY) {
		struct weave_packet_metadata *meta = weave_packet_get_meta(buf);

		if (meta && meta->packet_id != ctx->filter &&
		    meta->packet_id != WEAVE_PACKET_ID_ANY) {
			LOG_DBG("Filtered: packet_id=%d, filter=%d", meta->packet_id, ctx->filter);
			return -EACCES; /* Filter out this packet */
		}
	}

	struct net_buf *ref = net_buf_ref(buf);

	LOG_DBG("ref: buf=%p, refcount=%d", (void *)buf, buf->ref);
	ARG_UNUSED(ref);
	return 0;
}

static void packet_buf_unref(void *ptr)
{
	struct net_buf *buf = (struct net_buf *)ptr;

	LOG_DBG("unref: buf=%p, refcount=%d", ptr, buf->ref);
	net_buf_unref(buf);
}

const struct weave_payload_ops weave_packet_ops = {
	.ref = packet_buf_ref,
	.unref = packet_buf_unref,
};

/* ============================ Buffer Allocation ============================ */

/**
 * @brief Initialize packet metadata
 */
static void packet_meta_init(struct weave_packet_metadata *meta, struct weave_packet_pool *pool,
			     uint8_t packet_id)
{
	meta->packet_id = packet_id;
	meta->client_id = 0;
	meta->counter = (uint16_t)atomic_inc(&pool->counter);
#ifdef CONFIG_WEAVE_PACKET_TIMESTAMP_HIRES
	meta->cycles = k_cycle_get_64();
#else
	meta->ticks = k_uptime_ticks();
#endif
}

struct net_buf *weave_packet_alloc(struct weave_packet_pool *pool, k_timeout_t timeout)
{
	return weave_packet_alloc_with_id(pool, WEAVE_PACKET_ID_ANY, timeout);
}

struct net_buf *weave_packet_alloc_with_id(struct weave_packet_pool *pool, uint8_t packet_id,
					   k_timeout_t timeout)
{
	if (!pool || !pool->pool) {
		return NULL;
	}

	struct net_buf *buf = net_buf_alloc(pool->pool, timeout);

	if (!buf) {
		LOG_DBG("Alloc failed (pool exhausted)");
		return NULL;
	}

	/* Access user_data directly - weave_packet_get_meta() can't be used here
	 * because it validates the timestamp, which isn't set yet */
	if (buf->user_data_size >= WEAVE_PACKET_METADATA_SIZE) {
		struct weave_packet_metadata *meta =
			(struct weave_packet_metadata *)net_buf_user_data(buf);
		packet_meta_init(meta, pool, packet_id);
		LOG_DBG("Alloc: id=%d, counter=%d", packet_id, meta->counter);
	} else {
	}

	return buf;
}
