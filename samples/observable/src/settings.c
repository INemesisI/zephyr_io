/*
 * Copyright (c) 2026 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>
#include "settings.h"

LOG_MODULE_REGISTER(settings, LOG_LEVEL_INF);

/* ============================ Shell Commands ============================ */

static int cmd_settings_set(const struct shell *sh, size_t argc, char **argv)
{
	if (argc != 2) {
		shell_error(sh, "Usage: settings set <rate_ms>");
		return -EINVAL;
	}

	uint32_t rate = strtoul(argv[1], NULL, 10);
	if (rate < 100 || rate > 10000) {
		shell_error(sh, "Rate must be between 100 and 10000 ms");
		return -EINVAL;
	}

	struct sensor_settings new_settings = {
		.sample_rate_ms = rate,
	};

	int ret = WEAVE_OBSERVABLE_SET(sensor_settings, &new_settings);
	if (ret < 0) {
		shell_error(sh, "Failed to update settings: %d", ret);
		return ret;
	}

	shell_print(sh, "Settings updated: sample_rate=%u ms (%d observers notified)", rate, ret);
	return 0;
}

static int cmd_settings_get(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	struct sensor_settings current;
	int ret = WEAVE_OBSERVABLE_GET(sensor_settings, &current);
	if (ret < 0) {
		shell_error(sh, "Failed to get settings: %d", ret);
		return ret;
	}

	shell_print(sh, "Current settings: sample_rate=%u ms", current.sample_rate_ms);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(settings_cmds,
			       SHELL_CMD(set, NULL, "Set sample rate: settings set <rate_ms>",
					 cmd_settings_set),
			       SHELL_CMD(get, NULL, "Get current settings", cmd_settings_get),
			       SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(settings, &settings_cmds, "Sensor settings commands", NULL);
