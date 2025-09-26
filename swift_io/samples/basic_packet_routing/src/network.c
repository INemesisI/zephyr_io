/*
 * Packet I/O Sample - Network Module Implementation
 *
 * Self-contained TCP network transmitter
 */

#include <zephyr/kernel.h>
#include <zephyr/net/buf.h>
#include <zephyr_io/swift_io/swift_io.h>
#include <zephyr/logging/log.h>

#include "network.h"
#include "packet_defs.h"

LOG_MODULE_REGISTER(network, LOG_LEVEL_INF);

/* Event queue for network sink */
SWIFT_IO_EVENT_QUEUE_DEFINE(network_queue, 10);

/* Handler for TCP transmission */
static void tcp_handler(struct swift_io_sink *sink, struct net_buf *buf);

/* Define TCP network sink with queued handler */
SWIFT_IO_SINK_DEFINE_QUEUED(tcp_sink, tcp_handler, network_queue);

/* Network statistics */
static uint32_t packets_transmitted;
static uint32_t bytes_transmitted;
static uint32_t first_packet_time;

static void tcp_handler(struct swift_io_sink *sink, struct net_buf *buf)
{
	size_t total_len = net_buf_frags_len(buf);

	/* Track first packet time for rate calculation */
	if (packets_transmitted == 0) {
		first_packet_time = k_uptime_get_32();
	}

	/* Simulate TCP transmission with guaranteed delivery */
	k_msleep(50); /* Simulate network latency */

	/* Parse and log packet header if present */
	if (buf->len >= sizeof(struct packet_header)) {
		struct packet_header *hdr = (struct packet_header *)buf->data;
		LOG_INF("TCP TX: Sensor %d, Type %02x, Seq %u, %zu bytes", hdr->source_id,
			hdr->packet_type, hdr->sequence, total_len);
	} else {
		LOG_INF("TCP TX: %zu bytes (no header)", total_len);
	}

	/* Update statistics */
	packets_transmitted++;
	bytes_transmitted += total_len;

	/* Log statistics with every packet */
	uint32_t elapsed = k_uptime_get_32() - first_packet_time;
	if (elapsed > 0) {
		uint32_t rate = (bytes_transmitted * 1000) / elapsed;
		LOG_INF("TCP Stats: %u packets, %u bytes, %u B/s", packets_transmitted,
			bytes_transmitted, rate);
	}

	/* Buffer unref handled by framework */
}

static void network_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("TCP network transmitter started");

	while (1) {
		/* Process events from the network queue */
		int ret = swift_io_event_process(&network_queue, K_FOREVER);
		if (ret != 0 && ret != -EAGAIN) {
			LOG_ERR("Failed to process network event: %d", ret);
		}
	}
}

/* Auto-start network thread */
K_THREAD_DEFINE(network_thread, 1024, network_thread_fn, NULL, NULL, NULL, 7, 0, 0);