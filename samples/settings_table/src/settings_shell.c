/*
 * Copyright (c) 2026 Zephyr Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * Settings shell commands
 */

#include <zephyr/shell/shell.h>
#include <stdlib.h>
#include <errno.h>
#include "settings_api.h"

static int cmd_settings_read(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 3) {
		shell_error(sh, "Usage: settings read <addr> <len>");
		return -EINVAL;
	}

	uint16_t addr = strtoul(argv[1], NULL, 0);
	size_t len = strtoul(argv[2], NULL, 0);

	if (len > 64) {
		shell_error(sh, "Max read length is 64 bytes");
		return -EINVAL;
	}

	uint8_t buf[64];
	int ret = settings_read(addr, buf, len);
	if (ret < 0) {
		shell_error(sh, "Read failed: %d", ret);
		return ret;
	}

	shell_hexdump(sh, buf, ret);
	return 0;
}

static int cmd_settings_write(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 3) {
		shell_error(sh, "Usage: settings write <addr> <byte0> [byte1] ...");
		return -EINVAL;
	}

	uint16_t addr = strtoul(argv[1], NULL, 0);
	size_t len = argc - 2;

	if (len > 64) {
		shell_error(sh, "Max write length is 64 bytes");
		return -EINVAL;
	}

	uint8_t buf[64];
	for (size_t i = 0; i < len; i++) {
		buf[i] = strtoul(argv[2 + i], NULL, 0);
	}

	int ret = settings_write(addr, buf, len);
	if (ret < 0) {
		shell_error(sh, "Write failed: %d", ret);
		return ret;
	}

	shell_print(sh, "Wrote %d bytes to 0x%04x", ret, addr);
	return 0;
}

static int cmd_settings_reg_read(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_error(sh, "Usage: settings reg_read <addr>");
		return -EINVAL;
	}

	uint16_t addr = strtoul(argv[1], NULL, 0);

	const struct setting_area *area = settings_find_area(addr);
	if (!area) {
		shell_error(sh, "No area at 0x%04x", addr);
		return -ENOENT;
	}

	const struct setting_reg *reg = settings_find_reg(addr);
	if (!reg) {
		shell_error(sh, "No register at 0x%04x", addr);
		return -ENOENT;
	}

	union setting_value val;
	int ret = settings_reg_get(area, reg, &val);
	if (ret < 0) {
		shell_error(sh, "Read failed: %d", ret);
		return ret;
	}

#if CONFIG_SETTINGS_REG_NAMES
	const char *name = reg->name ? reg->name : "?";
#else
	const char *name = "?";
#endif

	switch (reg->type) {
	case ST_U8:
		shell_print(sh, "%s = %u", name, val.u8);
		break;
	case ST_U16:
		shell_print(sh, "%s = %u", name, val.u16);
		break;
	case ST_U32:
		shell_print(sh, "%s = %u", name, val.u32);
		break;
	case ST_I8:
		shell_print(sh, "%s = %d", name, val.i8);
		break;
	case ST_I16:
		shell_print(sh, "%s = %d", name, val.i16);
		break;
	case ST_I32:
		shell_print(sh, "%s = %d", name, val.i32);
		break;
	case ST_F32:
		shell_print(sh, "%s = %.2f", name, (double)val.f32);
		break;
	case ST_F64:
		shell_print(sh, "%s = %.2f", name, val.f64);
		break;
	case ST_BOOL:
		shell_print(sh, "%s = %s", name, val.b ? "true" : "false");
		break;
	default:
		break;
	}

	return 0;
}

static int cmd_settings_reg_write(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 3) {
		shell_error(sh, "Usage: settings reg_write <addr> <value>");
		return -EINVAL;
	}

	uint16_t addr = strtoul(argv[1], NULL, 0);

	const struct setting_area *area = settings_find_area(addr);
	if (!area) {
		shell_error(sh, "No area at 0x%04x", addr);
		return -ENOENT;
	}

	const struct setting_reg *reg = settings_find_reg(addr);
	if (!reg) {
		shell_error(sh, "No register at 0x%04x", addr);
		return -ENOENT;
	}

	union setting_value val = {0};
	switch (reg->type) {
	case ST_U8:
		val.u8 = strtoul(argv[2], NULL, 0);
		break;
	case ST_U16:
		val.u16 = strtoul(argv[2], NULL, 0);
		break;
	case ST_U32:
		val.u32 = strtoul(argv[2], NULL, 0);
		break;
	case ST_I8:
		val.i8 = strtol(argv[2], NULL, 0);
		break;
	case ST_I16:
		val.i16 = strtol(argv[2], NULL, 0);
		break;
	case ST_I32:
		val.i32 = strtol(argv[2], NULL, 0);
		break;
	case ST_F32:
		val.f32 = strtof(argv[2], NULL);
		break;
	case ST_F64:
		val.f64 = strtod(argv[2], NULL);
		break;
	case ST_BOOL:
		val.b = (strtoul(argv[2], NULL, 0) != 0);
		break;
	default:
		shell_error(sh, "Unknown type");
		return -EINVAL;
	}

	int ret = settings_reg_set(area, reg, val);
	if (ret < 0) {
		shell_error(sh, "Write failed: %d", ret);
		return ret;
	}

	shell_print(sh, "OK");
	return 0;
}

static int cmd_settings_dump(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	settings_dump();
	return 0;
}

static int cmd_settings_reset(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int ret = settings_reset_defaults(NULL);
	if (ret < 0) {
		shell_error(sh, "Reset failed: %d", ret);
		return ret;
	}

	shell_print(sh, "Reset to defaults");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	settings_cmds,
	SHELL_CMD_ARG(read, NULL, "Read bytes: settings read <addr> <len>", cmd_settings_read, 3,
		      0),
	SHELL_CMD_ARG(write, NULL, "Write bytes: settings write <addr> <b0> [b1] ...",
		      cmd_settings_write, 3, 62),
	SHELL_CMD_ARG(reg_read, NULL, "Read register: settings reg_read <addr>",
		      cmd_settings_reg_read, 2, 0),
	SHELL_CMD_ARG(reg_write, NULL, "Write register: settings reg_write <addr> <value>",
		      cmd_settings_reg_write, 3, 0),
	SHELL_CMD(dump, NULL, "Dump settings table", cmd_settings_dump),
	SHELL_CMD(reset, NULL, "Reset to defaults", cmd_settings_reset), SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(settings, &settings_cmds, "Settings table commands", NULL);
