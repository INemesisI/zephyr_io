/*
 * Copyright (c) 2026 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This sample demonstrates the Weave Method RPC API.
 *
 * Key features shown:
 * - Type-safe RPC calls with WEAVE_METHOD_CALL() macro
 * - Request/reply pattern with compile-time type checking
 * - Queued method execution in dedicated thread
 * - No wiring needed - methods are called directly
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "sensor_module.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

int main(void)
{
	int ret;

	LOG_INF("==============================================");
	LOG_INF("Weave Method RPC Sample");
	LOG_INF("==============================================");
	LOG_INF("Demonstrating type-safe RPC with direct method calls");

	/* Give sensor thread time to initialize */
	k_sleep(K_MSEC(100));

	/* --------------------------------------------------------
	 * Test 1: Configure sensor with low threshold
	 * -------------------------------------------------------- */
	LOG_INF("");
	LOG_INF("Test 1: Configure sensor");
	LOG_INF("------------------------");

	struct set_config_request new_config = {
		.sample_rate_ms = 500,
		.threshold = 70,
		.auto_sample = true,
	};

	/* Type-safe call with void response - demonstrates WV_VOID for reply */
	ret = WEAVE_METHOD_CALL(sensor_set_config, &new_config, WV_VOID);
	if (ret == 0) {
		LOG_INF("Config set: threshold=%d, rate=%u ms", new_config.threshold,
			new_config.sample_rate_ms);
	} else {
		LOG_ERR("Failed to set config: %d", ret);
	}

	/* Let auto-sampling run */
	k_sleep(K_SECONDS(3));

	/* --------------------------------------------------------
	 * Test 2: Manual sensor reads
	 * -------------------------------------------------------- */
	LOG_INF("");
	LOG_INF("Test 2: Manual sensor reads");
	LOG_INF("----------------------------");

	for (int i = 0; i < 5; i++) {
		struct read_sensor_request req = {.channel = i};
		struct read_sensor_response res;

		ret = WEAVE_METHOD_CALL(sensor_read_sensor, &req, &res);
		if (ret == 0) {
			LOG_INF("Manual read ch%d: value=%d, timestamp=%u", req.channel, res.value,
				res.timestamp);
		} else {
			LOG_ERR("Read failed: %d", ret);
		}

		k_sleep(K_MSEC(500));
	}

	/* --------------------------------------------------------
	 * Test 3: Update threshold
	 * -------------------------------------------------------- */
	LOG_INF("");
	LOG_INF("Test 3: Increase threshold");
	LOG_INF("---------------------------");

	new_config.threshold = 150;
	new_config.sample_rate_ms = 1000;

	ret = WEAVE_METHOD_CALL(sensor_set_config, &new_config, WV_VOID);
	if (ret == 0) {
		LOG_INF("Config updated: threshold=%d, rate=%u ms", new_config.threshold,
			new_config.sample_rate_ms);
	}

	k_sleep(K_SECONDS(3));

	/* --------------------------------------------------------
	 * Test 4: Get statistics
	 * -------------------------------------------------------- */
	LOG_INF("");
	LOG_INF("Test 4: Get statistics");
	LOG_INF("-----------------------");

	struct get_stats_response stats;

	ret = WEAVE_METHOD_CALL(sensor_get_stats, WV_VOID, &stats);
	if (ret == 0) {
		LOG_INF("Sensor Statistics:");
		LOG_INF("  Total reads:      %u", stats.total_reads);
		LOG_INF("  Threshold events: %u", stats.threshold_events);
		LOG_INF("  Value range:      %d to %d", stats.min_value, stats.max_value);
	}

	/* --------------------------------------------------------
	 * Summary
	 * -------------------------------------------------------- */
	LOG_INF("");
	LOG_INF("==============================================");
	LOG_INF("Sample Complete!");
	LOG_INF("==============================================");
	LOG_INF("");
	LOG_INF("Key observations:");
	LOG_INF("- Direct method calls (no ports or wiring needed)");
	LOG_INF("- WEAVE_METHOD_CALL() provides compile-time type checking");
	LOG_INF("- Methods execute in sensor thread (queued)");
	LOG_INF("- Request/response structures define the contract");

	/* Stop sensor thread for clean shutdown */
	sensor_stop();
	k_sleep(K_MSEC(200));

	return 0;
}
