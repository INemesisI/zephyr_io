/*
 * Copyright (c) 2026 Zephyr Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * Settings Table Sample
 *
 * Demonstrates:
 * - Distributed settings areas (motor, sensor) via iterable sections
 * - Complete separation: main.c has no knowledge of module internals
 * - Shell interface for reading/writing settings
 *
 * Shell commands:
 *   settings dump              - Show table structure
 *   settings read <addr> <len> - Read bytes from address
 *   settings write <addr> <b0> [b1] ... - Write bytes to address
 *   settings reg_read <addr>   - Read register value
 *   settings reg_write <addr> <value> - Write register value
 *   settings reset             - Reset all to defaults
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "settings_api.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("Settings Table Sample");
	LOG_INF("Areas: %zu, Initialized: %s", settings_area_count(),
		settings_is_initialized() ? "yes" : "no");
	LOG_INF("Use 'settings' shell commands to interact");

	return 0;
}
