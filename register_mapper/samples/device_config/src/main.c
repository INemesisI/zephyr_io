/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Register Mapper Sample Application
 *
 * Demonstrates:
 * - Module-based configuration with ZBUS channels
 * - Register mapping for external interfaces
 * - Block write transactions
 * - Automatic change notifications
 */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>
#include "sensor_module.h"
#include "uart_handler.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Note: Sensor channels are now separated into status, config, and command */
/* The sensor module updates its own status via timer */

int main(void)
{
	LOG_INF("Register Mapper Sample Starting");

	/* Modules are initialized via SYS_INIT */
	/* Wait a bit for system to stabilize */
	k_sleep(K_MSEC(500));

	/* Run UART demo to show external register access */
	uart_demo_commands();

	/* Main loop - modules run independently via ZBUS */
	while (1) {
		k_sleep(K_SECONDS(10));
		/* System runs autonomously via ZBUS and timers */
	}

	return 0;
}