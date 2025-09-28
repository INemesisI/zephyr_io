/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Advanced tests for register mapper edge cases
 *
 * Tests advanced scenarios and edge cases that weren't covered
 * in the basic test suites.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr_io/register_mapper/register_mapper.h>
#include <zephyr_io/register_mapper/register_channel.h>

/* Message with all supported types */
struct all_types_msg {
	uint8_t u8;
	uint16_t u16;
	uint32_t u32;
	uint64_t u64;
	int8_t i8;
	int16_t i16;
	int32_t i32;
	int64_t i64;
};

/* Channel for testing all types */
REGISTER_CHAN_DEFINE(
	all_types_chan, struct all_types_msg, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
	({.u8 = 0, .u16 = 0, .u32 = 0, .u64 = 0, .i8 = 0, .i16 = 0, .i32 = 0, .i64 = 0}));

/* Define registers at maximum address range */
REG_MAPPING_DEFINE(max_addr_u8, 0xFFF8, &all_types_chan, struct all_types_msg, u8, REG_TYPE_U8,
		   REG_FLAGS_RW);
REG_MAPPING_DEFINE(max_addr_u16, 0xFFFA, &all_types_chan, struct all_types_msg, u16, REG_TYPE_U16,
		   REG_FLAGS_RW);
REG_MAPPING_DEFINE(max_addr_u32, 0xFFFC, &all_types_chan, struct all_types_msg, u32, REG_TYPE_U32,
		   REG_FLAGS_RW);

/* Define registers at minimum address */
REG_MAPPING_DEFINE(min_addr_u8, 0x0000, &all_types_chan, struct all_types_msg, i8, REG_TYPE_I8,
		   REG_FLAGS_RW);
REG_MAPPING_DEFINE(min_addr_u16, 0x0002, &all_types_chan, struct all_types_msg, i16, REG_TYPE_I16,
		   REG_FLAGS_RW);

ZTEST_SUITE(test_advanced, NULL, NULL, NULL, NULL, NULL);

/**
 * @brief Test register access at address boundaries
 */
ZTEST(test_advanced, test_address_boundaries)
{
	struct reg_value val;
	int ret;

	/* Test at minimum address (0x0000) */
	val = REG_VALUE_I8(-128);
	ret = reg_write_value(0x0000, val, K_NO_WAIT);
	zassert_equal(ret, 0, "Write at address 0 should work");

	ret = reg_read_value(0x0000, &val);
	zassert_equal(ret, 0, "Read at address 0 should work");
	zassert_equal(val.val.i8, -128, "Value at address 0 should be correct");

	/* Test near maximum address */
	val = REG_VALUE_U32(0xDEADBEEF);
	ret = reg_write_value(0xFFFC, val, K_NO_WAIT);
	zassert_equal(ret, 0, "Write at max address should work");

	ret = reg_read_value(0xFFFC, &val);
	zassert_equal(ret, 0, "Read at max address should work");
	zassert_equal(val.val.u32, 0xDEADBEEF, "Value at max address should be correct");
}

/**
 * @brief Test channel state consistency
 */
ZTEST(test_advanced, test_channel_state_consistency)
{
	struct reg_value val;
	int ret;

	/* Write through register interface */
	val = REG_VALUE_U16(0x1234);
	ret = reg_write_value(0xFFFA, val, K_NO_WAIT);
	zassert_equal(ret, 0);

	/* Access channel directly and verify consistency */
	ret = zbus_chan_claim(&all_types_chan, K_NO_WAIT);
	zassert_equal(ret, 0, "Should be able to claim channel");

	struct all_types_msg *msg = (struct all_types_msg *)zbus_chan_msg(&all_types_chan);
	zassert_equal(msg->u16, 0x1234, "Channel should reflect register write");

	/* Modify through channel */
	msg->u32 = 0xABCDEF00;
	zbus_chan_finish(&all_types_chan);

	/* Read through register interface */
	ret = reg_read_value(0xFFFC, &val);
	zassert_equal(ret, 0);
	zassert_equal(val.val.u32, 0xABCDEF00, "Register should reflect channel modification");
}

/* Helper callbacks for foreach test - moved outside to avoid nested functions */
static int immediate_stop_callback(const struct reg_mapping *mapping, void *user_data)
{
	return -1; /* Stop immediately */
}

struct search_context {
	uint16_t target_addr;
	int found_count;
};

static int search_callback(const struct reg_mapping *mapping, void *user_data)
{
	struct search_context *data = user_data;

	if (mapping->address == data->target_addr) {
		data->found_count++;
	}
	return 0;
}

/**
 * @brief Test foreach callback edge cases
 */
