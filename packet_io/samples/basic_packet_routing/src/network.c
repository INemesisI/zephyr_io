/*
 * Packet I/O Sample - Network Module Implementation
 *
 * Self-contained TCP network transmitter
 */

#include <zephyr/kernel.h>
#include <zephyr/net/buf.h>
#include <zephyr/packet_io/packet_io.h>
#include <zephyr/logging/log.h>

#include "network.h"
#include "packet_defs.h"

LOG_MODULE_REGISTER(network, LOG_LEVEL_INF);

/* Define TCP network sink */
PACKET_SINK_DEFINE(tcp_sink, 10, false);  /* Queue 10, wait if full */

/* Network statistics */
static uint32_t packets_transmitted;
static uint32_t bytes_transmitted;
static uint32_t first_packet_time;

static void network_thread_fn(void *p1, void *p2, void *p3)
{
	struct net_buf *buf;
	int ret;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("TCP network transmitter started");

	while (1) {
		/* Wait for packet on TCP sink */
		ret = k_msgq_get(&tcp_sink.msgq, &buf, K_FOREVER);
		if (ret != 0) {
			continue;
		}

		size_t total_len = net_buf_frags_len(buf);

		/* Track first packet time for rate calculation */
		if (packets_transmitted == 0) {
			first_packet_time = k_uptime_get_32();
		}

		/* Simulate TCP transmission with guaranteed delivery */
		k_sleep(K_MSEC(50));  /* TCP transmission time */

		/* Update statistics */
		packets_transmitted++;
		bytes_transmitted += total_len;

		/* Report network statistics every 5 packets (~1 second) */
		if (packets_transmitted % 5 == 0) {
			uint32_t now = k_uptime_get_32();
			uint32_t duration_ms = now - first_packet_time;
			uint32_t rate_bps = 0;

			if (duration_ms > 0) {
				rate_bps = (bytes_transmitted * 8 * 1000) / duration_ms;
			}

			LOG_INF("TCP Network: %d packets, %d bytes, %d bps",
				packets_transmitted, bytes_transmitted, rate_bps);
		}

		/* In a real system, tcp_send() would linearize the buffer chain if needed
		 * and transmit over the network interface */

		net_buf_unref(buf);
	}
}

/* Static thread initialization - starts automatically */
K_THREAD_DEFINE(network_thread, 2048,
		network_thread_fn, NULL, NULL, NULL,
		5, 0, 0);