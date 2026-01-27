/*
 * Packet Routing Sample - Command Handler Implementation
 *
 * Processes control commands from TCP server (start/stop sampling)
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/buf.h>
#include <weave/packet.h>

#include "cmd_handler.h"
#include "sensors.h"

LOG_MODULE_REGISTER(cmd_handler, LOG_LEVEL_INF);

/* Forward declaration of handler function */
static void cmd_handler_fn(struct net_buf *buf, void *user_data);

/* Define command sink with immediate execution */
WEAVE_PACKET_SINK_DEFINE(cmd_sink, cmd_handler_fn, WV_IMMEDIATE, WV_NO_FILTER, NULL);

/* Command statistics */
static uint32_t commands_processed;
static uint32_t start_commands;
static uint32_t stop_commands;
static uint32_t unknown_commands;

/* Command handler */
static void cmd_handler_fn(struct net_buf *buf, void *user_data)
{
	ARG_UNUSED(user_data);

	if (buf->len < 1) {
		LOG_WRN("Empty command received");
		return;
	}

	uint8_t cmd = buf->data[0];
	commands_processed++;

	switch (cmd) {
	case CMD_START_SAMPLING:
		start_commands++;
		LOG_INF("Command: START sampling (total: %u start, %u stop)", start_commands,
			stop_commands);
		sensor_start_sampling();
		break;

	case CMD_STOP_SAMPLING:
		stop_commands++;
		LOG_INF("Command: STOP sampling (total: %u start, %u stop)", start_commands,
			stop_commands);
		sensor_stop_sampling();
		break;

	default:
		unknown_commands++;
		LOG_WRN("Unknown command: 0x%02x (total unknown: %u)", cmd, unknown_commands);
		break;
	}
}

/* No thread needed for immediate handler - executes in source context */
