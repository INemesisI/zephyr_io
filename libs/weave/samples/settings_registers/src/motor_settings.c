/*
 * Copyright (c) 2026 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "settings.h"
#include "motor_settings.h"

LOG_MODULE_DECLARE(app, LOG_LEVEL_INF);

/* Motor configuration structure - internal to this compilation unit */
struct motor_config {
	uint16_t speed;    /* RPM */
	uint16_t accel;    /* RPM/s */
	bool enabled;      /* 0=off, 1=on */
	uint8_t direction; /* 0=forward, 1=reverse */
	uint8_t status;    /* Read-only status: 0=idle, 1=running, 2=error */
	int8_t temp;       /* Temperature offset (-128 to 127) */
	int16_t position;  /* Position offset (-32768 to 32767) */
};

static const struct setting_field motor_fields[] = {
	SETTING_FIELD_FROM_TYPE(0x00, struct motor_config, speed, SETTING_FLAG_RW),
	SETTING_FIELD_FROM_TYPE(0x02, struct motor_config, accel, SETTING_FLAG_RW),
	SETTING_FIELD_FROM_TYPE(0x04, struct motor_config, enabled, SETTING_FLAG_RW),
	SETTING_FIELD_FROM_TYPE(0x05, struct motor_config, direction, SETTING_FLAG_RW),
	SETTING_FIELD_FROM_TYPE(0x06, struct motor_config, status, SETTING_FLAG_R),
	SETTING_FIELD_FROM_TYPE(0x07, struct motor_config, temp, SETTING_FLAG_RW),
	SETTING_FIELD_FROM_TYPE(0x08, struct motor_config, position, SETTING_FLAG_RW),
};

static const struct setting_group motor_group = {
	.name = "motor",
	.base_reg = 0x100,
	.size = sizeof(struct motor_config),
	.fields = motor_fields,
	.field_count = ARRAY_SIZE(motor_fields),
};

/* Validator: speed <= 10000, accel <= 5000, direction <= 1 */
static int motor_validate(struct weave_observable *obs, const void *new_value, void *user_data)
{
	ARG_UNUSED(obs);
	ARG_UNUSED(user_data);

	const struct motor_config *cfg = new_value;

	if (cfg->speed > 10000) {
		return -EINVAL;
	}
	if (cfg->accel > 5000) {
		return -EINVAL;
	}
	if (cfg->direction > 1) {
		return -EINVAL;
	}
	return 0;
}

/* Handler prints motor settings on change */
static void on_motor_changed(struct weave_observable *obs, void *user_data)
{
	ARG_UNUSED(user_data);

	struct motor_config cfg;
	weave_observable_get_unchecked(obs, &cfg);
	LOG_INF("[MOTOR] speed=%u accel=%u enabled=%u direction=%u", cfg.speed, cfg.accel,
		cfg.enabled, cfg.direction);
}

SETTING_OBSERVABLE_DEFINE(motor_settings, struct motor_config, on_motor_changed, WV_IMMEDIATE,
			  &motor_group, motor_validate);