ZTEST(test_advanced, test_foreach_edge_cases)
{
	int ret;

	/* Test with NULL callback */
	ret = reg_foreach(NULL, NULL);
	zassert_equal(ret, -EINVAL, "Should return -EINVAL for NULL callback");

	/* Test callback that stops immediately */
	ret = reg_foreach(immediate_stop_callback, NULL);
	zassert_equal(ret, 1, "Should process exactly one register");

	/* Test callback that counts specific addresses */
	struct search_context search_data = {.target_addr = 0xFFFC, .found_count = 0};

	ret = reg_foreach(search_callback, &search_data);
	zassert_true(ret > 0, "Should process registers");
	zassert_equal(search_data.found_count, 1, "Should find exactly one register at 0xFFFC");
}

/**
 * @brief Test error propagation through the stack
 */
ZTEST(test_advanced, test_error_propagation)
{
	struct reg_value val;
	int ret;

	/* Test reading non-existent register */
	ret = reg_read_value(0x9999, &val);
	zassert_equal(ret, -ENOENT, "Should return -ENOENT for non-existent register");

	/* Test type mismatch */
	val = REG_VALUE_U16(123); /* Wrong type for U8 register */
	ret = reg_write_value(0xFFF8, val, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should return -EINVAL for type mismatch");

	/* Test permission violation on read-only */
	/* First, let's check if we have any RO registers from other tests */
	/* If not, this test is skipped as all our registers are RW */
}

#ifdef CONFIG_REGISTER_MAPPER_BLOCK_WRITE

/**
 * @brief Test block write with mixed operations
 */
ZTEST(test_advanced, test_block_write_mixed)
{
	struct reg_value val;
	int ret;

	/* Begin transaction */
	ret = reg_block_write_begin(K_NO_WAIT);
	zassert_equal(ret, 0, "Block write begin should succeed");

	/* Mix of valid and invalid operations */
	ret = reg_block_write_register(0xFFF8, REG_VALUE_U8(11));
	zassert_equal(ret, 0, "Valid write should succeed");

	ret = reg_block_write_register(0x9999, REG_VALUE_U32(22));
	zassert_equal(ret, -EINVAL, "Invalid address should fail");

	ret = reg_block_write_register(0xFFFC, REG_VALUE_U32(33));
	zassert_equal(ret, 0, "Valid write should succeed");

	/* Commit should succeed for valid operations */
	ret = reg_block_write_commit(K_NO_WAIT);
	zassert_equal(ret, 0, "Commit should succeed");

	/* Verify valid writes took effect */
	ret = reg_read_value(0xFFF8, &val);
	zassert_equal(ret, 0);
	zassert_equal(val.val.u8, 11, "First write should be applied");

	ret = reg_read_value(0xFFFC, &val);
	zassert_equal(ret, 0);
	zassert_equal(val.val.u32, 33, "Third write should be applied");
}

/**
 * @brief Test block write with no operations
 */
ZTEST(test_advanced, test_block_write_empty)
{
	int ret;

	/* Begin and immediately commit */
	ret = reg_block_write_begin(K_NO_WAIT);
	zassert_equal(ret, 0, "Begin should succeed");

	ret = reg_block_write_commit(K_NO_WAIT);
	zassert_equal(ret, 0, "Empty commit should succeed");
}

#endif /* CONFIG_REGISTER_MAPPER_BLOCK_WRITE */

/* Observer for testing notifications - defined at file scope */
static volatile int observer_count = 0;

static void test_observer(const struct zbus_channel *chan)
{
	observer_count++;
}

ZBUS_LISTENER_DEFINE(test_listener, test_observer);

/* Channel with observer */
REGISTER_CHAN_DEFINE(
	observed_chan, struct all_types_msg, NULL, NULL, ZBUS_OBSERVERS(test_listener),
	({.u8 = 0, .u16 = 0, .u32 = 0, .u64 = 0, .i8 = 0, .i16 = 0, .i32 = 0, .i64 = 0}));

REG_MAPPING_DEFINE(observed_reg, 0xE000, &observed_chan, struct all_types_msg, u32, REG_TYPE_U32,
		   REG_FLAGS_RW);

/* Registers with restricted permissions for testing */
REG_MAPPING_DEFINE(write_only_reg, 0x5000, &all_types_chan, struct all_types_msg, u32, REG_TYPE_U32,
		   REG_FLAGS_WO);

REG_MAPPING_DEFINE(read_only_reg, 0x5004, &all_types_chan, struct all_types_msg, u16, REG_TYPE_U16,
		   REG_FLAGS_RO);

/**
 * @brief Test observer interaction with register writes
 */
ZTEST(test_advanced, test_observer_interaction)
{
	struct reg_value val;
	int ret;

	observer_count = 0;

	/* Write should trigger observer */
	val = REG_VALUE_U32(42);
	ret = reg_write_value(0xE000, val, K_NO_WAIT);
	zassert_equal(ret, 0, "Write should succeed");

	/* Give time for observer to run */
	k_sleep(K_MSEC(10));

	/* Observer should have been called */
	zassert_true(observer_count > 0, "Observer should be notified on write");
}

/**
 * @brief Test register operations with interrupts disabled
 */
ZTEST(test_advanced, test_irq_disabled_ops)
{
	struct reg_value val;
	int ret;
	unsigned int key;

	/* Disable interrupts */
	key = irq_lock();

	/* Perform operations with IRQs disabled */
	val = REG_VALUE_U8(99);
	ret = reg_write_value(0xFFF8, val, K_NO_WAIT);
	zassert_equal(ret, 0, "Write with IRQs disabled should work");

	ret = reg_read_value(0xFFF8, &val);
	zassert_equal(ret, 0, "Read with IRQs disabled should work");
	zassert_equal(val.val.u8, 99, "Value should be correct");

	/* Re-enable interrupts */
	irq_unlock(key);
}

/**
 * @brief Test permission violations (Category 1)
 */
ZTEST(test_advanced, test_permission_violations)
{
	struct reg_value val;
	int ret;

	/* Test reading from write-only register */
	ret = reg_read_value(0x5000, &val);
	zassert_equal(ret, -EACCES, "Reading write-only register should return -EACCES");

	/* Test writing to read-only register */
	val = REG_VALUE_U16(0x1234);
	ret = reg_write_value(0x5004, val, K_NO_WAIT);
	zassert_equal(ret, -EACCES, "Writing read-only register should return -EACCES");

	/* First write to read-only register to set initial value */
	/* Note: We need to test that reads work on read-only */
	/* Initialize the read-only register through the channel */
	ret = zbus_chan_claim(&all_types_chan, K_NO_WAIT);
	zassert_equal(ret, 0, "Should claim channel");
	struct all_types_msg *msg = (struct all_types_msg *)zbus_chan_msg(&all_types_chan);
	msg->u16 = 0xABCD;
	zbus_chan_finish(&all_types_chan);

	/* Now verify we can read from read-only register */
	ret = reg_read_value(0x5004, &val);
	zassert_equal(ret, 0, "Reading read-only register should work");
	zassert_equal(val.val.u16, 0xABCD, "Read-only register value should be correct");

	/* Test writing to write-only register (should work) */
	val = REG_VALUE_U32(0xDEADBEEF);
	ret = reg_write_value(0x5000, val, K_NO_WAIT);
	zassert_equal(ret, 0, "Writing write-only register should work");
}

/**
 * @brief Test NULL parameter handling (Category 1)
 */
ZTEST(test_advanced, test_null_parameters)
{
	int ret;

	/* Test reg_read_value with NULL value pointer */
	ret = reg_read_value(0xFFF8, NULL);
	zassert_equal(ret, -EINVAL, "reg_read_value with NULL should return -EINVAL");

	/* Test reg_foreach with NULL callback */
	ret = reg_foreach(NULL, NULL);
	zassert_equal(ret, -EINVAL, "reg_foreach with NULL callback should return -EINVAL");
}

/**
 * @brief Test type validation (Category 2)
 */
ZTEST(test_advanced, test_type_validation)
{
	struct reg_value val;
	int ret;
	size_t size;

	/* Test type mismatch: write U16 value to U8 register */
	val = REG_VALUE_U16(0x1234); /* Wrong type for U8 register at 0xFFF8 */
	ret = reg_write_value(0xFFF8, val, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Type mismatch should return -EINVAL");

	/* Test type mismatch: write U64 value to U32 register */
	val = REG_VALUE_U64(0x123456789ABCDEF0);
	ret = reg_write_value(0xFFFC, val, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Type mismatch U64->U32 should return -EINVAL");

	/* Test invalid type value for reg_type_size */
	size = reg_type_size(REG_TYPE_COUNT); /* Invalid type */
	zassert_equal(size, 0, "Invalid type should return size 0");

	size = reg_type_size(REG_TYPE_COUNT + 10); /* Way out of range */
	zassert_equal(size, 0, "Out of range type should return size 0");

	/* Test valid types return correct sizes */
	size = reg_type_size(REG_TYPE_U8);
	zassert_equal(size, 1, "U8 should be 1 byte");

	size = reg_type_size(REG_TYPE_U32);
	zassert_equal(size, 4, "U32 should be 4 bytes");
}

/* Context for register sorting verification */
struct sort_verify_context {
	uint16_t prev_addr;
	int count;
	bool first;
	bool is_sorted;
};

/* Callback to verify sorting - defined outside test function to avoid nested function */
static int verify_sorted_cb_for_test(const struct reg_mapping *map, void *user_data)
{
	struct sort_verify_context *ctx = (struct sort_verify_context *)user_data;

	if (!ctx->first) {
		if (map->address <= ctx->prev_addr) {
			TC_PRINT("ERROR: Register 0x%04x comes after 0x%04x but should be "
				 "sorted!\n",
				 map->address, ctx->prev_addr);
			TC_PRINT("  This can happen if addresses use inconsistent "
				 "notation\n");
			TC_PRINT("  (mixing hex like 0x1000 with decimal like 4096)\n");
			ctx->is_sorted = false;
			return 1; /* Stop iteration */
		}

		/* Also verify addresses are properly spaced for testing */
		if (ctx->count < 10 && (map->address - ctx->prev_addr) < 2) {
			TC_PRINT("WARNING: Addresses 0x%04x and 0x%04x are very close\n",
				 ctx->prev_addr, map->address);
		}
	}

	ctx->prev_addr = map->address;
	ctx->first = false;
	ctx->count++;
	return 0;
}

/**
 * @brief Test that register mappings are sorted by address
 *
 * This test verifies that the linker-based sorting mechanism works correctly.
 * The REG_MAP_SORTED_NAME macro should result in iterable sections being
 * sorted by address automatically.
 *
 * IMPORTANT: All addresses in the test use consistent hex notation (0xNNNN).
 * Mixing hex and decimal would break sorting due to lexicographic ordering.
 */
ZTEST(test_advanced, test_register_sorting)
{
	struct sort_verify_context ctx = {
		.prev_addr = 0, .count = 0, .first = true, .is_sorted = true};

	/* Use the external callback function */
	reg_foreach(verify_sorted_cb_for_test, &ctx);

	zassert_true(ctx.is_sorted, "Register mappings must be sorted by address");
	zassert_true(ctx.count > 0, "Should have at least some registers to verify sorting");

	TC_PRINT("Verified %d registers are sorted by address (all using hex notation)\n",
		 ctx.count);

	/* Note: If this test fails, check that all register addresses use the same notation.
	 * Either use all hex (0xNNNN) or all decimal (NNNNN), but don't mix them!
	 */
}

#ifdef CONFIG_REGISTER_MAPPER_VALIDATION

/* External function declaration - internal function available for testing */
extern int _reg_validate_no_overlaps(void);

/* Channel for overlapping register test */
REGISTER_CHAN_DEFINE(
	overlap_test_chan, struct all_types_msg, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
	({.u8 = 0, .u16 = 0, .u32 = 0, .u64 = 0, .i8 = 0, .i16 = 0, .i32 = 0, .i64 = 0}));

/* These registers intentionally overlap for testing:
 * overlap_reg1: 0x7000-0x7003 (4 bytes, U32)
 * overlap_reg2: 0x7002-0x7003 (2 bytes, U16) - OVERLAPS with reg1!
 * overlap_reg3: 0x7006-0x7009 (4 bytes, U32)
 * overlap_reg4: 0x7009-0x700C (4 bytes, U32) - OVERLAPS with reg3!
 */
REG_MAPPING_DEFINE(overlap_reg1, 0x7000, &overlap_test_chan, struct all_types_msg, u32,
		   REG_TYPE_U32, REG_FLAGS_RW);

REG_MAPPING_DEFINE(overlap_reg2, 0x7002, &overlap_test_chan, struct all_types_msg, u16,
		   REG_TYPE_U16, REG_FLAGS_RW); /* OVERLAPS with reg1! */

REG_MAPPING_DEFINE(overlap_reg3, 0x7006, &overlap_test_chan, struct all_types_msg, u32,
		   REG_TYPE_U32, REG_FLAGS_RW);

REG_MAPPING_DEFINE(overlap_reg4, 0x7009, &overlap_test_chan, struct all_types_msg, u32,
		   REG_TYPE_U32, REG_FLAGS_RW); /* OVERLAPS with reg3! */

/**
 * @brief Test overlap detection (Category 3)
 */
ZTEST(test_advanced, test_overlap_detection)
{
	int ret;

	/* With the overlapping registers defined above,
	 * the validation should FAIL and return -EINVAL.
	 * We have 2 overlapping pairs:
	 * - reg1 (0x7000-0x7003) overlaps with reg2 (0x7002-0x7003)
	 * - reg3 (0x7006-0x7009) overlaps with reg4 (0x7009-0x700C)
	 */
	ret = _reg_validate_no_overlaps();
	zassert_equal(ret, -EINVAL, "Overlapping registers should fail validation");
}

#endif /* CONFIG_REGISTER_MAPPER_VALIDATION */

/* Mark the stack as non-executable for security */
#if defined(__GNUC__)
__asm__(".section .note.GNU-stack,\"\",@progbits");
#endif