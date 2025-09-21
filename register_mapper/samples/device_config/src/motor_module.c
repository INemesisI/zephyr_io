/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#include <zephyr/register_mapper/register_channel.h>
#include "motor_module.h"
#include "sensor_module.h"

LOG_MODULE_REGISTER(motor_module, LOG_LEVEL_INF);

/* Forward declaration for the observer */
static void motor_handler_cb(const struct zbus_channel *chan);
ZBUS_LISTENER_DEFINE(motor_handler, motor_handler_cb);

/* Define the motor channel using register mapper macro */
REGISTER_CHAN_DEFINE(motor_chan,
	struct motor_config,
	NULL, /* No validator */
	NULL, /* user_data will be set to channel_state */
	ZBUS_OBSERVERS(motor_handler),
	(ZBUS_MSG_INIT(.status = MOTOR_STATUS_IDLE,
		       .direction = MOTOR_DIR_FORWARD,
		       .speed = 0,
		       .acceleration = 100,
		       .current = 0)));

/* Handler for motor configuration changes */
static void motor_handler_cb(const struct zbus_channel *chan)
{
	/* Channel is already locked by ZBUS - direct access is safe */
	struct motor_config *cfg = zbus_chan_msg(chan);

	LOG_INF("Motor config changed: speed=%u dir=%s accel=%d",
		cfg->speed,
		cfg->direction == MOTOR_DIR_FORWARD ? "FWD" : "REV",
		cfg->acceleration);

	/* Apply configuration to motor controller (simulated) */
	if (cfg->speed > 0) {
		cfg->status = MOTOR_STATUS_RUNNING;
		/* Simulate current draw based on speed */
		cfg->current = (cfg->speed / 10);  /* Simple linear model */
	} else {
		cfg->status = MOTOR_STATUS_IDLE;
		cfg->current = 0;
	}

	/* Safety check - limit speed if sensor alert is active */
	struct sensor_status sensor_status;
	if (zbus_chan_read(&sensor_status_chan, &sensor_status, K_NO_WAIT) == 0) {
		if (sensor_status.status & SENSOR_STATUS_ALERT) {
			if (cfg->speed > 5000) {
				LOG_WRN("Limiting motor speed due to sensor alert");
				cfg->speed = 5000;
			}
		}
	}
}