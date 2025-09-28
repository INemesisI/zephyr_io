/*
 * Copyright (c) 2024 Zephyr IO Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This sample demonstrates Weave's non-invasive integration with existing threads.
 * Modules are defined in separate files, and connections are wired here at compile time.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr_io/weave/weave.h>
#include "sensor_module.h"
#include "monitor_module.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

/* ========================================================================
 * Method Ports - Define client-side endpoints for calling methods
 * ======================================================================== */

/* Define method call ports for calling sensor methods */
WEAVE_METHOD_PORT_DEFINE(monitor_call_read_sensor, struct read_sensor_request,
			 struct read_sensor_reply);

WEAVE_METHOD_PORT_DEFINE(monitor_call_set_config, struct set_config_request,
			 struct set_config_reply);

WEAVE_METHOD_PORT_DEFINE(monitor_call_get_stats, struct get_stats_request, struct get_stats_reply);

/* ========================================================================
 * Wiring - Connect modules at compile time
 * Direct method-to-method connections with module-prefixed names
 * ======================================================================== */

/* Connect monitor's method call ports to sensor's methods */
WEAVE_METHOD_CONNECT(monitor_call_read_sensor, sensor_read_sensor);
WEAVE_METHOD_CONNECT(monitor_call_set_config, sensor_set_config);
WEAVE_METHOD_CONNECT(monitor_call_get_stats, sensor_get_stats);

/* Connect sensor's threshold signal to monitor's handler */
WEAVE_SIGNAL_CONNECT(threshold_exceeded, monitor_on_threshold_exceeded);

/* ========================================================================
 * Application Main
 * ======================================================================== */

int main(void)
{
	int ret;

	LOG_INF("==============================================");
	LOG_INF("Weave Thread Integration Sample");
	LOG_INF("==============================================");
	LOG_INF("Using clean API with direct method connections");

	/* Give sensor thread time to initialize */
	k_sleep(K_MSEC(100));

	LOG_INF("\nTest 1: Configure sensor with low threshold");
	LOG_INF("----------------------------------------------");

	struct set_config_request new_config = {
		.sample_rate_ms = 500,
		.threshold = 70,
		.auto_sample = true,
	};
	struct set_config_reply old_config;

	ret = weave_call_method(&monitor_call_set_config, &new_config, sizeof(new_config),
				&old_config, sizeof(old_config), K_SECONDS(1));
	if (ret == 0) {
		LOG_INF("Config changed: old threshold=%d, new=%d", old_config.threshold,
			new_config.threshold);
	} else {
		LOG_ERR("Failed to set config: %d", ret);
	}

	/* Let auto-sampling run for a bit */
	k_sleep(K_SECONDS(3));

	LOG_INF("\nTest 2: Manual sensor reads");
	LOG_INF("----------------------------");

	for (int i = 0; i < 5; i++) {
		struct read_sensor_request req = {.channel = i};
		struct read_sensor_reply rep;

		ret = weave_call_method(&monitor_call_read_sensor, &req, sizeof(req), &rep,
					sizeof(rep), K_SECONDS(1));
		if (ret == 0) {
			LOG_INF("Manual read ch%d: value=%d", req.channel, rep.value);
		}

		k_sleep(K_SECONDS(1));
	}

	LOG_INF("\nTest 3: Increase threshold");
	LOG_INF("---------------------------");

	new_config.threshold = 150;
	new_config.sample_rate_ms = 1000;

	ret = weave_call_method(&monitor_call_set_config, &new_config, sizeof(new_config), NULL,
				0, /* No reply needed */
				K_SECONDS(1));
	if (ret == 0) {
		LOG_INF("Threshold increased to %d", new_config.threshold);
	}

	/* Let it run */
	k_sleep(K_SECONDS(3));

	LOG_INF("\nTest 4: Get statistics");
	LOG_INF("-----------------------");

	struct get_stats_request stats_req = {};
	struct get_stats_reply stats;

	ret = weave_call_method(&monitor_call_get_stats, &stats_req, sizeof(stats_req), &stats,
				sizeof(stats), K_SECONDS(1));
	if (ret == 0) {
		LOG_INF("Sensor Statistics:");
		LOG_INF("  Total reads:      %u", stats.total_reads);
		LOG_INF("  Threshold events: %u", stats.threshold_events);
		LOG_INF("  Value range:      %d to %d", stats.min_value, stats.max_value);
	}

	struct monitor_stats *mon_stats = monitor_get_statistics();
	LOG_INF("\nMonitor Statistics:");
	LOG_INF("  Alerts received: %u", mon_stats->alerts_received);
	if (mon_stats->alerts_received > 0) {
		LOG_INF("  Last alert value: %d", mon_stats->last_alert_value);
	}

	LOG_INF("\n==============================================");
	LOG_INF("Sample Complete!");
	LOG_INF("==============================================");

	LOG_INF("\nKey Observations:");
	LOG_INF("- Direct method-to-method connections (no IDs needed)");
	LOG_INF("- Type-safe connections with request/reply structures");
	LOG_INF("- Clean naming conventions (call_*, on_*, *_request, *_reply)");
	LOG_INF("- Modules cleanly separated into individual files");
	LOG_INF("- Connections wired at compile time in main.c");
	LOG_INF("- Thread boundaries properly respected");

	return 0;
}