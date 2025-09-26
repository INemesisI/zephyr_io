/*
 * System Control Module - Ping Only
 */

#include "system_control.h"
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr_io/swift_io/swift_io.h>

LOG_MODULE_REGISTER(sys_control, LOG_LEVEL_INF);

/* Buffer pool for ping responses */
NET_BUF_POOL_DEFINE(sys_control_pool, 4, 32, 4, NULL);

/* Ping counter */
static atomic_t ping_count = ATOMIC_INIT(0);

static void system_control_cmd_handler(struct swift_io_sink *sink, struct net_buf *buf)
{
	struct ping_cmd *cmd;
	struct net_buf *resp_buf;
	struct ping_resp *resp;
	uint32_t count;

	if (buf->len < sizeof(struct ping_cmd)) {
		LOG_WRN("Ping packet too small: %d bytes", buf->len);
		return;
	}

	cmd = (struct ping_cmd *)buf->data;

	if (cmd->command != PING_CMD) {
		LOG_WRN("Unknown command: 0x%02x", cmd->command);
		return;
	}

	resp_buf = net_buf_alloc(&sys_control_pool, K_NO_WAIT);
	if (!resp_buf) {
		LOG_ERR("No buffer for ping response");
		return;
	}

	count = atomic_inc(&ping_count) + 1;

	resp = net_buf_add(resp_buf, sizeof(*resp));
	resp->command = PING_CMD;
	resp->seq_num = cmd->seq_num;
	resp->timestamp = k_uptime_get_32();

	LOG_INF("Ping #%u (seq=%u)", count, cmd->seq_num);

	swift_io_source_send_consume(&system_control_source, resp_buf, K_NO_WAIT);
}

SWIFT_IO_SINK_DEFINE_IMMEDIATE(system_control_sink, system_control_cmd_handler);
SWIFT_IO_SOURCE_DEFINE(system_control_source);
