/*
 * Copyright (c) 2026 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Sensor module - demonstrates Weave Method RPC API
 *
 * This module provides three RPC methods:
 * - read_sensor: Read a sensor value
 * - set_config: Configure the sensor
 * - get_stats: Get sensor statistics
 */

#ifndef SENSOR_MODULE_H_
#define SENSOR_MODULE_H_

#include <weave/method.h>

/* ============================ Request/Response Types ============================ */

struct read_sensor_request {
	uint32_t channel;
};

struct read_sensor_response {
	int32_t value;
	uint32_t timestamp;
};

struct set_config_request {
	uint32_t sample_rate_ms;
	int32_t threshold;
	bool auto_sample;
};

/* Note: set_config has no response - demonstrates WV_VOID for response */

struct get_stats_response {
	uint32_t total_reads;
	uint32_t threshold_events;
	int32_t min_value;
	int32_t max_value;
};

/* ============================ Method Declarations ============================ */

/* Declare methods with type info for compile-time checking */
WEAVE_METHOD_DECLARE(sensor_read_sensor, struct read_sensor_request, struct read_sensor_response);
WEAVE_METHOD_DECLARE(sensor_set_config, struct set_config_request, WV_VOID);
WEAVE_METHOD_DECLARE(sensor_get_stats, WV_VOID, struct get_stats_response);

/* Thread entry point */
void sensor_thread(void *p1, void *p2, void *p3);

/* Signal sensor thread to stop */
void sensor_stop(void);

#endif /* SENSOR_MODULE_H_ */
