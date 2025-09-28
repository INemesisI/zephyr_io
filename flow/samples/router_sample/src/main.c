/* Packet Router Sample
 * Demonstrates IoTSense protocol routing with modular architecture
 */

#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr_io/flow/flow.h>

#include "protocols/iotsense_router.h"
#include "modules/led_controller.h"
#include "modules/system_control.h"
#include "modules/tcp_server.h"
#include "modules/temperature_sensor.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Packet type registrations */

/* Temperature sensor - outbound only */
ROUTER_OUTBOUND_ROUTE_DEFINE(iotsense_router, PKT_ID_SENSOR_TEMP, temperature_sensor_source);

/* System control - bidirectional */
ROUTER_OUTBOUND_ROUTE_DEFINE(iotsense_router, PKT_ID_SYSTEM_CONFIG, system_control_source);
ROUTER_INBOUND_ROUTE_DEFINE(iotsense_router, PKT_ID_SYSTEM_CONFIG, system_control_sink);

/* LED controller - inbound only */
ROUTER_INBOUND_ROUTE_DEFINE(iotsense_router, PKT_ID_ACTUATOR_LED, led_controller_sink);

/* Network connections */
FLOW_CONNECT(&tcp_server_source, &iotsense_router.network_sink);
FLOW_CONNECT(&iotsense_router.network_source, &tcp_server_sink);

/* Statistics reporting thread */
static void stats_thread(void *p1, void *p2, void *p3)
{
	struct router_stats stats;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (1) {
		k_sleep(K_SECONDS(30));

		LOG_INF("System status:");

#ifdef CONFIG_FLOW_STATS
		router_get_stats(&iotsense_router, &stats);
		LOG_INF("Router: in=%u out=%u unknown=%u errors=%u buffers=%u",
			stats.inbound_packets, stats.outbound_packets, stats.unknown_packet_ids,
			stats.parse_errors, stats.buffer_errors);
#endif
	}
}

K_THREAD_DEFINE(stats_tid, 1024, stats_thread, NULL, NULL, NULL, 9, 0, 0);
