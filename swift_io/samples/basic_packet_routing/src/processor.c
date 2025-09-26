/*
 * Packet I/O Sample - Processor Module Implementation
 *
 * Self-contained packet processor with own buffer pool and thread
 */

#include <zephyr/kernel.h>
#include <zephyr/net/buf.h>
#include <zephyr_io/swift_io/swift_io.h>
#include <zephyr/logging/log.h>

#include "processor.h"
#include "packet_defs.h"

LOG_MODULE_REGISTER(processor, LOG_LEVEL_INF);

/* Processor module's own buffer pool (small - only for headers) */
NET_BUF_POOL_DEFINE(processor_pool, 16, sizeof(struct packet_header), 4, NULL);

/* Event queue for processor sink */
SWIFT_IO_EVENT_QUEUE_DEFINE(processor_queue, 10);

/* Handler for incoming packets */
static void processor_handler(struct swift_io_sink *sink, struct net_buf *buf);

/* Define processor sink with queued handler and source */
SWIFT_IO_SINK_DEFINE_QUEUED(processor_sink, processor_handler, processor_queue);
SWIFT_IO_SOURCE_DEFINE(processor_source);

/* Sequence counter */
static uint16_t sequence_counter;

static void processor_handler(struct swift_io_sink *sink, struct net_buf *buf)
{
	struct net_buf *header_buf;
	struct packet_header *header;
	uint8_t source_id = 0;

	/* Allocate buffer for header */
	header_buf = net_buf_alloc(&processor_pool, K_NO_WAIT);
	if (!header_buf) {
		LOG_WRN("No buffer for header");
		/* Buffer unref handled by framework */
		return;
	}

	/* Determine source from packet data (first byte convention) */
	if (buf->len > 0) {
		source_id = (buf->data[0] == 0xA0) ? 1 : 2;
	}

	/* Calculate the original payload length before chaining */
	uint16_t payload_len = buf->len; /* Get length of single buffer */

	/* Fill in header */
	header = (struct packet_header *)net_buf_add(header_buf, sizeof(*header));
	header->source_id = source_id;
	header->packet_type = PACKET_TYPE_DATA;
	header->sequence = sequence_counter++;
	header->timestamp = k_uptime_get_32();
	header->content_length = payload_len; /* Set payload length */
	header->reserved = 0;

	/* Chain original data after header - frag_add takes ownership of buf */
	/* Add reference since handler doesn't own the buffer it received */
	buf = net_buf_ref(buf);
	net_buf_frag_add(header_buf, buf);

	/* Log the processing */
	size_t total_len = net_buf_frags_len(header_buf);
	LOG_INF("Processed from sensor %d, seq %u, total %zu bytes", source_id, header->sequence,
		total_len);

	/* Forward the packet with header to all connected sinks */
	swift_io_source_send(&processor_source, header_buf, K_NO_WAIT);
}

static void processor_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("Packet processor started");

	while (1) {
		/* Process events from the queue */
		int ret = swift_io_event_process(&processor_queue, K_FOREVER);
		if (ret != 0 && ret != -EAGAIN) {
			LOG_ERR("Failed to process packet event: %d", ret);
		}
	}
}

/* Auto-start processor thread */
K_THREAD_DEFINE(processor_thread, 1024, processor_thread_fn, NULL, NULL, NULL, 7, 0, 0);