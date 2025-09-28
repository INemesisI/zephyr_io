/*
 * Packet I/O Sample - Echo Module Implementation
 *
 * Echo Server - receives packets and echoes them back
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/buf.h>
#include <zephyr_io/flow/flow.h>

#include "echo.h"

LOG_MODULE_REGISTER(echo, LOG_LEVEL_INF);

/* Event queue for echo sink */
FLOW_EVENT_QUEUE_DEFINE(echo_queue, 10);

/* Handler for echo server */
static void echo_handler(struct flow_sink *sink, struct net_buf *buf);

/* Define echo sink with queued handler */
FLOW_SINK_DEFINE_QUEUED(echo_sink, echo_handler, echo_queue);

/* Define echo source - sends received packets back */
FLOW_SOURCE_DEFINE(echo_source);

/* Echo statistics */
static uint32_t packets_received;
static uint32_t packets_echoed;
static uint32_t bytes_processed;
static uint32_t first_packet_time;

static void echo_handler(struct flow_sink *sink, struct net_buf *buf)
{
	size_t total_len = net_buf_frags_len(buf);
	int ret;

	/* Track first packet time for rate calculation */
	if (packets_received == 0) {
		first_packet_time = k_uptime_get_32();
	}

	packets_received++;
	bytes_processed += total_len;

	/* Echo the packet back to the processor */
	/* The packet ID is already stamped in the buffer by the routed source */
	ret = flow_source_send(&echo_source, buf, K_NO_WAIT);
	if (ret > 0) {
		packets_echoed++;
	} else {
		LOG_WRN("ECHO: Failed to send packet back");
	}

	/* Log statistics periodically */
	if (packets_received % 10 == 0) {
		uint32_t elapsed = k_uptime_get_32() - first_packet_time;
		if (elapsed > 0) {
			uint32_t rate = (bytes_processed * 1000) / elapsed;
			LOG_INF("Echo Stats: RX=%u, Echoed=%u, %u bytes, %u B/s", packets_received,
				packets_echoed, bytes_processed, rate);
		}
	}

	/* Buffer unref handled by framework */
}

static void echo_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("Echo server started");

	while (1) {
		/* Process events from the echo queue */
		int ret = flow_event_process(&echo_queue, K_FOREVER);
		if (ret != 0 && ret != -EAGAIN) {
			LOG_ERR("Failed to process echo event: %d", ret);
		}
	}
}

/* Auto-start echo thread */
K_THREAD_DEFINE(echo_thread, 1024, echo_thread_fn, NULL, NULL, NULL, 7, 0, 0);