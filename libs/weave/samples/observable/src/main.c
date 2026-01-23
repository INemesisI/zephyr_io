/*
 * Copyright (c) 2026 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This sample demonstrates Weave Observable for settings/configuration.
 *
 * Key features:
 * - Settings observable holds configuration state
 * - Shell commands update settings via WEAVE_OBSERVABLE_SET
 * - Multiple observers react to settings changes:
 *   - Sensor module (immediate) adjusts sample rate
 *   - Logger observers (immediate + queued) demonstrate multi-listener pattern
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "settings.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

/* ============================ Immediate Observer ============================ */

static int immediate_count;

static void immediate_logger(struct weave_observable *obs, void *user_data)
{
	ARG_UNUSED(obs);
	ARG_UNUSED(user_data);

	struct sensor_settings settings;
	int ret = WEAVE_OBSERVABLE_GET(sensor_settings, &settings);
	if (ret != 0) {
		LOG_ERR("Failed to get settings: %d", ret);
		return;
	}

	immediate_count++;
	LOG_INF("[IMMEDIATE #%d] Settings changed: sample_rate=%u ms", immediate_count,
		settings.sample_rate_ms);
}

WEAVE_OBSERVER_DEFINE(immediate_observer, immediate_logger, WV_IMMEDIATE, NULL);
WEAVE_OBSERVER_CONNECT(sensor_settings, immediate_observer);

/* ============================ Queued Observer ============================ */

WEAVE_MSGQ_DEFINE(logger_msgq, 4);

static int queued_count;

static void queued_logger(struct weave_observable *obs, void *user_data)
{
	ARG_UNUSED(user_data);

	struct sensor_settings settings;
	int ret = weave_observable_get_unchecked(obs, &settings);
	if (ret != 0) {
		LOG_ERR("Failed to get settings: %d", ret);
		return;
	}

	queued_count++;
	LOG_INF("[QUEUED #%d] Settings changed: sample_rate=%u ms", queued_count,
		settings.sample_rate_ms);
}

WEAVE_OBSERVER_DEFINE(queued_observer, queued_logger, &logger_msgq, NULL);
WEAVE_OBSERVER_CONNECT(sensor_settings, queued_observer);

/* ============================ Main ============================ */

int main(void)
{
	LOG_INF("==============================================");
	LOG_INF("Weave Observable Settings Sample");
	LOG_INF("==============================================");
	LOG_INF("");
	LOG_INF("Demonstrates observables for configuration:");
	LOG_INF("- Use 'settings get' to view current settings");
	LOG_INF("- Use 'settings set <rate_ms>' to change sample rate");
	LOG_INF("- Sensor prints readings at configured rate");
	LOG_INF("- Multiple observers log when settings change");
	LOG_INF("");

	while (1) {
		weave_process_messages(&logger_msgq, K_MSEC(100));
	}

	return 0;
}
