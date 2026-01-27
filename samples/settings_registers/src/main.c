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
#include "motor_settings.h"
#include "network_settings.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

/* ============================ Shell Commands ============================ */

static void print_field_value(const struct shell *sh, const struct setting_group *grp,
			      const struct setting_field *field, const void *buf)
{
	shell_fprintf(sh, SHELL_NORMAL, "%s.%s (%s) = ", grp->name, field->name,
		      setting_type_name(field->type));

	switch (field->type) {
	case SETTING_U8:
		shell_fprintf(sh, SHELL_NORMAL, "%u", *(uint8_t *)buf);
		break;
	case SETTING_U16:
		shell_fprintf(sh, SHELL_NORMAL, "%u", *(uint16_t *)buf);
		break;
	case SETTING_U32:
		shell_fprintf(sh, SHELL_NORMAL, "%u", *(uint32_t *)buf);
		break;
	case SETTING_U64:
		shell_fprintf(sh, SHELL_NORMAL, "%llu", *(uint64_t *)buf);
		break;
	case SETTING_I8:
		shell_fprintf(sh, SHELL_NORMAL, "%d", *(int8_t *)buf);
		break;
	case SETTING_I16:
		shell_fprintf(sh, SHELL_NORMAL, "%d", *(int16_t *)buf);
		break;
	case SETTING_I32:
		shell_fprintf(sh, SHELL_NORMAL, "%d", *(int32_t *)buf);
		break;
	case SETTING_I64:
		shell_fprintf(sh, SHELL_NORMAL, "%lld", *(int64_t *)buf);
		break;
	case SETTING_F32:
		shell_fprintf(sh, SHELL_NORMAL, "%.6g", (double)*(float *)buf);
		break;
	case SETTING_F64:
		shell_fprintf(sh, SHELL_NORMAL, "%.6g", *(double *)buf);
		break;
	case SETTING_BOOL:
		shell_fprintf(sh, SHELL_NORMAL, "%s", *(bool *)buf ? "true" : "false");
		break;
	default:
		shell_fprintf(sh, SHELL_NORMAL, "?");
		break;
	}
	shell_fprintf(sh, SHELL_NORMAL, "\n");
}

static int cmd_reg_read(const struct shell *sh, size_t argc, char **argv)
{
	if (argc != 2) {
		shell_error(sh, "Usage: reg read <addr>");
		return -EINVAL;
	}

	uint16_t reg = strtoul(argv[1], NULL, 0);
	struct weave_observable *obs;
	const struct setting_field *field;

	int ret = settings_find_reg(reg, &obs, &field);
	if (ret < 0) {
		shell_error(sh, "Read failed: %d", ret);
		return ret;
	}

	uint64_t value = 0;
	settings_field_get(obs, field, &value);
	print_field_value(sh, settings_get_group(obs), field, &value);
	return 0;
}

static int cmd_reg_write(const struct shell *sh, size_t argc, char **argv)
{
	if (argc != 3) {
		shell_error(sh, "Usage: reg write <addr> <value>");
		return -EINVAL;
	}

	uint16_t reg = strtoul(argv[1], NULL, 0);
	struct weave_observable *obs;
	const struct setting_field *field;

	int ret = settings_find_reg(reg, &obs, &field);
	if (ret < 0) {
		shell_error(sh, "Write failed: %d", ret);
		return ret;
	}

	uint64_t value = strtoul(argv[2], NULL, 0);
	ret = settings_field_set(obs, field, &value);
	if (ret < 0) {
		shell_error(sh, "Write failed: %d", ret);
		return ret;
	}

	print_field_value(sh, settings_get_group(obs), field, &value);
	return 0;
}

