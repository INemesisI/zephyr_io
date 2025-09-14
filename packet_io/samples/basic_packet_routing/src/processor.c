/*
 * Packet I/O Sample - Processor Module Implementation
 *
 * Self-contained packet processor with own buffer pool and thread
 */

#include <zephyr/kernel.h>
#include <zephyr/net/buf.h>
#include <zephyr/packet_io/packet_io.h>
#include <zephyr/logging/log.h>

#include "processor.h"
#include "packet_defs.h"

LOG_MODULE_REGISTER(processor, LOG_LEVEL_INF);

/* Processor module's own buffer pool (small - only for headers) */
NET_BUF_POOL_DEFINE(processor_pool, 16, sizeof(struct packet_header), 4, NULL);

/* Define processor sink and source */
PACKET_SINK_DEFINE(processor_sink, 10, false);  /* Queue 10 packets */
PACKET_SOURCE_DEFINE(processor_source);

/* Sequence counter */
static uint16_t sequence_counter;

static void processor_thread_fn(void *p1, void *p2, void *p3)
{
	struct net_buf *in_buf, *header_buf;
	struct packet_header *header;
	int ret;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("Packet processor started");

	while (1) {
		/* Wait for incoming packet */
		ret = k_msgq_get(&processor_sink.msgq, &in_buf, K_FOREVER);
		if (ret != 0) {
			continue;
		}

		/* Allocate buffer for header */
		header_buf = net_buf_alloc(&processor_pool, K_NO_WAIT);
		if (!header_buf) {
			LOG_WRN("No buffer for header");
			net_buf_unref(in_buf);
			continue;
		}

		/* Reserve space and fill header */
		header = net_buf_add(header_buf, sizeof(struct packet_header));
		header->source_id = (in_buf->data[0] == 0xA0) ? SOURCE_ID_SENSOR1 : SOURCE_ID_SENSOR2;
		header->packet_type = PACKET_TYPE_DATA;
		header->sequence = sequence_counter++;
		header->timestamp = k_uptime_get_32();
		header->content_length = net_buf_frags_len(in_buf);  /* Calculate total payload length */
		header->reserved = 0;

		/* Chain original packet data to header buffer (zero-copy) */
		net_buf_frag_add(header_buf, in_buf);

		LOG_DBG("Processing packet from sensor %d, seq %d, header %d + payload %d bytes",
			header->source_id, header->sequence,
			header_buf->len, header->content_length);

		/* Send chained packet to connected sinks */
		ret = packet_source_send(&processor_source, header_buf, K_NO_WAIT);
		LOG_DBG("Distributed to %d sinks", ret);

		/* Clean up - only unref header_buf, in_buf is part of the chain */
		net_buf_unref(header_buf);
	}
}

/* Static thread initialization - starts automatically */
K_THREAD_DEFINE(processor_thread, 2048,
		processor_thread_fn, NULL, NULL, NULL,
		5, 0, 0);