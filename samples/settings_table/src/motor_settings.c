/*
 * Copyright (c) 2026 Zephyr Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * Motor settings module - self-contained, no public interface
 */

#include "settings_api.h"
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(motor, LOG_LEVEL_INF);

/* Motor settings base address */
#define MOTOR_BASE_ADDR 0x0100

/* Motor configuration structure (private to this module) */
struct motor_config {
	uint16_t speed;    /* RPM: 0-10000 */
	uint16_t accel;    /* Acceleration: 0-5000 */
	uint8_t direction; /* 0=forward, 1=reverse */
	uint8_t enabled;   /* 0=off, 1=on */
	uint8_t status;    /* Read-only: 0=idle, 1=running, 2=error */
	uint8_t _reserved; /* Padding */
};

/* Cross-register validation: if enabled, speed must be > 0 */
static int motor_validate(struct weave_observable *obs, const void *new_value, void *user_data)
{
	ARG_UNUSED(obs);
	ARG_UNUSED(user_data);
	const struct motor_config *cfg = new_value;

	if (cfg->enabled && cfg->speed == 0) {
		return -EINVAL; /* Cannot enable with zero speed */
	}
	return 0;
}

/* Owner handler - called on each change */
static void motor_on_change(struct weave_observable *obs, void *user_data)
{
	ARG_UNUSED(user_data);
	struct motor_config cfg;
	weave_observable_get_unchecked(obs, &cfg);
	LOG_INF("speed=%u accel=%u dir=%u enabled=%u status=%u", cfg.speed, cfg.accel,
		cfg.direction, cfg.enabled, cfg.status);
}

/* Register definitions - pragma silences _Generic overflow warnings */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverflow"
static const struct setting_reg motor_regs[] = {
	REGISTER(0x00, struct motor_config, speed, 0, 10000, 1000, ST_FLAG_RW),
	REGISTER(0x02, struct motor_config, accel, 0, 5000, 500, ST_FLAG_RW),
	REGISTER(0x04, struct motor_config, direction, 0, 1, 0, ST_FLAG_RW),
	REGISTER(0x05, struct motor_config, enabled, 0, 1, 0, ST_FLAG_RW),
	REGISTER(0x06, struct motor_config, status, 0, 2, 0, ST_FLAG_R),
};
#pragma GCC diagnostic pop

/* Complete area definition - observable auto-generated, auto-registered */
SETTING_REG_AREA_DEFINE(motor, MOTOR_BASE_ADDR, struct motor_config, motor_regs,
			ARRAY_SIZE(motor_regs), motor_on_change, motor_validate);
