/*
 * Copyright (c) 2024 Zephyr IO Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SENSOR_MODULE_H_
#define SENSOR_MODULE_H_

#include <zephyr/kernel.h>
#include <zephyr_io/weave/weave.h>

/* Data structures following naming conventions */
struct read_sensor_request {
	uint32_t channel;
};

struct read_sensor_reply {
	int32_t value;
	uint32_t timestamp;
};

struct set_config_request {
	uint32_t sample_rate_ms;
	int32_t threshold;
	bool auto_sample;
};

struct set_config_reply {
	/* Returns old config */
	uint32_t sample_rate_ms;
	int32_t threshold;
	bool auto_sample;
};

struct get_stats_request {
	/* Empty request */
};

struct get_stats_reply {
	uint32_t total_reads;
	uint32_t threshold_events;
	uint32_t errors;
	int32_t min_value;
	int32_t max_value;
};

/* Signal event */
struct threshold_exceeded_event {
	int32_t value;
	int32_t threshold;
	uint32_t timestamp;
};

/* Module exports */
extern struct weave_module sensor_module;
extern struct k_msgq sensor_msgq;

/* Methods provided by sensor module */
extern struct weave_method sensor_read_sensor;
extern struct weave_method sensor_set_config;
extern struct weave_method sensor_get_stats;

/* Signals emitted by sensor module */
extern struct weave_signal threshold_exceeded;

/* Thread entry point */
void sensor_thread(void *p1, void *p2, void *p3);

#endif /* SENSOR_MODULE_H_ */