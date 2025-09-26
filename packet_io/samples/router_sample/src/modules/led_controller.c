/*
 * LED Controller - Toggle Only
 */

#include "led_controller.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/packet_io/packet_io.h>

LOG_MODULE_REGISTER(led_controller, LOG_LEVEL_INF);

/* LED states */
static bool led_status_on;
static atomic_t toggle_count = ATOMIC_INIT(0);

static void led_controller_cmd_handler(struct packet_sink *sink, struct net_buf *buf)
{
	struct led_toggle_cmd *cmd;
	uint32_t count;

	if (buf->len < sizeof(struct led_toggle_cmd)) {
		LOG_WRN("Toggle packet too small: %d bytes", buf->len);
		return;
	}

	cmd = (struct led_toggle_cmd *)buf->data;

	if (cmd->command != LED_TOGGLE_CMD) {
		LOG_WRN("Unknown LED command: 0x%02x", cmd->command);
		return;
	}

	led_status_on = !led_status_on;
	count = atomic_inc(&toggle_count) + 1;

	LOG_INF("LED toggle #%u: %s", count, led_status_on ? "ON" : "OFF");
}

PACKET_SINK_DEFINE_IMMEDIATE(led_controller_sink, led_controller_cmd_handler);
