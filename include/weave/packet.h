/*
 * Copyright (c) 2026 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Weave Packet API
 *
 * Zero-copy net_buf packet routing built on Weave core.
 *
 * Provides all functionality from old flow module:
 * - Buffer pools with auto-incrementing counters
 * - Packet metadata (ID, flags, counter, timestamp)
 * - ID-based filtering in ref callback
 * - Consuming and non-consuming send variants
 *
 * Uses weave primitives where possible - packet adds only net_buf specifics.
 */

#ifndef ZEPHYR_INCLUDE_WEAVE_PACKET_H_
#define ZEPHYR_INCLUDE_WEAVE_PACKET_H_

#include <weave/core.h>
#include <zephyr/net/buf.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup weave_packet_apis Weave Packet APIs
 * @ingroup os_services
 * @{
 */

/* ============================ Constants ============================ */

/** @brief Special packet ID that matches any packet */
#define WEAVE_PACKET_ID_ANY 0xFF

/** @brief No filter - accept all packets */
#define WV_NO_FILTER WEAVE_PACKET_ID_ANY

/* ============================ Metadata ============================ */

/**
 * @brief Packet metadata stored in net_buf user_data
 *
 * @warning Weave packet takes ownership of net_buf user_data.
 */
struct weave_packet_metadata {
	uint8_t packet_id; /**< Packet ID for filtering (255 = ANY) */
	uint8_t client_id; /**< Client ID for reply routing */
	uint16_t counter;  /**< Auto-incrementing sequence counter */
#ifdef CONFIG_WEAVE_PACKET_TIMESTAMP_HIRES
	uint64_t cycles; /**< CPU cycles (k_cycle_get_64) */
#else
	uint32_t ticks; /**< System ticks (k_uptime_ticks) */
#endif
} __packed;

#define WEAVE_PACKET_METADATA_SIZE sizeof(struct weave_packet_metadata)

/* ============================ Buffer Pool ============================ */

/**
 * @brief Packet buffer pool with auto-incrementing counter
 */
struct weave_packet_pool {
	struct net_buf_pool *pool; /**< Underlying net_buf pool */
	atomic_t counter;          /**< Atomic counter for sequence numbers */
};

/**
 * @brief Define a packet buffer pool
 *
 * @param _name Pool variable name
 * @param _count Number of buffers
 * @param _size Buffer data size (bytes)
 * @param _destroy Destructor callback or NULL
 */
#define WEAVE_PACKET_POOL_DEFINE(_name, _count, _size, _destroy)                                   \
	NET_BUF_POOL_DEFINE(_name##_net_buf_pool, _count, _size, WEAVE_PACKET_METADATA_SIZE,       \
			    _destroy);                                                             \
	static struct weave_packet_pool _name = {                                                  \
		.pool = &_name##_net_buf_pool,                                                     \
		.counter = ATOMIC_INIT(0),                                                         \
	}

/* ============================ Sink Context ============================ */

/**
 * @brief Packet sink context (stored in sink->user_data)
 *
 * Used internally by WEAVE_PACKET_SINK_DEFINE to store filter and user data.
 */
struct weave_packet_sink_ctx {
	uint8_t filter;  /**< Packet ID filter (WV_NO_FILTER = accept all) */
	void *user_data; /**< User's actual user_data */
};

/* ============================ Payload Ops ============================ */

/**
 * @brief Payload ops for net_buf with optional ID filtering
 *
 * Filtering happens in ref callback using weave_packet_sink_ctx.
 * If ctx->filter != WEAVE_PACKET_ID_ANY, only matching packets pass.
 */
extern const struct weave_payload_ops weave_packet_ops;

/* ============================ Handler Type ============================ */

/**
 * @brief Packet handler function type
 *
 * Type-safe handler that receives net_buf directly.
 *
 * @warning The buffer is borrowed - do NOT call net_buf_unref().
 * The framework manages the reference and releases it after the handler returns.
 * If you need to keep the buffer (e.g., chain it), call net_buf_ref() first.
 *
 * @param buf The packet buffer (borrowed reference)
 * @param user_data User data from sink definition
 */
typedef void (*weave_packet_handler_t)(struct net_buf *buf, void *user_data);

/* ============================ Source Macros ============================ */

/**
 * @brief Define a packet source
 */
#define WEAVE_PACKET_SOURCE_DEFINE(_name) WEAVE_SOURCE_DEFINE(_name, &weave_packet_ops)

/**
 * @brief Declare extern packet source
 */
#define WEAVE_PACKET_SOURCE_DECLARE(_name) extern struct weave_source _name

/* ============================ Sink Macros ============================ */

/**
 * @brief Define a packet sink
 *
 * Unified macro for all sink configurations.
 * Handler signature: void handler(struct net_buf *buf, void *user_data)
 *
 * @note For best performance, declare handlers as ``static inline`` in the same
 *       file. This allows the compiler to inline the handler into the wrapper,
 *       reducing call overhead.
 *
 * @param _name Sink variable name
 * @param _handler Handler function (recommend static inline)
 * @param _queue Message queue (WV_IMMEDIATE for immediate mode, or &queue for queued)
 * @param _filter Packet ID filter (WV_NO_FILTER for all, or specific ID)
 * @param _user_data User data pointer (NULL if unused)
 */
#define WEAVE_PACKET_SINK_DEFINE(_name, _handler, _queue, _filter, _user_data)                     \
	static struct weave_packet_sink_ctx _name##_ctx = {.filter = (_filter),                    \
							   .user_data = (_user_data)};             \
	static void _name##_wrapper(void *ptr, void *ctx)                                          \
	{                                                                                          \
		weave_packet_handler_t handler_fn = (weave_packet_handler_t)(_handler);            \
		struct weave_packet_sink_ctx *sink_ctx = (struct weave_packet_sink_ctx *)ctx;      \
		handler_fn((struct net_buf *)ptr, sink_ctx->user_data);                            \
	}                                                                                          \
	struct weave_sink _name = WEAVE_SINK_INITIALIZER(_name##_wrapper, (_queue), &_name##_ctx)

/**
 * @brief Declare extern packet sink
 */
#define WEAVE_PACKET_SINK_DECLARE(_name) extern struct weave_sink _name

/* ============================ Buffer Allocation ============================ */

/**
 * @brief Allocate a packet buffer
 *
 * Allocates a buffer from the pool and initializes metadata:
 * - packet_id = WEAVE_PACKET_ID_ANY (0xFF)
 * - client_id = 0
 * - counter = auto-incremented from pool
 * - timestamp = current time
 *
 * @param pool Packet pool to allocate from
 * @param timeout Allocation timeout
 * @return Allocated buffer, or NULL on timeout/failure
 */
struct net_buf *weave_packet_alloc(struct weave_packet_pool *pool, k_timeout_t timeout);

/**
 * @brief Allocate a packet buffer with specific ID
 *
 * Same as weave_packet_alloc() but sets the specified packet_id.
 * Use this when sending to filtered sinks.
 *
 * @param pool Packet pool to allocate from
 * @param packet_id Packet ID for routing/filtering
 * @param timeout Allocation timeout
 * @return Allocated buffer, or NULL on timeout/failure
 */
struct net_buf *weave_packet_alloc_with_id(struct weave_packet_pool *pool, uint8_t packet_id,
					   k_timeout_t timeout);

/* ============================ Send Functions ============================ */

/**
 * @brief Send a packet (consuming reference)
 *
 * Sends the buffer to all connected sinks, then releases the caller's
 * reference. Do not use the buffer after calling this function.
 *
 * This is the recommended send variant - more efficient than send_ref.
 *
 * @param source Source to send from
 * @param buf Buffer to send (caller's reference consumed)
 * @param timeout Maximum time for all deliveries
 * @return Number of successful deliveries, or negative errno
 */
static inline int weave_packet_send(struct weave_source *source, struct net_buf *buf,
				    k_timeout_t timeout)
{
	int ret = weave_source_emit(source, buf, timeout);

	net_buf_unref(buf);
	return ret;
}

/**
 * @brief Send a packet (preserving reference)
 *
 * Sends the buffer to all connected sinks, preserving the caller's
 * reference. Caller must call net_buf_unref() when done with buffer.
 *
 * @param source Source to send from
 * @param buf Buffer to send (caller retains reference)
 * @param timeout Maximum time for all deliveries
 * @return Number of successful deliveries, or negative errno
 */
static inline int weave_packet_send_ref(struct weave_source *source, struct net_buf *buf,
					k_timeout_t timeout)
{
	return weave_source_emit(source, buf, timeout);
}

/* ============================ Metadata Accessors ============================ */

/**
 * @brief Check if metadata is all zeros (uninitialized)
 */
static inline bool weave_packet_meta_is_zero(const struct weave_packet_metadata *meta)
{
	const uint8_t *bytes = (const uint8_t *)meta;

	/* First byte must be 0, and all remaining bytes must equal the first */
	return bytes[0] == 0 && memcmp(bytes, bytes + 1, sizeof(*meta) - 1) == 0;
}

/**
 * @brief Get packet metadata from buffer
 *
 * Returns NULL if:
 * - buf is NULL
 * - user_data_size is too small for metadata
 * - metadata is all zeros (uninitialized)
 *
 * This ensures only properly allocated buffers (via weave_packet_alloc)
 * are accepted, detecting buffers allocated directly via net_buf_alloc.
 */
static inline struct weave_packet_metadata *weave_packet_get_meta(struct net_buf *buf)
{
	if (!buf || buf->user_data_size < WEAVE_PACKET_METADATA_SIZE) {
		return NULL;
	}
	struct weave_packet_metadata *meta = (struct weave_packet_metadata *)net_buf_user_data(buf);
	if (weave_packet_meta_is_zero(meta)) {
		return NULL; /* Uninitialized buffer */
	}
	return meta;
}

/** @brief Set packet ID */
static inline int weave_packet_set_id(struct net_buf *buf, uint8_t packet_id)
{
	struct weave_packet_metadata *meta = weave_packet_get_meta(buf);
	if (!meta) {
		return -EINVAL;
	}
	meta->packet_id = packet_id;
	return 0;
}

/** @brief Get packet ID */
static inline int weave_packet_get_id(struct net_buf *buf, uint8_t *packet_id)
{
	struct weave_packet_metadata *meta = weave_packet_get_meta(buf);
	if (!meta || !packet_id) {
		return -EINVAL;
	}
	*packet_id = meta->packet_id;
	return 0;
}

/** @brief Set client ID */
static inline int weave_packet_set_client_id(struct net_buf *buf, uint8_t client_id)
{
	struct weave_packet_metadata *meta = weave_packet_get_meta(buf);
	if (!meta) {
		return -EINVAL;
	}
	meta->client_id = client_id;
	return 0;
}

/** @brief Get client ID */
static inline int weave_packet_get_client_id(struct net_buf *buf, uint8_t *client_id)
{
	struct weave_packet_metadata *meta = weave_packet_get_meta(buf);
	if (!meta || !client_id) {
		return -EINVAL;
	}
	*client_id = meta->client_id;
	return 0;
}

/** @brief Set counter */
static inline int weave_packet_set_counter(struct net_buf *buf, uint16_t counter)
{
	struct weave_packet_metadata *meta = weave_packet_get_meta(buf);
	if (!meta) {
		return -EINVAL;
	}
	meta->counter = counter;
	return 0;
}

/** @brief Get counter */
static inline int weave_packet_get_counter(struct net_buf *buf, uint16_t *counter)
{
	struct weave_packet_metadata *meta = weave_packet_get_meta(buf);
	if (!meta || !counter) {
		return -EINVAL;
	}
	*counter = meta->counter;
	return 0;
}

/** @brief Update timestamp to current time */
static inline int weave_packet_update_timestamp(struct net_buf *buf)
{
	struct weave_packet_metadata *meta = weave_packet_get_meta(buf);
	if (!meta) {
		return -EINVAL;
	}
#ifdef CONFIG_WEAVE_PACKET_TIMESTAMP_HIRES
	meta->cycles = k_cycle_get_64();
#else
	meta->ticks = k_uptime_ticks();
#endif
	return 0;
}

#ifndef CONFIG_WEAVE_PACKET_TIMESTAMP_HIRES
/** @brief Get timestamp in ticks */
static inline int weave_packet_get_timestamp_ticks(struct net_buf *buf, uint32_t *ticks)
{
	struct weave_packet_metadata *meta = weave_packet_get_meta(buf);
	if (!meta || !ticks) {
		return -EINVAL;
	}
	*ticks = meta->ticks;
	return 0;
}

/** @brief Set timestamp in ticks */
static inline int weave_packet_set_timestamp_ticks(struct net_buf *buf, uint32_t ticks)
{
	struct weave_packet_metadata *meta = weave_packet_get_meta(buf);
	if (!meta) {
		return -EINVAL;
	}
	meta->ticks = ticks;
	return 0;
}
#else
/** @brief Get timestamp in cycles */
static inline int weave_packet_get_timestamp_cycles(struct net_buf *buf, uint64_t *cycles)
{
	struct weave_packet_metadata *meta = weave_packet_get_meta(buf);
	if (!meta || !cycles) {
		return -EINVAL;
	}
	*cycles = meta->cycles;
	return 0;
}

/** @brief Set timestamp in cycles */
static inline int weave_packet_set_timestamp_cycles(struct net_buf *buf, uint64_t cycles)
{
	struct weave_packet_metadata *meta = weave_packet_get_meta(buf);
	if (!meta) {
		return -EINVAL;
	}
	meta->cycles = cycles;
	return 0;
}
#endif

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_WEAVE_PACKET_H_ */
