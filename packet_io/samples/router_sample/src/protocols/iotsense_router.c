/*
 * Copyright (c) 2024 Zephyr Project
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * IoTSense Protocol Router Implementation
 */

#include "iotsense_router.h"
#include "framework/packet_router.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/buf.h>

LOG_MODULE_REGISTER(iotsense_router, LOG_LEVEL_INF);

/* Protocol constants */
#define IOTSENSE_VERSION     0x01 /**< Protocol version 1.0 */
#define IOTSENSE_HEADER_SIZE sizeof(struct iotsense_header)
#define IOTSENSE_MIN_PACKET  IOTSENSE_HEADER_SIZE

/*
 * Internal Helper Functions
 */

/**
 * @brief Check if packet ID is valid
 *
 * @param packet_id Packet ID to validate
 * @return true if valid, false if reserved/invalid
 */
static inline bool is_valid_packet_id(uint8_t packet_id)
{
	return (packet_id != PKT_ID_INVALID);
}

/*
 * Protocol Parser Functions
 */

/**
 * @brief Extract packet ID from IoTSense header
 *
 * @param buf Buffer containing packet with header
 * @return Packet ID, or PKT_ID_INVALID if error
 */
static uint16_t iotsense_get_packet_id(const struct net_buf *buf)
{
	if (buf->len < IOTSENSE_HEADER_SIZE) {
		return PKT_ID_INVALID;
	}

	const struct iotsense_header *hdr = (const struct iotsense_header *)buf->data;

	/* Validate version */
	if (hdr->version != IOTSENSE_VERSION) {
		return PKT_ID_INVALID;
	}

	return hdr->packet_id;
}

/**
 * @brief Validate IoTSense packet header
 *
 * @param buf Buffer containing complete packet
 * @return 0 if valid, negative errno if invalid
 */
static int iotsense_validate_header(const struct net_buf *buf)
{
	const struct iotsense_header *hdr;
	uint16_t total_len;

	if (buf->len < IOTSENSE_HEADER_SIZE) {
		return -EINVAL;
	}

	hdr = (const struct iotsense_header *)buf->data;

	/* Check version */
	if (hdr->version != IOTSENSE_VERSION) {
		return -EPROTONOSUPPORT;
	}

	/* Validate packet length */
	total_len = IOTSENSE_HEADER_SIZE + hdr->payload_len;
	if (buf->len != total_len) {
		return -EMSGSIZE;
	}

	/* Validate packet ID */
	if (!is_valid_packet_id(hdr->packet_id)) {
		return -EINVAL;
	}

	return 0;
}

/**
 * @brief Add IoTSense header by creating and chaining header buffer
 *
 * Creates a new buffer containing the header and chains the payload
 * buffer to it. This achieves zero-copy operation.
 *
 * @param payload_buf Payload buffer to add header to
 * @param packet_id Packet ID for header
 * @param header_pool Buffer pool for header allocation
 * @param context Optional context (unused)
 * @return Pointer to complete packet (header + payload), or NULL on error
 */
static struct net_buf *iotsense_add_header(struct net_buf *payload_buf, uint16_t packet_id,
					   struct net_buf_pool *header_pool, void *context)
{
	struct net_buf *header_buf;
	struct iotsense_header *hdr;

	ARG_UNUSED(context);

	/* Allocate buffer for header */
	header_buf = net_buf_alloc(header_pool, K_NO_WAIT);
	if (!header_buf) {
		return NULL;
	}

	/* Add header to buffer */
	hdr = (struct iotsense_header *)net_buf_add(header_buf, IOTSENSE_HEADER_SIZE);

	/* Fill header fields */
	hdr->version = IOTSENSE_VERSION;
	hdr->packet_id = packet_id;
	hdr->payload_len = payload_buf ? payload_buf->len : 0;

	/* Chain payload after header (zero-copy) */
	if (payload_buf) {
		net_buf_frag_add(header_buf, payload_buf);
	}

	return header_buf;
}

/*
 * Buffer Pool for Header Allocation
 */

/* Buffer pool: 32 buffers, 64 bytes each, 4 byte alignment */
NET_BUF_POOL_DEFINE(iotsense_header_pool, 32, 64, 4, NULL);

*Protocol Handlers * /

	static void iotsense_network_inbound_handler(struct packet_sink *sink, struct net_buf *buf);
