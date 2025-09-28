/*
 * Copyright (c) 2024 Zephyr IO Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MONITOR_MODULE_H_
#define MONITOR_MODULE_H_

#include <zephyr/kernel.h>
#include <zephyr_io/weave/weave.h>
#include "sensor_module.h"

/* Monitor statistics */
struct monitor_stats {
	uint32_t alerts_received;
	int32_t last_alert_value;
};

/* Signal handler */
extern struct weave_signal_handler monitor_on_threshold_exceeded;

/* Get monitor statistics */
struct monitor_stats *monitor_get_statistics(void);

#endif /* MONITOR_MODULE_H_ */