static int cmd_reg_bulkr(const struct shell *sh, size_t argc, char **argv)
{
	if (argc != 3) {
		shell_error(sh, "Usage: reg bulk <addr> <len>");
		return -EINVAL;
	}

	uint16_t reg = strtoul(argv[1], NULL, 0);
	size_t len = strtoul(argv[2], NULL, 0);

	if (len > 16) {
		shell_error(sh, "Max 16 bytes");
		return -EINVAL;
	}

	uint8_t buf[16] = {0};
	int ret = settings_read(reg, buf, len);
	if (ret < 0) {
		shell_error(sh, "Bulk read failed: %d", ret);
		return ret;
	}

	shell_fprintf(sh, SHELL_NORMAL, "reg 0x%04x [%d]: ", reg, ret);
	for (int i = 0; i < ret; i++) {
		shell_fprintf(sh, SHELL_NORMAL, "%02x", buf[i]);
	}
	shell_fprintf(sh, SHELL_NORMAL, "\n");
	return 0;
}

static int cmd_reg_bulkw(const struct shell *sh, size_t argc, char **argv)
{
	if (argc != 3) {
		shell_error(sh, "Usage: reg bulkw <addr> <hexstring>");
		return -EINVAL;
	}

	uint16_t reg = strtoul(argv[1], NULL, 0);
	const char *hex = argv[2];
	size_t hex_len = strlen(hex);

	if (hex_len % 2 != 0) {
		shell_error(sh, "Hex string must have even length");
		return -EINVAL;
	}

	size_t len = hex_len / 2;
	if (len > 16) {
		shell_error(sh, "Max 16 bytes");
		return -EINVAL;
	}

	uint8_t buf[16];
	for (size_t i = 0; i < len; i++) {
		char byte_str[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
		buf[i] = strtoul(byte_str, NULL, 16);
	}

	int ret = settings_write(reg, buf, len);
	if (ret < 0) {
		shell_error(sh, "Bulk write failed: %d", ret);
		return ret;
	}

	shell_print(sh, "Wrote %d bytes to 0x%04x", ret, reg);
	return 0;
}

static int cmd_reg_list(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	for (int i = 0; i < settings_get_count(); i++) {
		struct weave_observable *obs = settings_get_at(i);
		const struct setting_group *grp = settings_get_group(obs);

		shell_print(sh, "%s (base 0x%x):", grp->name, grp->base_reg);
		for (int j = 0; j < grp->field_count; j++) {
			const struct setting_field *f = &grp->fields[j];
			shell_print(sh, "  0x%03x: %s (%s)", grp->base_reg + f->reg, f->name,
				    setting_type_name(f->type));
		}
	}
	return 0;
}

static int cmd_get(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	for (int i = 0; i < settings_get_count(); i++) {
		struct weave_observable *obs = settings_get_at(i);
		const struct setting_group *grp = settings_get_group(obs);

		shell_fprintf(sh, SHELL_NORMAL, "%s: ", grp->name);
		for (int j = 0; j < grp->field_count; j++) {
			const struct setting_field *f = &grp->fields[j];
			uint32_t value = 0;
			settings_field_get(obs, f, &value);
			shell_fprintf(sh, SHELL_NORMAL, "%s=%u ", f->name, value);
		}
		shell_fprintf(sh, SHELL_NORMAL, "\n");
	}
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	reg_cmds, SHELL_CMD(read, NULL, "Read field: reg read <addr>", cmd_reg_read),
	SHELL_CMD(write, NULL, "Write field: reg write <addr> <value>", cmd_reg_write),
	SHELL_CMD(bulkr, NULL, "Bulk read: reg bulkr <addr> <len>", cmd_reg_bulkr),
	SHELL_CMD(bulkw, NULL, "Bulk write: reg bulkw <addr> <hex...>", cmd_reg_bulkw),
	SHELL_CMD(list, NULL, "List all registers", cmd_reg_list), SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(reg, &reg_cmds, "Register commands", NULL);
SHELL_CMD_REGISTER(get, NULL, "Get all settings", cmd_get);

/* ============================ Main ============================ */

int main(void)
{
	LOG_INF("Settings Register Sample");
	LOG_INF("Commands: reg list, reg read <addr>, reg write <a> <v>, get");

	settings_register(&motor_settings);
	settings_register(&network_settings);

	LOG_INF("Settings initialized.");
	return 0;
}
