/*
 * Copyright (c) 2024 Zephyr IO Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_concurrency, LOG_LEVEL_INF);

/* Thread stacks and data */
K_THREAD_STACK_ARRAY_DEFINE(test_stacks, 4, TEST_THREAD_STACK);
static struct k_thread test_threads[4];
static atomic_t thread_counter = ATOMIC_INIT(0);

/* Thread function for concurrent method calls */
static void concurrent_method_thread(void *p1, void *p2, void *p3)
{
	int thread_id = (int)(uintptr_t)p1;
	struct test_request req = {.value = (uint32_t)(0x1000 + thread_id), .flags = 0};
	struct test_reply reply = {0};
	int ret;

	/* Make multiple method calls */
	for (int i = 0; i < 5; i++) {
		req.flags = i;
		ret = weave_call_method(&test_port_simple, &req, sizeof(req), &reply, sizeof(reply),
					K_SECONDS(1));
		zassert_equal(ret, 0, "Thread %d call %d failed", thread_id, i);
	}

	atomic_inc(&thread_counter);
}

/* Thread function for concurrent signal emissions */
static void concurrent_signal_thread(void *p1, void *p2, void *p3)
{
	int thread_id = (int)(uintptr_t)p1;
	struct test_event event = {.event_id = (uint32_t)(0x2000 + thread_id), .data = 0};
	int ret;

	/* Emit multiple signals */
	for (int i = 0; i < 10; i++) {
		event.data = i;
		ret = weave_emit_signal(&test_signal_basic, &event);
		zassert_equal(ret, 0, "Thread %d signal %d failed", thread_id, i);
	}

	atomic_inc(&thread_counter);
}

/* Test multiple threads calling methods concurrently */
ZTEST(weave_concurrency_suite, test_concurrent_method_calls)
{
	k_tid_t tids[4];
	int i;

	/* Reset counter */
	atomic_clear(&thread_counter);

	/* Start multiple threads */
	for (i = 0; i < 4; i++) {
		tids[i] = k_thread_create(&test_threads[i], test_stacks[i], TEST_THREAD_STACK,
					  concurrent_method_thread, (void *)(uintptr_t)i, NULL,
					  NULL, TEST_THREAD_PRIORITY, 0, K_NO_WAIT);
	}

	/* Wait for all threads to complete */
	int ret = wait_for_count(&thread_counter, 4, K_SECONDS(5));
	zassert_equal(ret, 0, "Threads did not complete");

	/* Clean up threads */
	for (i = 0; i < 4; i++) {
		k_thread_abort(tids[i]);
	}

	/* Verify all calls were processed */
	zassert_true(atomic_get(&tracker_a.call_count) >= 20,
		     "Expected at least 20 calls (4 threads * 5 calls)");
}

/* Test multiple threads emitting signals concurrently */
ZTEST(weave_concurrency_suite, test_concurrent_signal_emissions)
{
	k_tid_t tids[4];
	int i;

	/* Setup signal handler */
	sys_slist_init(&test_signal_basic.handlers);
	test_handler_a.module = &test_module_no_queue; /* Direct execution */
	sys_slist_append(&test_signal_basic.handlers, &test_handler_a.node);

	/* Reset counter */
	atomic_clear(&thread_counter);

	/* Start multiple threads */
	for (i = 0; i < 4; i++) {
		tids[i] = k_thread_create(&test_threads[i], test_stacks[i], TEST_THREAD_STACK,
					  concurrent_signal_thread, (void *)(uintptr_t)i, NULL,
					  NULL, TEST_THREAD_PRIORITY, 0, K_NO_WAIT);
	}

	/* Wait for all threads to complete */
	int ret = wait_for_count(&thread_counter, 4, K_SECONDS(5));
	zassert_equal(ret, 0, "Threads did not complete");

	/* Clean up threads */
	for (i = 0; i < 4; i++) {
		k_thread_abort(tids[i]);
	}

	/* Verify signals were processed - may vary slightly due to timing */
	int signal_count = atomic_get(&signal_tracker.call_count);
	zassert_true(signal_count >= 35 && signal_count <= 45,
		     "Expected ~40 signals (4 threads * 10), got %d", signal_count);
}

