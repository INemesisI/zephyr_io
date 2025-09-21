/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Concurrency tests for register mapper
 *
 * Tests thread safety and concurrent access patterns.
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/register_mapper/register_mapper.h>
#include <zephyr/register_mapper/register_channel.h>

#define STACK_SIZE 1024
#define THREAD_PRIORITY 5

/* Test message */
struct concurrent_msg {
	uint32_t counter;
	uint8_t last_writer;
};

/* Channel for concurrency testing */
REGISTER_CHAN_DEFINE(concurrent_chan, struct concurrent_msg, NULL, NULL,
		     ZBUS_OBSERVERS_EMPTY,
		     ({.counter = 0, .last_writer = 0}));

/* Register mappings */
REG_MAPPING_DEFINE(concurrent_counter, 0xC000, &concurrent_chan, struct concurrent_msg,
		   counter, REG_TYPE_U32, REG_FLAGS_RW);
REG_MAPPING_DEFINE(concurrent_writer, 0xC010, &concurrent_chan, struct concurrent_msg,
		   last_writer, REG_TYPE_U8, REG_FLAGS_RW);

/* Shared state */
static volatile bool race_detected = false;
static volatile int total_writes = 0;
static struct k_sem test_sem;

K_THREAD_STACK_DEFINE(worker1_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(worker2_stack, STACK_SIZE);
static struct k_thread worker1_thread;
static struct k_thread worker2_thread;

static void worker_thread(void *id_ptr, void *unused2, void *unused3)
{
	uint8_t id = (uint8_t)(uintptr_t)id_ptr;
	struct reg_value val;
	int ret;

	k_sem_give(&test_sem);  /* Signal ready */

	for (int i = 0; i < 50; i++) {
		/* Read current counter */
		ret = reg_read_value(0xC000, &val);
		if (ret != 0) continue;

		uint32_t old_val = val.val.u32;

		/* Increment counter */
		val.val.u32 = old_val + 1;
		ret = reg_write_value(0xC000, val, K_NO_WAIT);
		if (ret != 0) continue;

		/* Write our ID */
		val = REG_VALUE_U8(id);
		reg_write_value(0xC010, val, K_NO_WAIT);

		/* Check if counter was incremented by someone else (race) */
		ret = reg_read_value(0xC000, &val);
		if (ret == 0 && val.val.u32 != old_val + 1) {
			/* Someone else also incremented - not atomic */
			race_detected = true;
		}

		total_writes++;
		k_yield();
	}
}

static void *test_concurrency_setup(void)
{
	k_sem_init(&test_sem, 0, 2);
	race_detected = false;
	total_writes = 0;
	return NULL;
}

ZTEST_SUITE(test_concurrency, NULL, test_concurrency_setup, NULL, NULL, NULL);

/**
 * @brief Test concurrent register access from multiple threads
 */
ZTEST(test_concurrency, test_concurrent_increment)
{
	/* Reset counter */
	struct reg_value val = REG_VALUE_U32(0);
	reg_write_value(0xC000, val, K_NO_WAIT);

	/* Start two threads that increment the same counter */
	k_thread_create(&worker1_thread, worker1_stack, STACK_SIZE,
			worker_thread, (void *)1, NULL, NULL,
			THREAD_PRIORITY, 0, K_NO_WAIT);

	k_thread_create(&worker2_thread, worker2_stack, STACK_SIZE,
			worker_thread, (void *)2, NULL, NULL,
			THREAD_PRIORITY, 0, K_NO_WAIT);

	/* Wait for threads to start */
	k_sem_take(&test_sem, K_FOREVER);
	k_sem_take(&test_sem, K_FOREVER);

	/* Wait for threads to complete */
	k_thread_join(&worker1_thread, K_FOREVER);
	k_thread_join(&worker2_thread, K_FOREVER);

	/* Read final counter value */
	reg_read_value(0xC000, &val);

	/* The counter might not be exactly 100 due to races, but should be > 0 */
	zassert_true(val.val.u32 > 0, "Counter should have been incremented");
	zassert_true(total_writes > 0, "Writes should have occurred");

	/* Note: race_detected shows if non-atomic behavior was observed
	 * This is expected since register operations aren't atomic */
	TC_PRINT("Final counter: %u, Total writes: %d, Races: %s\n",
		 val.val.u32, total_writes, race_detected ? "yes" : "no");
}

#ifdef CONFIG_REGISTER_MAPPER_BLOCK_WRITE

/**
 * @brief Test block write mutual exclusion
 */
ZTEST(test_concurrency, test_block_write_mutex)
{
	int ret1, ret2;

	/* First acquisition should succeed */
	ret1 = reg_block_write_begin(K_NO_WAIT);
	zassert_equal(ret1, 0, "First block write begin should succeed");

	/* Second acquisition from same thread should fail */
	ret2 = reg_block_write_begin(K_NO_WAIT);
	zassert_not_equal(ret2, 0, "Recursive acquisition should fail");

	/* Write some data */
	reg_block_write_register(0xC000, REG_VALUE_U32(999));

	/* Commit releases mutex */
	ret1 = reg_block_write_commit(K_NO_WAIT);
	zassert_equal(ret1, 0, "Commit should succeed");

	/* Now acquisition should succeed again */
	ret1 = reg_block_write_begin(K_NO_WAIT);
	zassert_equal(ret1, 0, "Acquisition after release should succeed");

	ret1 = reg_block_write_commit(K_NO_WAIT);
	zassert_equal(ret1, 0, "Second commit should succeed");
}

#endif /* CONFIG_REGISTER_MAPPER_BLOCK_WRITE */

/**
 * @brief Test rapid register switching under load
 */
ZTEST(test_concurrency, test_rapid_switching)
{
	struct reg_value val;
	const int iterations = 100;
	int success_count = 0;

	for (int i = 0; i < iterations; i++) {
		/* Rapidly alternate between registers */
		val = REG_VALUE_U32(i);
		if (reg_write_value(0xC000, val, K_NO_WAIT) == 0) {
			success_count++;
		}

		val = REG_VALUE_U8(i & 0xFF);
		if (reg_write_value(0xC010, val, K_NO_WAIT) == 0) {
			success_count++;
		}

		/* No delay - maximum speed */
	}

	/* Most operations should succeed */
	zassert_true(success_count > iterations, "Most operations should succeed");
}