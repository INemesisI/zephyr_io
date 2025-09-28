/*
 * Packet I/O Sample - Processor Module Implementation
 *
 * Self-contained packet processor with own buffer pool and thread
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/buf.h>
#include <zephyr_io/flow/flow.h>

#include "processor.h"
#include "sensors.h" /* For packet_id_SENSOR1/2 */

LOG_MODULE_REGISTER(processor, LOG_LEVEL_INF);

/* Simplified packet header - private to processor module */
struct packet_header {
	uint16_t packet_id;
	uint16_t content_length; /* Length of payload (excluding header) */
} __packed;

/* Processor module's own buffer pool (small - only for headers) */
NET_BUF_POOL_DEFINE(processor_pool, 16, sizeof(struct packet_header), 4, NULL);

/* Event queue for processor sink */
FLOW_EVENT_QUEUE_DEFINE(processor_queue, 10);

/* Handlers for packet processing */
static void outbound_handler(struct flow_sink *sink, struct net_buf *buf);
static void inbound_handler(struct flow_sink *sink, struct net_buf *buf);

/* Define outbound sink for packets from sensors */
FLOW_SINK_DEFINE_QUEUED(processor_outbound_sink, outbound_handler, processor_queue);

/* Define inbound sink for packets from echo server */
FLOW_SINK_DEFINE_QUEUED(processor_inbound_sink, inbound_handler, processor_queue);

/* Define sources for forwarding packets */
FLOW_SOURCE_DEFINE(processor_outbound_source); /* For sending to echo server */
FLOW_SOURCE_DEFINE(processor_inbound_source);  /* For sending to validators */

static void outbound_handler(struct flow_sink *sink, struct net_buf *buf)
{
	struct net_buf *header_buf;
	struct packet_header *header;

	/* Allocate buffer for header */
	header_buf = net_buf_alloc(&processor_pool, K_NO_WAIT);
	if (!header_buf) {
		LOG_WRN("No buffer for header");
		/* Buffer unref handled by framework */
		return;
	}

	/* Get source ID from packet ID in buffer's user data (set by
	 * FLOW_SOURCE_DEFINE_ROUTED) */
	uint16_t packet_id;
	if (flow_packet_id_get(buf, &packet_id) != 0) {
		/* If no packet ID available, default to unknown source */
		LOG_ERR("No packet ID in buffer, using source ID 0");
		return;
	}

	/* Calculate the original payload length before chaining */
	uint16_t payload_len = net_buf_frags_len(buf);

	/* Fill in simplified header */
	header = (struct packet_header *)net_buf_add(header_buf, sizeof(*header));
	header->packet_id = packet_id;
	header->content_length = payload_len; /* Set payload length */

	/* Chain original data after header - frag_add takes ownership of buf */
	/* Add reference since handler doesn't own the buffer it received */
	buf = net_buf_ref(buf);
	net_buf_frag_add(header_buf, buf);

	/* Log the processing with detailed packet info */
	size_t total_len = net_buf_frags_len(header_buf);
	LOG_INF("Processed: Sensor %d, %zu bytes (header %zu + payload %u)", packet_id, total_len,
		sizeof(*header), header->content_length);

	/* Forward the packet with header to all connected sinks */
	/* The packet ID is preserved in the buffer for routing */
	flow_source_send(&processor_outbound_source, header_buf, K_NO_WAIT);
}

/* Inbound handler - processes packets echoed back from TCP server */
static void inbound_handler(struct flow_sink *sink, struct net_buf *buf)
{
	struct packet_header *header;
	struct net_buf *payload_buf;
	int ret;

	/* Check if buffer has header */
	if (buf->len < sizeof(struct packet_header)) {
		LOG_WRN("Echo packet too small for header: %d bytes", buf->len);
		return;
	}

	header = (struct packet_header *)buf->data;

	/* Get the payload buffer - it's in the fragment chain after the header */
	if (!buf->frags) {
		LOG_WRN("No payload fragment in echoed packet");
		return;
	}

	payload_buf = buf->frags;

	/* Set the packet ID in the payload buffer for routing to the correct
	 * validator */
	ret = flow_packet_id_set(payload_buf, header->packet_id);
	if (ret != 0) {
		LOG_WRN("Failed to set packet ID for routing: %d", ret);
	}

	/* Forward only the payload (without header) to the correct validator */
	/* flow_source_send takes its own reference, so we don't need to ref/unref */
	flow_source_send(&processor_inbound_source, payload_buf, K_NO_WAIT);
}

static void processor_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("Packet processor started");

	while (1) {
		/* Process events from the queue */
		int ret = flow_event_process(&processor_queue, K_FOREVER);
		if (ret != 0 && ret != -EAGAIN) {
			LOG_ERR("Failed to process packet event: %d", ret);
		}
	}
}

/* Auto-start processor thread */
K_THREAD_DEFINE(processor_thread, 1024, processor_thread_fn, NULL, NULL, NULL, 7, 0, 0);