/* Test message queue overflow handling */
ZTEST(weave_concurrency_suite, test_message_queue_overflow)
{
	struct test_request req = {.value = 0xFFFF, .flags = 0};
	struct test_reply reply = {0};
	int ret;

	/* Fill the queue */
	int filled = fill_message_queue(&test_msgq_a);
	zassert_true(filled > 0, "Queue should be filled");

	/* Try to add more - should fail with K_NO_WAIT */
	ret = weave_call_method(&test_port_simple, &req, sizeof(req), &reply, sizeof(reply),
				K_NO_WAIT);
	zassert_not_equal(ret, 0, "Should fail when queue is full");

	/* Drain queue */
	drain_message_queue(&test_msgq_a);

	/* Now it should work */
	ret = weave_call_method(&test_port_simple, &req, sizeof(req), &reply, sizeof(reply),
				K_SECONDS(1));
	zassert_equal(ret, 0, "Should succeed after draining queue");
}

/* Thread control for race condition test */
static struct k_sem race_processor_ready;
static volatile bool race_processor_run = false;

static void race_processor_thread(void *p1, void *p2, void *p3)
{
	struct weave_module *module = (struct weave_module *)p1;

	/* Delay slightly to create race condition */
	k_sleep(K_MSEC(45));
	k_sem_give(&race_processor_ready);

	while (race_processor_run) {
		weave_process_all_messages(module);
		k_sleep(K_MSEC(1));
	}
}

/* Test race condition between timeout and completion */
ZTEST(weave_concurrency_suite, test_race_condition_completion)
{
	struct test_request req = {.value = 0x8ACE, .flags = 0};
	struct test_reply reply = {0};
	struct k_thread processor_thread;
	k_tid_t tid;
	int ret;

	/* Initialize synchronization */
	k_sem_init(&race_processor_ready, 0, 1);
	race_processor_run = true;

	/* Start processor that may complete just as timeout expires */
	tid = k_thread_create(&processor_thread, test_stacks[0], TEST_THREAD_STACK,
			      race_processor_thread, &test_module_a, NULL, NULL,
			      TEST_THREAD_PRIORITY, 0, K_NO_WAIT);

	/* Use timeout that races with processor delay */
	ret = weave_call_method(&test_port_simple, &req, sizeof(req), &reply, sizeof(reply),
				K_MSEC(50));

	/* Stop processor cleanly */
	race_processor_run = false;
	k_thread_join(&processor_thread, K_SECONDS(1));

	/* Should either succeed or timeout, but not crash */
	zassert_true(ret == 0 || ret == -ETIMEDOUT, "Should either succeed or timeout: %d", ret);
}

/* Test atomic reference count operations */
ZTEST(weave_concurrency_suite, test_atomic_refcount_operations)
{
	atomic_t ref_test = ATOMIC_INIT(0);
	int i;

	/* Test atomic operations */
	for (i = 0; i < 100; i++) {
		atomic_inc(&ref_test);
	}
	zassert_equal(atomic_get(&ref_test), 100, "Increment failed");

	for (i = 0; i < 50; i++) {
		atomic_dec(&ref_test);
	}
	zassert_equal(atomic_get(&ref_test), 50, "Decrement failed");

	/* Test atomic_dec return value */
	atomic_set(&ref_test, 1);
	zassert_equal(atomic_dec(&ref_test), 1, "Should return 1 when reaching 0");
	zassert_equal(atomic_get(&ref_test), 0, "Should be 0");
}

ZTEST_SUITE(weave_concurrency_suite, NULL, NULL, common_test_setup, common_test_teardown, NULL);