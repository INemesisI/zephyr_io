/*
 * Copyright (c) 2024 Zephyr Project
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * IoTSense Protocol Router Implementation
 */

#include "iotsense_router.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/buf.h>

LOG_MODULE_REGISTER(iotsense_router, LOG_LEVEL_INF);

#define IOTSENSE_VERSION     0x01
#define IOTSENSE_HEADER_SIZE sizeof(struct iotsense_header)
#define IOTSENSE_MIN_PACKET  IOTSENSE_HEADER_SIZE

static inline bool is_valid_packet_id(uint8_t packet_id)
{
	return (packet_id != PKT_ID_INVALID);
}

static uint16_t iotsense_get_packet_id(const struct net_buf *buf)
{
	if (buf->len < IOTSENSE_HEADER_SIZE) {
		return PKT_ID_INVALID;
	}

	const struct iotsense_header *hdr = (const struct iotsense_header *)buf->data;

	if (hdr->version != IOTSENSE_VERSION) {
		return PKT_ID_INVALID;
	}

	return hdr->packet_id;
}

static int iotsense_validate_header(const struct net_buf *buf)
{
	const struct iotsense_header *hdr;
	uint16_t total_len;

	if (buf->len < IOTSENSE_HEADER_SIZE) {
		return -EINVAL;
	}

	hdr = (const struct iotsense_header *)buf->data;

	if (hdr->version != IOTSENSE_VERSION) {
		return -EPROTONOSUPPORT;
	}

	total_len = IOTSENSE_HEADER_SIZE + hdr->payload_len;
	if (buf->len != total_len) {
		return -EMSGSIZE;
	}

	if (!is_valid_packet_id(hdr->packet_id)) {
		return -EINVAL;
	}

	return 0;
}

static struct net_buf *iotsense_add_header(struct net_buf *payload_buf, uint16_t packet_id,
					   struct net_buf_pool *header_pool, void *context)
{
	struct net_buf *header_buf;
	struct iotsense_header *hdr;

	ARG_UNUSED(context);

	header_buf = net_buf_alloc(header_pool, K_NO_WAIT);
	if (!header_buf) {
		return NULL;
	}

	hdr = (struct iotsense_header *)net_buf_add(header_buf, IOTSENSE_HEADER_SIZE);

	hdr->version = IOTSENSE_VERSION;
	hdr->packet_id = packet_id;
	hdr->payload_len = payload_buf ? payload_buf->len : 0;

	if (payload_buf) {
		net_buf_frag_add(header_buf, payload_buf);
	}

	return header_buf;
}

NET_BUF_POOL_DEFINE(iotsense_header_pool, 32, 64, 4, NULL);

static void iotsense_network_inbound_handler(struct flow_sink *sink, struct net_buf *buf);
static void iotsense_network_outbound_handler(struct packet_router *router, struct net_buf *buf,
					      uint16_t packet_id);

ROUTER_DEFINE(iotsense_router, iotsense_network_inbound_handler, iotsense_network_outbound_handler);

static void iotsense_network_inbound_handler(struct flow_sink *sink, struct net_buf *buf)
{
	struct router_inbound_route *route;
	uint16_t packet_id;

	if (buf->len < IOTSENSE_HEADER_SIZE) {
#ifdef CONFIG_FLOW_STATS
		atomic_inc(&iotsense_router.parse_errors);
#endif
		LOG_WRN("Packet too small: %d bytes", buf->len);
		return;
	}

	if (iotsense_validate_header(buf) < 0) {
#ifdef CONFIG_FLOW_STATS
		atomic_inc(&iotsense_router.parse_errors);
#endif
		LOG_WRN("Invalid header");
		return;
	}

	packet_id = iotsense_get_packet_id(buf);
	if (packet_id == PKT_ID_INVALID) {
#ifdef CONFIG_FLOW_STATS
		atomic_inc(&iotsense_router.parse_errors);
#endif
		LOG_WRN("Invalid packet ID");
		return;
	}

	route = router_find_inbound_route(&iotsense_router, packet_id);
	if (!route) {
#ifdef CONFIG_FLOW_STATS
		atomic_inc(&iotsense_router.unknown_packet_ids);
#endif
		LOG_WRN("Unknown packet ID: 0x%04x", packet_id);
		return;
	}

	net_buf_pull(buf, IOTSENSE_HEADER_SIZE);

	int ret = flow_sink_deliver_consume(route->app_sink_ptr, buf, K_NO_WAIT);
	if (ret < 0) {
		LOG_WRN("Delivery failed: %d", ret);
	}

#ifdef CONFIG_FLOW_STATS
	atomic_inc(&iotsense_router.inbound_packets);
#endif
}

static void iotsense_network_outbound_handler(struct packet_router *router, struct net_buf *payload,
					      uint16_t packet_id)
{
	struct net_buf *complete_packet;

	complete_packet = iotsense_add_header(payload, packet_id, &iotsense_header_pool, NULL);
	if (!complete_packet) {
#ifdef CONFIG_FLOW_STATS
		atomic_inc(&router->buffer_errors);
#endif
		LOG_ERR("Failed to add header");
		net_buf_unref(payload);
		return;
	}

	int ret = flow_source_send_consume(&router->network_source, complete_packet, K_NO_WAIT);
	if (ret < 0) {
		LOG_WRN("Network send failed: %d", ret);
	}

#ifdef CONFIG_FLOW_STATS
	atomic_inc(&router->outbound_packets);
#endif
}

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

SYS_INIT(iotsense_router_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);