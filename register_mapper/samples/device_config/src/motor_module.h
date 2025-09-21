/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MOTOR_MODULE_H_
#define MOTOR_MODULE_H_

#include <stdint.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/sys/util.h>

/* Direction values */
#define MOTOR_DIR_FORWARD  0
#define MOTOR_DIR_REVERSE  1

/* Motor configuration structure - stored in ZBUS channel */
struct motor_config {
	uint8_t status;       /* Runtime status */
	uint8_t direction;    /* Motor direction (0=forward, 1=reverse) */
	uint16_t speed;       /* RPM 0-10000 */
	int16_t acceleration; /* RPM/s (-1000 to 1000) */
	uint16_t current;     /* Current draw in mA */
};

/* Status bits */
#define MOTOR_STATUS_IDLE    BIT(0)
#define MOTOR_STATUS_RUNNING BIT(1)
#define MOTOR_STATUS_FAULT   BIT(2)

/* Export channel for use by register mapper */
ZBUS_CHAN_DECLARE(motor_chan);

#endif /* MOTOR_MODULE_H_ */