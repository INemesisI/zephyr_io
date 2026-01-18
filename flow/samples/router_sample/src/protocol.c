/*
 * Packet I/O Sample - Protocol Module Implementation
 *
 * Self-contained packet processor with own buffer pool and thread.
 * Uses flow buffer metadata (packet_id, flags, counter, timestamp).
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/buf.h>
#include <zephyr_io/flow/flow.h>

#include "protocol.h"
#include "sensors.h" /* For packet_id_SENSOR1/2 */

LOG_MODULE_REGISTER(protocol, LOG_LEVEL_INF);

/* Protocol packet header - private to protocol module */
struct packet_header {
	uint8_t packet_id;
	uint8_t reserved;        /* Reserved for future use */
	uint16_t counter;        /* From metadata */
	uint16_t content_length; /* Length of payload (excluding header) */
	uint64_t timestamp_ns;   /* Timestamp in nanoseconds */
} __packed;

/* Buffer pool for protocol headers */
FLOW_BUF_POOL_DEFINE(protocol_pool, 4, sizeof(struct packet_header), NULL);

/* Event queue for protocol sink */
FLOW_EVENT_QUEUE_DEFINE(protocol_queue, 10);

/* Handlers for packet processing */
static void outbound_handler(struct flow_sink *sink, struct net_buf *buf_ref);
static void inbound_handler(struct flow_sink *sink, struct net_buf *buf_ref);

/* Define outbound sink for packets from sensors */
FLOW_SINK_DEFINE_QUEUED(protocol_outbound_sink, outbound_handler, &protocol_queue);

/* Define inbound sink for packets from TCP server */
FLOW_SINK_DEFINE_QUEUED(protocol_inbound_sink, inbound_handler, &protocol_queue);

/* Define sources for forwarding packets */
FLOW_SOURCE_DEFINE(protocol_outbound_source); /* For sending to TCP server */
FLOW_SOURCE_DEFINE(protocol_inbound_source);  /* For sending processed inbound data */

static void outbound_handler(struct flow_sink *sink, struct net_buf *buf_ref)
{
	struct net_buf *header_buf;
	struct packet_header *header;
	uint8_t packet_id;
	uint16_t counter;
	uint64_t cycles;

	/* Allocate buffer for header */
	header_buf = flow_buf_alloc(&protocol_pool, K_NO_WAIT);
	if (!header_buf) {
		LOG_WRN("No buffer for header");
		/* Buffer unref handled by framework */
		return;
	}

	/* Get metadata from incoming buffer */
	if (flow_buf_get_id(buf_ref, &packet_id) != 0) {
		LOG_ERR("No packet ID in buffer");
		net_buf_unref(header_buf);
		return;
	}

	if (flow_buf_get_counter(buf_ref, &counter) != 0) {
		counter = 0;
	}

	if (flow_buf_get_timestamp_cycles(buf_ref, &cycles) != 0) {
		cycles = 0;
	}

	/* Calculate the original payload length before chaining */
	uint16_t payload_len = net_buf_frags_len(buf_ref);

	/* Fill in protocol header with metadata */
	header = (struct packet_header *)net_buf_add(header_buf, sizeof(*header));
	header->packet_id = packet_id;
	header->reserved = 0;
	header->counter = counter;
	header->content_length = payload_len;
	header->timestamp_ns = k_cyc_to_ns_floor64(cycles);

	/* Chain original data after header - frag_add takes ownership of a buffer */
	/* We need to prevent the framework from unreffing our buffer since we're
	 * transferring ownership to the fragment chain. Take a ref for the framework. */

	/* Chain original data after header - frag_add takes ownership of a buffer */
	/* Take a new reference since the framework will unref the original */
	struct net_buf *chain_buf = net_buf_ref(buf_ref);

	/* Safety check: ensure we're not creating circular references */
	if (chain_buf == header_buf) {
		LOG_ERR("CRITICAL: Trying to chain buffer to itself!");
		net_buf_unref(chain_buf);
		net_buf_unref(header_buf);
		return;
	}

	/* Check if chain_buf somehow points back to header_buf */
	if (chain_buf->frags == header_buf) {
		LOG_ERR("CRITICAL: Circular reference detected!");
		net_buf_unref(chain_buf);
		net_buf_unref(header_buf);
		return;
	}

	net_buf_frag_add(header_buf, chain_buf);

	/* Log the processing with detailed packet info */
	size_t total_len = net_buf_frags_len(header_buf);
	LOG_INF("Processed: Sensor %d, counter=%u, timestamp=%llu ns, %zu bytes (header %zu + "
		"payload %u)",
		packet_id, counter, header->timestamp_ns, total_len, sizeof(*header),
		header->content_length);

	/* Forward the packet with header to all connected sinks */
	/* The packet ID is preserved in the buffer for routing */
	/* flow_source_send consumes the buffer reference */
	int ret = flow_source_send(&protocol_outbound_source, header_buf, K_NO_WAIT);
	if (ret <= 0) {
		LOG_ERR("Failed to send to TCP server: %d sinks received", ret);
	}
}

/* Inbound handler - processes packets received from TCP server */
static void inbound_handler(struct flow_sink *sink, struct net_buf *buf_ref)
{
	struct packet_header *header;

	/* Check if buffer has header */
	if (buf_ref->len < sizeof(struct packet_header)) {
		LOG_WRN("Inbound packet too small for header: %d bytes", buf_ref->len);
		return;
	}

	/* Point to header in the buffer */
	header = (struct packet_header *)buf_ref->data;

	/* Restore metadata from protocol header for routing */
	flow_buf_set_id(buf_ref, header->packet_id);
	flow_buf_set_counter(buf_ref, header->counter);

	/* Convert nanoseconds back to cycles for timestamp restoration */
	/* Note: This assumes CONFIG_FLOW_BUF_TIMESTAMP_HIRES is enabled (per prj.conf) */
	uint64_t cycles = k_ns_to_cyc_floor64(header->timestamp_ns);
	flow_buf_set_timestamp_cycles(buf_ref, cycles);

	LOG_INF("Inbound: packet_id=%d, counter=%u, payload=%u bytes", header->packet_id,
		header->counter, buf_ref->len - sizeof(struct packet_header));

	/* Skip past the header to get to the payload */
	net_buf_pull(buf_ref, sizeof(struct packet_header));

	/* Forward the payload (without header) for further processing */
	/* Handler doesn't own the buffer, so use send_ref to preserve the reference */
	flow_source_send_ref(&protocol_inbound_source, buf_ref, K_NO_WAIT);
}

static void protocol_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("Protocol processor started");

	while (1) {
		/* Process events from the queue */
		int ret = flow_event_process(&protocol_queue, K_FOREVER);
		if (ret != 0 && ret != -EAGAIN) {
			LOG_ERR("Failed to process protocol event: %d", ret);
		}
	}
}

/* Auto-start protocol thread */
K_THREAD_DEFINE(protocol_thread, 1024, protocol_thread_fn, NULL, NULL, NULL, 7, 0, 0);