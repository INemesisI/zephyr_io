/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Basic tests for register mapper
 *
 * Tests core functionality of the register mapper.
 */

#include <zephyr/ztest.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/register_mapper/register_mapper.h>
#include <zephyr/register_mapper/register_channel.h>

/* Test message structure */
struct basic_msg {
	uint32_t value;
	uint8_t status;
};

/* Test channel */
REGISTER_CHAN_DEFINE(basic_chan, struct basic_msg, NULL, NULL,
		     ZBUS_OBSERVERS_EMPTY,
		     ({.value = 0, .status = 0}));

/* Register mappings */
REG_MAPPING_DEFINE(basic_value, 0x1000, &basic_chan, struct basic_msg,
		   value, REG_TYPE_U32, REG_FLAGS_RW);
REG_MAPPING_DEFINE(basic_status, 0x1010, &basic_chan, struct basic_msg,
		   status, REG_TYPE_U8, REG_FLAGS_RW);

ZTEST_SUITE(test_basic, NULL, NULL, NULL, NULL, NULL);

/**
 * @brief Test basic read and write operations
 */
ZTEST(test_basic, test_read_write)
{
	struct reg_value val;
	int ret;

	/* Write value */
	val = REG_VALUE_U32(0x12345678);
	ret = reg_write_value(0x1000, val, K_NO_WAIT);
	zassert_equal(ret, 0, "Write should succeed");

	/* Read value back */
	ret = reg_read_value(0x1000, &val);
	zassert_equal(ret, 0, "Read should succeed");
	zassert_equal(val.type, REG_TYPE_U32, "Type should be U32");
	zassert_equal(val.val.u32, 0x12345678, "Value should match");

	/* Write status */
	val = REG_VALUE_U8(42);
	ret = reg_write_value(0x1010, val, K_NO_WAIT);
	zassert_equal(ret, 0, "Status write should succeed");

	/* Read status back */
	ret = reg_read_value(0x1010, &val);
	zassert_equal(ret, 0, "Status read should succeed");
	zassert_equal(val.type, REG_TYPE_U8, "Type should be U8");
	zassert_equal(val.val.u8, 42, "Status value should match");
}

/**
 * @brief Test invalid register access
 */
ZTEST(test_basic, test_invalid_access)
{
	struct reg_value val;
	int ret;

	/* Read non-existent register */
	ret = reg_read_value(0x9999, &val);
	zassert_equal(ret, -ENOENT, "Should return -ENOENT for non-existent register");

	/* Write to non-existent register */
	val = REG_VALUE_U32(123);
	ret = reg_write_value(0x9999, val, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should return -EINVAL for non-existent register");

	/* Type mismatch */
	val = REG_VALUE_U16(456);  /* Wrong type for U32 register */
	ret = reg_write_value(0x1000, val, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should return -EINVAL for type mismatch");
}