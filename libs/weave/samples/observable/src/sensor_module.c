/*
 * Copyright (c) 2026 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "settings.h"

LOG_MODULE_REGISTER(sensor, LOG_LEVEL_INF);

/* ============================ Settings Observable ============================ */

WEAVE_OBSERVABLE_DEFINE(sensor_settings, struct sensor_settings, WV_NO_HANDLER, WV_IMMEDIATE, NULL,
			WV_NO_VALID);

static int sensor_settings_init(void)
{
	struct sensor_settings initial = {
		.sample_rate_ms = 1000,
	};
	int ret = WEAVE_OBSERVABLE_SET(sensor_settings, &initial);
	if (ret < 0) {
		LOG_ERR("Failed to initialize settings: %d", ret);
		return ret;
	}
	LOG_INF("Settings initialized: sample_rate=%u ms", initial.sample_rate_ms);
	return 0;
}

SYS_INIT(sensor_settings_init, APPLICATION, 50);

/* ============================ Settings Observer ============================ */

static atomic_t current_rate_ms = ATOMIC_INIT(1000);
static int sensor_obs_count;

static void settings_changed(struct weave_observable *obs, void *user_data)
{
	ARG_UNUSED(user_data);

	struct sensor_settings settings;
	if (weave_observable_get_unchecked(obs, &settings) == 0) {
		atomic_set(&current_rate_ms, settings.sample_rate_ms);
		sensor_obs_count++;
		LOG_INF("[SENSOR #%d] Rate updated to %u ms", sensor_obs_count,
			settings.sample_rate_ms);
	}
}

/* Define observer for settings changes */
WEAVE_OBSERVER_DEFINE(sensor_settings_observer, settings_changed, WV_IMMEDIATE, NULL);

/* Connect sensor to settings observable */
WEAVE_OBSERVER_CONNECT(sensor_settings, sensor_settings_observer);

/* ============================ Sensor Thread ============================ */

static void sensor_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("Sensor thread started");

	int32_t reading = 0;

	while (1) {
		/* Generate artificial sensor data */
		reading = (reading + 7) % 100;

		LOG_INF("[SENSOR] Reading: %d", reading);

		/* Sleep for configured rate */
		uint32_t rate = atomic_get(&current_rate_ms);
		k_sleep(K_MSEC(rate));
	}
}

K_THREAD_DEFINE(sensor_thread_id, 1024, sensor_thread, NULL, NULL, NULL, 5, 0, 0);
