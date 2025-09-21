/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief UART protocol handler demonstrating external register access
 *
 * Simulates a UART-based protocol for register read/write operations,
 * demonstrating how external systems can interact with the device
 * through the register mapper interface.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/register_mapper/register_mapper.h>
#include <zephyr/register_mapper/register_types.h>
#include <string.h>
#include <stdio.h>
#include "sensor_module.h"

LOG_MODULE_REGISTER(uart_handler, LOG_LEVEL_INF);

/* Callback to print register info */
static int list_register_cb(const struct reg_mapping *map, void *user_data)
{
	const char *type_str[] = {"u8 ", "u16", "u32", "u64",
				   "i8 ", "i16", "i32", "i64"};
	const char *access_str = map->flags.readable ?
				 (map->flags.writable ? "RW" : "RO") :
				 (map->flags.writable ? "WO" : "--");

	struct reg_value value = {0};
	char value_str[20];

	/* Try to read the current value if readable */
	if (map->flags.readable && reg_read_value(map->address, &value) == 0) {
		/* Format value based on type */
		switch (map->type) {
		case REG_TYPE_U8:
			snprintf(value_str, sizeof(value_str), "%u", value.val.u8);
			break;
		case REG_TYPE_U16:
			snprintf(value_str, sizeof(value_str), "%u", value.val.u16);
			break;
		case REG_TYPE_U32:
			snprintf(value_str, sizeof(value_str), "%u", value.val.u32);
			break;
		case REG_TYPE_U64:
			snprintf(value_str, sizeof(value_str), "%llu", (unsigned long long)value.val.u64);
			break;
		case REG_TYPE_I8:
			snprintf(value_str, sizeof(value_str), "%d", value.val.i8);
			break;
		case REG_TYPE_I16:
			snprintf(value_str, sizeof(value_str), "%d", value.val.i16);
			break;
		case REG_TYPE_I32:
			snprintf(value_str, sizeof(value_str), "%d", value.val.i32);
			break;
		case REG_TYPE_I64:
			snprintf(value_str, sizeof(value_str), "%lld", (long long)value.val.i64);
			break;
		default:
			strcpy(value_str, "???");
			break;
		}
	} else {
		strcpy(value_str, "-");
	}

#ifdef CONFIG_REGISTER_MAPPER_NAMES
	const char *name = map->name ? map->name : "-";
#else
	const char *name = "-";
#endif

	LOG_INF("  0x%05X | %-4s | %3s | %3u | %9p | %-10s | %-20s",
		map->address,
		type_str[map->type],
		access_str,
		map->offset,
		map->channel,
		value_str,
		name);

	return 0;  /* Continue iteration */
}

/* Function to list all registered addresses */
static void reg_list_all(void)
{
	int count;

	LOG_INF("Register Map:");
	LOG_INF("  Address | Type | Acc | Off | Channel   | Value      | Name");
	LOG_INF("  --------|------|-----|-----|-----------|------------|----------------------");
	count = reg_foreach(list_register_cb, NULL);
	LOG_INF("  Total: %d registers", count);
}

/* Example callback: Count readable registers */
static int count_readable_cb(const struct reg_mapping *map, void *user_data)
{
	int *count = (int *)user_data;
	if (map->flags.readable) {
		(*count)++;
	}
	return 0;  /* Continue iteration */
}

/* Helper to find register name by address */
struct find_reg_by_addr_data {
	uint16_t addr;
	const char *name;
};

static int find_reg_by_addr_cb(const struct reg_mapping *map, void *user_data)
{
	struct find_reg_by_addr_data *data = (struct find_reg_by_addr_data *)user_data;
	if (map->address == data->addr) {
#ifdef CONFIG_REGISTER_MAPPER_NAMES
		data->name = map->name ? map->name : "unknown";
#else
		data->name = "unknown";
#endif
		return 1;  /* Stop iteration - found it */
	}
	return 0;  /* Continue iteration */
}

static const char *get_register_name(uint16_t addr)
{
	struct find_reg_by_addr_data data = {
		.addr = addr,
		.name = NULL
	};
	reg_foreach(find_reg_by_addr_cb, &data);
	return data.name ? data.name : "unknown";
}

/* Demonstrate custom register iteration */
static void demo_custom_register_iteration(void)
{
	int ret;

	/* Example: Count readable registers */
	int readable_count = 0;
	ret = reg_foreach(count_readable_cb, &readable_count);
	LOG_INF("Found %d readable registers out of %d total", readable_count, ret);
}

/* Protocol commands */
enum uart_cmd {
	CMD_READ_REG = 0x01,
	CMD_WRITE_REG = 0x02,
	CMD_BLOCK_WRITE = 0x03,
	CMD_LIST_REGS = 0x04,
};

/* Response codes */
enum uart_rsp {
	RSP_OK = 0x00,
	RSP_ERROR = 0x01,
	RSP_INVALID = 0x02,
};

