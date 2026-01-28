/*
 * Copyright (c) 2026 Zephyr Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * Sensor settings module - self-contained, no public interface
 */

#include "settings_api.h"
#include <errno.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(sensor, LOG_LEVEL_INF);

/* Sensor settings base address */
#define SENSOR_BASE_ADDR 0x0200

/* Sensor configuration structure (private to this module) */
struct sensor_config {
	uint16_t sample_rate; /* Samples per second: 1-1000 */
	int16_t temp_offset;  /* Temperature calibration: -50 to +50 */
	float gain;           /* Amplifier gain: 0.1 to 10.0 */
	uint8_t channel_mask; /* Enabled channels bitmask: 0x01-0xFF */
	uint8_t _reserved[3]; /* Padding to 12 bytes */
};

/* Cross-register validation: channel_mask must have at least one bit set */
static int sensor_validate(struct weave_observable *obs, const void *new_value, void *user_data)
{
	ARG_UNUSED(obs);
	ARG_UNUSED(user_data);
	const struct sensor_config *cfg = new_value;

	if (cfg->channel_mask == 0) {
		return -EINVAL; /* At least one channel must be enabled */
	}
	return 0;
}

/* Owner handler - called on each change */
static void sensor_on_change(struct weave_observable *obs, void *user_data)
{
	ARG_UNUSED(user_data);
	struct sensor_config cfg;
	weave_observable_get_unchecked(obs, &cfg);
	LOG_INF("rate=%u offset=%d gain=%.2f mask=0x%02x", cfg.sample_rate, cfg.temp_offset,
		(double)cfg.gain, cfg.channel_mask);
}

/* Register definitions - pragma silences _Generic overflow warnings */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverflow"
static const struct setting_reg sensor_regs[] = {
	REGISTER(0x00, struct sensor_config, sample_rate, 1, 1000, 100, ST_FLAG_RW),
	REGISTER(0x02, struct sensor_config, temp_offset, -50, 50, 0, ST_FLAG_RW),
	REGISTER(0x04, struct sensor_config, gain, 0.1f, 10.0f, 1.0f, ST_FLAG_RW),
	REGISTER(0x08, struct sensor_config, channel_mask, 1, 0xFF, 0x0F, ST_FLAG_RW),
};
#pragma GCC diagnostic pop

/* Complete area definition - observable auto-generated, auto-registered */
SETTING_REG_AREA_DEFINE(sensor, SENSOR_BASE_ADDR, struct sensor_config, sensor_regs,
			ARRAY_SIZE(sensor_regs), sensor_on_change, sensor_validate);
