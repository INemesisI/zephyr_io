/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#include <zephyr_io/register_mapper/register_channel.h>
#include "sensor_module.h"

LOG_MODULE_REGISTER(sensor_module, LOG_LEVEL_INF);

REGISTER_CHAN_DEFINE(sensor_status_chan, struct sensor_status, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		     (ZBUS_MSG_INIT(.status = SENSOR_STATUS_READY, .data = 0)));

REGISTER_CHAN_DEFINE(sensor_config_chan, struct sensor_config, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		     (ZBUS_MSG_INIT(.threshold = 2048, .reserved = 0)));

REGISTER_CHAN_DEFINE(sensor_command_chan, struct sensor_command, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		     (ZBUS_MSG_INIT(.control = 0, .reserved = {0})));

ZBUS_SUBSCRIBER_DEFINE(sensor_subscriber, 8);

/* Subscribe to both config and command channels */
ZBUS_CHAN_ADD_OBS(sensor_config_chan, sensor_subscriber, 0);
ZBUS_CHAN_ADD_OBS(sensor_command_chan, sensor_subscriber, 1);

/* Timer for periodic sampling */
static void sensor_timer_handler(struct k_timer *timer);
K_TIMER_DEFINE(sensor_timer, sensor_timer_handler, NULL);

/* Signal for timer events */
static struct k_poll_signal sensor_timer_signal = K_POLL_SIGNAL_INITIALIZER(sensor_timer_signal);

/* Poll events for the sensor thread (initialized at runtime) */
static struct k_poll_event sensor_events[2]; /* Timer + subscriber */

/* Timer handler - raises signal for thread */
static void sensor_timer_handler(struct k_timer *timer)
{
	k_poll_signal_raise(&sensor_timer_signal, 1);
}

/* Perform sensor sampling and update status */
static void sensor_sample(void)
{
	struct sensor_status *status;
	struct sensor_config config;
	uint8_t old_status;

	/* Claim status channel for atomic update */
	if (zbus_chan_claim(&sensor_status_chan, K_MSEC(100)) != 0) {
		LOG_ERR("Failed to claim sensor status channel");
		return;
	}

	/* Get direct pointer to status data (no copy needed) */
	status = (struct sensor_status *)zbus_chan_const_msg(&sensor_status_chan);

	/* Check if sensor is running */
	if (!(status->status & SENSOR_STATUS_RUNNING)) {
		zbus_chan_finish(&sensor_status_chan);
		return;
	}

	/* Read current config for threshold */
	if (zbus_chan_read(&sensor_config_chan, &config, K_MSEC(100)) != 0) {
		zbus_chan_finish(&sensor_status_chan);
		return;
	}

	old_status = status->status;

	/* Simulate sensor data */
	status->data += 10;

	/* Check threshold */
	if (status->data > config.threshold && config.threshold > 0) {
		if (!(status->status & SENSOR_STATUS_ALERT)) {
			status->status |= SENSOR_STATUS_ALERT;
			LOG_WRN("Sensor alert: data %u exceeds threshold %u", status->data,
				config.threshold);
		}
	} else {
		status->status &= ~SENSOR_STATUS_ALERT;
	}

	/* Finish the claim */
	zbus_chan_finish(&sensor_status_chan);

	/* Notify if status changed (alert triggered/cleared) */
	if (status->status != old_status) {
		zbus_chan_notify(&sensor_status_chan, K_NO_WAIT);
	}
}

/* Process sensor control commands */
static void sensor_process_command(void)
{
	struct sensor_command cmd;
	struct sensor_status *status;
	uint8_t old_status;

	/* Read command */
	if (zbus_chan_read(&sensor_command_chan, &cmd, K_MSEC(100)) != 0) {
		return;
	}

	if (cmd.control == 0) {
		return; /* No command to process */
	}

	LOG_INF("Sensor command: control=0x%02x", cmd.control);

	/* Clear command register (self-clearing) */
	struct sensor_command clear_cmd = {0};
	zbus_chan_pub(&sensor_command_chan, &clear_cmd, K_MSEC(100));

	/* Claim status channel for atomic update */
	if (zbus_chan_claim(&sensor_status_chan, K_MSEC(100)) != 0) {
		LOG_ERR("Failed to claim sensor status channel");
		return;
	}

	/* Get direct pointer to status data (no copy needed) */
	status = (struct sensor_status *)zbus_chan_const_msg(&sensor_status_chan);
	old_status = status->status;

	/* Handle START command */
	if (cmd.control & SENSOR_CTRL_START) {
		LOG_INF("Starting sensor sampling");
		status->status |= SENSOR_STATUS_RUNNING;
	}

	/* Handle STOP command */
	if (cmd.control & SENSOR_CTRL_STOP) {
		LOG_INF("Stopping sensor sampling");
		status->status &= ~SENSOR_STATUS_RUNNING;
		status->data = 0;
	}

	/* Handle RESET command */
	if (cmd.control & SENSOR_CTRL_RESET) {
		LOG_INF("Resetting sensor");
		status->data = 0;
		status->status = SENSOR_STATUS_READY;
	}

	/* Finish and notify if status changed */
	zbus_chan_finish(&sensor_status_chan);

	if (status->status != old_status) {
		zbus_chan_notify(&sensor_status_chan, K_NO_WAIT);
	}
}

/* Process sensor configuration changes */
static void sensor_process_config(void)
{
	struct sensor_config config;

	/* Read config */
	if (zbus_chan_read(&sensor_config_chan, &config, K_MSEC(100)) != 0) {
		return;
	}

	LOG_INF("Sensor config updated: threshold=%u", config.threshold);
	/* Config is just stored, no action needed */
}

/* Sensor processing thread */
static void sensor_thread(void *p1, void *p2, void *p3)
{
	const struct zbus_channel *chan;

	/* Set thread name */
	k_thread_name_set(k_current_get(), "sensor");

	LOG_INF("Sensor thread started");

	/* Initialize poll events */
	k_poll_event_init(&sensor_events[0], K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY,
			  &sensor_timer_signal);
	k_poll_event_init(&sensor_events[1], K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
			  K_POLL_MODE_NOTIFY_ONLY, sensor_subscriber.queue);

	/* Start the periodic timer (100ms interval) */
	k_timer_start(&sensor_timer, K_MSEC(100), K_MSEC(100));

	while (true) {
		/* Wait for timer signal or ZBUS messages */
		k_poll(sensor_events, ARRAY_SIZE(sensor_events), K_FOREVER);

		/* Handle timer event - perform sampling */
		if (sensor_events[0].state == K_POLL_STATE_SIGNALED) {
			k_poll_signal_reset(&sensor_timer_signal);
			sensor_sample();
		}

		/* Handle ZBUS notifications (config or command) */
		if (sensor_events[1].state == K_POLL_STATE_MSGQ_DATA_AVAILABLE) {
			while (zbus_sub_wait(&sensor_subscriber, &chan, K_NO_WAIT) == 0) {
				/* Process based on which channel sent the notification */
				if (chan == &sensor_config_chan) {
					sensor_process_config();
				} else if (chan == &sensor_command_chan) {
					sensor_process_command();
				}
			}
		}
	}
}

/* Define and start thread automatically */
K_THREAD_DEFINE(sensor_thread_id, 2048, sensor_thread, NULL, NULL, NULL, K_PRIO_PREEMPT(5), 0, 0);