/* Simulate UART command processing */
static void process_read_command(uint16_t addr)
{
	struct reg_value value;
	int ret = reg_read_value(addr, &value);

	if (ret == 0) {
		LOG_INF("READ 0x%04x (%s): Success", addr, get_register_name(addr));

		/* Log the value based on type */
		switch (value.type) {
		case REG_TYPE_U8:
			LOG_INF("  Value: 0x%02x (U8)", value.val.u8);
			break;
		case REG_TYPE_U16:
			LOG_INF("  Value: 0x%04x (U16)", value.val.u16);
			break;
		case REG_TYPE_U32:
			LOG_INF("  Value: 0x%08x (U32)", value.val.u32);
			break;
		case REG_TYPE_I16:
			LOG_INF("  Value: %d (I16)", value.val.i16);
			break;
		default:
			LOG_INF("  Value: (type %d)", value.type);
			break;
		}
	} else {
		LOG_ERR("READ 0x%04x (%s): Failed (%d)", addr, get_register_name(addr), ret);
	}
}

static void process_write_command(uint16_t addr, struct reg_value value)
{
	int ret = reg_write_value(addr, value, K_MSEC(100));

	if (ret == 0) {
		LOG_INF("WRITE 0x%04x (%s): Success", addr, get_register_name(addr));
	} else {
		LOG_ERR("WRITE 0x%04x (%s): Failed (%d)", addr, get_register_name(addr), ret);
	}
}

static void process_block_write_command(void)
{
	LOG_INF("BLOCK WRITE: Starting transaction");

	/* Begin block transaction */
	int ret = reg_block_write_begin(K_MSEC(500));
	if (ret != 0) {
		LOG_ERR("Failed to begin block write: %d", ret);
		return;
	}

	/* Simulate writing multiple registers */
	/* Example: Configure motor for operation */
	ret = reg_block_write_register(0x2010, REG_VALUE_U8(0));  /* Direction forward */
	if (ret == 0) {
		ret = reg_block_write_register(0x2012, REG_VALUE_U16(3000));  /* Speed 3000 RPM */
	}
	if (ret == 0) {
		ret = reg_block_write_register(0x2014, REG_VALUE_I16(200));  /* Acceleration 200 RPM/s */
	}

	/* Also update sensor threshold */
	if (ret == 0) {
		ret = reg_block_write_register(0x2000, REG_VALUE_U32(3500));  /* New threshold */
	}

	/* Commit all changes */
	if (ret == 0) {
		ret = reg_block_write_commit(K_MSEC(100));
		if (ret == 0) {
			LOG_INF("BLOCK WRITE: Committed successfully");
		} else {
			LOG_ERR("BLOCK WRITE: Commit failed (%d)", ret);
		}
	} else {
		/* Abort on error - commit will still unlock mutex */
		reg_block_write_commit(K_NO_WAIT);
		LOG_ERR("BLOCK WRITE: Aborted due to error");
	}
}

/* Demo function to simulate UART commands */
void uart_demo_commands(void)
{
	LOG_INF("=== UART Protocol Demo ===");

	/* Wait for system to stabilize */
	k_sleep(K_MSEC(100));

	/* Demo 1: List all available registers first */
	LOG_INF("Demo 1: Available registers in the system");
	reg_list_all();

	k_sleep(K_MSEC(500));

	/* Demo 2: Read some registers */
	LOG_INF("Demo 2: Reading registers");
	process_read_command(0x1000);  /* Sensor status */
	process_read_command(0x1004);  /* Motor status */
	process_read_command(0x3000);  /* Sensor data */

	k_sleep(K_MSEC(500));

	/* Demo 3: Write individual registers */
	LOG_INF("Demo 3: Writing individual registers");
	process_write_command(0x4000, REG_VALUE_U8(SENSOR_CTRL_START));  /* Start sensor command */
	k_sleep(K_MSEC(100));
	process_write_command(0x2012, REG_VALUE_U16(1500));  /* Set motor speed */

	k_sleep(K_MSEC(500));

	/* Demo 4: Block write transaction */
	LOG_INF("Demo 4: Block write transaction");
	process_block_write_command();

	k_sleep(K_MSEC(500));

	/* Demo 5: Read updated values */
	LOG_INF("Demo 5: Reading updated values");
	process_read_command(0x1000);  /* Sensor status */
	process_read_command(0x1004);  /* Motor status */
	process_read_command(0x2000);  /* Sensor threshold */
	process_read_command(0x2012);  /* Motor speed */

	/* Demo 6: Motor control sequence */
	LOG_INF("Demo 6: Motor control sequence");
	process_write_command(0x2010, REG_VALUE_U8(1));  /* Set reverse direction */
	k_sleep(K_MSEC(100));
	process_write_command(0x2014, REG_VALUE_I16(-50));  /* Set deceleration */
	k_sleep(K_MSEC(100));
	process_write_command(0x2012, REG_VALUE_U16(500));  /* Set slow speed */
	k_sleep(K_MSEC(100));

	/* Demo 7: Read final system state */
	LOG_INF("Demo 7: Reading final system state");
	process_read_command(0x1000);  /* Sensor status */
	process_read_command(0x1004);  /* Motor status */
	process_read_command(0x1006);  /* Motor current */
	process_read_command(0x3000);  /* Sensor data */

	/* Demo 8: Custom iteration over registers */
	LOG_INF("Demo 8: Custom register iteration");
	demo_custom_register_iteration();

	LOG_INF("=== UART Demo Complete ===");
	LOG_INF("PROJECT EXECUTION SUCCESSFUL");
}