/*
 * Packet I/O Basic Sample - Main
 *
 * Demonstrates modular packet routing with header addition and validation.
 * Each module (sensors, processor, network, validator) is self-contained
 * with its own thread and buffer pool, initialized automatically via K_THREAD_DEFINE.
 *
 * Packet flow:
 *   sensor1_source ─┐                                      ┌─→ tcp_sink (network)
 *                   ├─→ processor_sink → processor_source ─┤
 *   sensor2_source ─┘                                      └─→ validator_sink
 */

#include <zephyr/kernel.h>
#include <zephyr/packet_io/packet_io.h>
#include <zephyr/logging/log.h>

#include "sensors.h"
#include "processor.h"
#include "network.h"
#include "validator.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Wire up the connections at compile time */
PACKET_SOURCE_CONNECT(sensor1_source, processor_sink);
PACKET_SOURCE_CONNECT(sensor2_source, processor_sink);
PACKET_SOURCE_CONNECT(processor_source, tcp_sink);
PACKET_SOURCE_CONNECT(processor_source, validator_sink);

int main(void)
{
	LOG_INF("Packet I/O Routing Sample");
	LOG_INF("==========================");
	LOG_INF("Demonstrating modular packet flow with header addition and validation");
	LOG_INF("Routing to TCP network and packet validator");
	LOG_INF("");
	LOG_INF("Module threads start automatically:");
	LOG_INF("  - Sensors: Generate test packets");
	LOG_INF("  - Processor: Add headers and forward");
	LOG_INF("  - Network: TCP transmission");
	LOG_INF("  - Validator: Check packet integrity");

	/* All threads start automatically via K_THREAD_DEFINE */
	/* Main thread can just sleep */
	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}