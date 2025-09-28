/*
 * Flow Basic Sample - Main
 *
 * Demonstrates modular packet routing with header addition, echo server, and
 * validation. Each module (sensors, processor, echo, validators) is
 * self-contained with its own thread and buffer pool, initialized automatically
 * via K_THREAD_DEFINE.
 *
 * Packet flow:
 *   sensor1_source ─┐                                      ┌─→ echo_sink (echo
 * server) ├─→ processor_sink → processor_source ─┤          ↓ sensor2_source ─┘
 * │    echo_source ↓          ↓ validator1  processor_echo_sink validator2 ↓ ↑
 * processor_source └────────────┘
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr_io/flow/flow.h>

#include "echo.h"
#include "processor.h"
#include "sensors.h"
#include "validator.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Wire up the connections at compile time */
/* Sensors to processor outbound */
FLOW_CONNECT(&sensor1_source, &processor_outbound_sink);
FLOW_CONNECT(&sensor2_source, &processor_outbound_sink);

/* Processor outbound to echo server only */
FLOW_CONNECT(&processor_outbound_source, &echo_sink);

/* Echo server back to processor inbound */
FLOW_CONNECT(&echo_source, &processor_inbound_sink);

/* Processor inbound to validators (routed by packet ID) */
FLOW_CONNECT(&processor_inbound_source, &validator1_sink);
FLOW_CONNECT(&processor_inbound_source, &validator2_sink);

int main(void)
{
	LOG_INF("Packet I/O Routing Sample with Echo Server");
	LOG_INF("===========================================");
	LOG_INF("Demonstrating packet routing with ID-based filtering:");
	LOG_INF("  1. Sensors generate packets with unique IDs");
	LOG_INF("  2. Processor adds headers and forwards to echo server");
	LOG_INF("  3. Echo server sends packets back");
	LOG_INF("  4. Processor routes echoed packets to validators");
	LOG_INF("  5. Validators filter packets by sensor ID");
	LOG_INF("");
	LOG_INF("Module threads start automatically:");
	LOG_INF("  - Sensors: Generate test packets with IDs");
	LOG_INF("  - Processor: Add headers and route packets");
	LOG_INF("  - Echo: Echo server");
	LOG_INF("  - Validators: Per-sensor packet validation");

	/* All threads start automatically via K_THREAD_DEFINE */
	/* Main thread can just sleep */
	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}