static void iotsense_network_outbound_handler(struct packet_router *router, struct net_buf *buf,
					      uint16_t packet_id);

/*
 * Router Instance Definition
 */

ROUTER_DEFINE(iotsense_router, iotsense_network_inbound_handler, iotsense_network_outbound_handler);

/**
 * @brief Handle inbound network packets
 *
 * Processes packets received from the network:
 * 1. Validates the IoTSense header
 * 2. Extracts the packet ID
 * 3. Finds the appropriate route
 * 4. Strips the header
 * 5. Delivers the payload to the application sink
 *
 * @param sink Network sink that received the packet
 * @param buf Packet buffer with IoTSense header
 */
static void iotsense_network_inbound_handler(struct packet_sink *sink, struct net_buf *buf)
{
	struct router_inbound_route *route;
	uint16_t packet_id;

	/* Validate packet size */
	if (buf->len < IOTSENSE_HEADER_SIZE) {
#ifdef CONFIG_PACKET_IO_STATS
		atomic_inc(&iotsense_router.parse_errors);
#endif
		LOG_WRN("Packet too small: %d bytes", buf->len);
		return;
	}

	/* Validate header */
	if (iotsense_validate_header(buf) < 0) {
#ifdef CONFIG_PACKET_IO_STATS
		atomic_inc(&iotsense_router.parse_errors);
#endif
		LOG_WRN("Invalid header");
		return;
	}

	/* Extract packet ID */
	packet_id = iotsense_get_packet_id(buf);
	if (packet_id == PKT_ID_INVALID) {
#ifdef CONFIG_PACKET_IO_STATS
		atomic_inc(&iotsense_router.parse_errors);
#endif
		LOG_WRN("Invalid packet ID");
		return;
	}

	/* Find route */
	route = router_find_inbound_route(&iotsense_router, packet_id);
	if (!route) {
#ifdef CONFIG_PACKET_IO_STATS
		atomic_inc(&iotsense_router.unknown_packet_ids);
#endif
		LOG_WRN("Unknown packet ID: 0x%04x", packet_id);
		return;
	}

	/* Strip header and deliver payload (buffer will be consumed) */
	net_buf_pull(buf, IOTSENSE_HEADER_SIZE);

	/* Deliver to application sink */
	int ret = packet_sink_deliver_consume(route->app_sink_ptr, buf, K_NO_WAIT);
	if (ret < 0) {
		LOG_WRN("Delivery failed: %d", ret);
	}

#ifdef CONFIG_PACKET_IO_STATS
	atomic_inc(&iotsense_router.inbound_packets);
#endif
}

/**
 * @brief Handle outbound application packets
 *
 * Processes packets from application sources:
 * 1. Adds the IoTSense header with the specified packet ID
 * 2. Chains the payload buffer (zero-copy)
 * 3. Sends the complete packet to the network
 *
 * @param router Router instance
 * @param payload Payload buffer from application
 * @param packet_id Packet ID to include in header
 */
static void iotsense_network_outbound_handler(struct packet_router *router, struct net_buf *payload,
					      uint16_t packet_id)
{
	struct net_buf *complete_packet;

	/* Add IoTSense header */
	complete_packet = iotsense_add_header(payload, packet_id, &iotsense_header_pool, NULL);
	if (!complete_packet) {
#ifdef CONFIG_PACKET_IO_STATS
		atomic_inc(&router->buffer_errors);
#endif
		LOG_ERR("Failed to add header");
		net_buf_unref(payload);
		return;
	}

	/* Send to network */
	int ret = packet_source_send_consume(&router->network_source, complete_packet, K_NO_WAIT);
	if (ret < 0) {
		LOG_WRN("Network send failed: %d", ret);
	}

#ifdef CONFIG_PACKET_IO_STATS
	atomic_inc(&router->outbound_packets);
#endif
}

/*
 * Module Initialization
 */

/**
 * @brief Initialize the IoTSense router
 *
 * Called during system initialization to set up the router
 * and register all compile-time routes.
 *
 * @return 0 on success, negative errno on error
 */
static int iotsense_router_init(void)
{
	int ret;

	ret = router_init(&iotsense_router);
	if (ret < 0) {
		LOG_ERR("Failed to initialize IoTSense router: %d", ret);
		return ret;
	}

	LOG_INF("IoTSense router initialized");
	return 0;
}

/* Initialize during application phase */
SYS_INIT(iotsense_router_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);