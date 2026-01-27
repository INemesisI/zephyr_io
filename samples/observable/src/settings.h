/*
 * Copyright (c) 2026 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Sensor settings - shared configuration for the observable sample
 */

#ifndef SETTINGS_H_
#define SETTINGS_H_

#include <weave/observable.h>

/* ============================ Data Types ============================ */

struct sensor_settings {
	uint32_t sample_rate_ms;
};

/* ============================ Observable Declaration ============================ */

/* Declare settings observable for external observers */
WEAVE_OBSERVABLE_DECLARE(sensor_settings, struct sensor_settings);

#endif /* SETTINGS_H_ */
