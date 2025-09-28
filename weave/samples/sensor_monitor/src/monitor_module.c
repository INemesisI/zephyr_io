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

/* Signal handler implementation */
static void handle_threshold_exceeded(void *user_data, const struct threshold_exceeded_event *event)
{
	struct monitor_stats *ctx = &monitor_ctx;

	ctx->alerts_received++;
	ctx->last_alert_value = event->value;

	LOG_WRN("ALERT: Threshold exceeded! value=%d > %d (alert #%u)", event->value,
		event->threshold, ctx->alerts_received);
}

/* Define signal handler with immediate execution (no queue) */
WEAVE_SIGNAL_HANDLER_DEFINE_IMMEDIATE(monitor_on_threshold_exceeded, handle_threshold_exceeded,
				      struct threshold_exceeded_event);

/* Get monitor statistics */
struct monitor_stats *monitor_get_statistics(void)
{
	return &monitor_ctx;
}