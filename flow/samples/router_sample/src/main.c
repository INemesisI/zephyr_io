/*
 * Flow Router Sample - Main
 *
 * Demonstrates TCP-based sensor data routing with protocol headers and
 * remote control commands. Sensors can be started/stopped via TCP commands.
 *
 * Packet flow:
 *   sensor1_source ─┐
 *                   ├─→ processor_outbound_sink → processor_outbound_source ─→ tcp_sink
 *   sensor2_source ─┘
 *
 * Command flow:
 *   tcp_rx_source → cmd_sink (start/stop sampling)
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr_io/flow/flow.h>

#include "tcp_server.h"
#include "protocol.h"
#include "sensors.h"
#include "cmd_handler.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Wire up the connections at compile time */

/* Sensors to protocol processor */
FLOW_CONNECT(&sensor1_source, &protocol_outbound_sink);
FLOW_CONNECT(&sensor2_source, &protocol_outbound_sink);

/* Protocol processor to TCP server */
FLOW_CONNECT(&protocol_outbound_source, &tcp_sink);

/* TCP incoming packets to command handler */
FLOW_CONNECT(&tcp_rx_source, &cmd_sink);

/* Setup networking for native_sim */
static void setup_networking(void)
{
	struct net_if *iface = net_if_get_default();

	if (!iface) {
		LOG_ERR("No network interface available");
		return;
	}

	/* For native_sim, manually configure loopback interface */
	struct in_addr addr = {{{127, 0, 0, 1}}};

	net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0);

	LOG_INF("Network interface configured: 127.0.0.1");
}

int main(void)
{
	LOG_INF("Flow Router Sample with TCP Server");
	LOG_INF("====================================");
	LOG_INF("Sensor data routing with remote control:");
	LOG_INF("  1. Sensors generate data packets with metadata");
	LOG_INF("  2. Protocol module adds headers (packet_id, flags, counter)");
	LOG_INF("  3. TCP server sends packets to connected clients");
	LOG_INF("  4. Clients can send commands to start/stop sampling");
	LOG_INF("");
	LOG_INF("Commands:");
	LOG_INF("  0x01 - Start sampling");
	LOG_INF("  0x02 - Stop sampling");
	LOG_INF("");
	LOG_INF("Connect with: python tcp_client.py");

	/* Setup network interface */
	setup_networking();

	LOG_INF("All modules started, entering main loop...");

	/* All threads start automatically via K_THREAD_DEFINE */
	/* Main thread can just sleep */
	while (1) {
		LOG_DBG("Main thread sleeping...");
		k_sleep(K_FOREVER);
		LOG_ERR("Main thread woke up from K_FOREVER! This should never happen!");
	}

	LOG_ERR("Main thread exiting! This should never be reached!");
	return 0;
}
