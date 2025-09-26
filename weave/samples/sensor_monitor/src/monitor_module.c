/*
 * Copyright (c) 2024 Zephyr IO Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "monitor_module.h"

LOG_MODULE_REGISTER(monitor, LOG_LEVEL_INF);

/* Monitor context */
static struct monitor_stats monitor_ctx;

/* Define monitor module (no queue = direct execution) */
WEAVE_MODULE_DEFINE(monitor_module, NULL);

/* Signal handler implementation */
static void handle_threshold_exceeded(struct weave_module *module,
				      const struct threshold_exceeded_event *event)
{
	struct monitor_stats *ctx = &monitor_ctx;

	ctx->alerts_received++;
	ctx->last_alert_value = event->value;

	LOG_WRN("ALERT: Threshold exceeded! value=%d > %d (alert #%u)", event->value,
		event->threshold, ctx->alerts_received);
}

/* Register signal handler with module prefix */
WEAVE_SIGNAL_HANDLER_REGISTER(monitor_on_threshold_exceeded, handle_threshold_exceeded,
			      struct threshold_exceeded_event);

/* Define method call ports (client-side) with module prefix */
WEAVE_METHOD_PORT_DEFINE(monitor_call_read_sensor, struct read_sensor_request,
			 struct read_sensor_reply);

WEAVE_METHOD_PORT_DEFINE(monitor_call_set_config, struct set_config_request,
			 struct set_config_reply);

WEAVE_METHOD_PORT_DEFINE(monitor_call_get_stats, struct get_stats_request, struct get_stats_reply);

/* Get monitor statistics */
struct monitor_stats *monitor_get_statistics(void)
{
	return &monitor_ctx;
}

/* Initialize monitor module */
static int monitor_init(void)
{
	/* Set the handler's module */
	monitor_on_threshold_exceeded.module = &monitor_module;

	LOG_INF("Monitor module initialized");
	return 0;
}

/* Initialize after kernel but before application */
SYS_INIT(monitor_init, APPLICATION, 90);