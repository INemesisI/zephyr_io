/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SENSOR_MODULE_H_
#define SENSOR_MODULE_H_

#include <stdint.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/sys/util.h>

/* Sensor status structure - Read-only runtime state */
struct sensor_status {
	uint8_t status; /* Runtime status bits */
	uint32_t data;  /* Current sensor reading */
};

/* Sensor configuration structure - Read-write persistent config */
struct sensor_config {
	uint32_t threshold; /* Alert threshold */
	uint32_t reserved;  /* Reserved for alignment/future use */
};

/* Sensor command structure - Write-only commands */
struct sensor_command {
	uint8_t control;     /* Control command bits */
	uint8_t reserved[3]; /* Reserved for alignment */
};

/* Control bits */
#define SENSOR_CTRL_START BIT(0)
#define SENSOR_CTRL_STOP  BIT(1)
#define SENSOR_CTRL_RESET BIT(2)

/* Status bits */
#define SENSOR_STATUS_READY   BIT(0)
#define SENSOR_STATUS_RUNNING BIT(1)
#define SENSOR_STATUS_ERROR   BIT(2)
#define SENSOR_STATUS_ALERT   BIT(3)

/* Export channels for use by register mapper and motor module */
ZBUS_CHAN_DECLARE(sensor_status_chan);
ZBUS_CHAN_DECLARE(sensor_config_chan);
ZBUS_CHAN_DECLARE(sensor_command_chan);

#endif /* SENSOR_MODULE_H_ */