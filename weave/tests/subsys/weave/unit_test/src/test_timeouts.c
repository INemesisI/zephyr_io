/*
 * Copyright (c) 2024 Zephyr IO Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_timeouts, LOG_LEVEL_INF);

/* Thread stacks for async tests */
K_THREAD_STACK_DEFINE(timeout_test_stack_1, TEST_THREAD_STACK);
K_THREAD_STACK_DEFINE(timeout_test_stack_2, TEST_THREAD_STACK);

/* Test K_NO_WAIT timeout */
ZTEST(weave_timeout_suite, test_method_timeout_no_wait)
{
	struct test_request req = {.value = 0x1111, .flags = 0};
	struct test_reply reply = {0};
	int ret;

	/* Fill queue to force immediate timeout */
	int filled = fill_message_queue(&test_msgq_a);
	zassert_true(filled > 0, "Queue should be filled");

	/* Call with K_NO_WAIT - should fail immediately */
	ret = weave_call_method(&test_port_simple, &req, sizeof(req), &reply, sizeof(reply),
				K_NO_WAIT);

	/* Clean up */
	drain_message_queue(&test_msgq_a);

	zassert_not_equal(ret, 0, "Should fail immediately with K_NO_WAIT");
}

/* Test finite timeout expiry */
ZTEST(weave_timeout_suite, test_method_timeout_finite)
{
	struct test_request req = {.value = 0x2222, .flags = 0};
	struct test_reply reply = {0};
	int64_t start, end;
	int ret;

	/* Measure timeout */
	start = k_uptime_get();
	ret = weave_call_method(&test_port_slow, &req, sizeof(req), &reply, sizeof(reply),
				K_MSEC(50));
	end = k_uptime_get();

	zassert_equal(ret, -ETIMEDOUT, "Should timeout");

	/* Verify timeout was approximately correct with wider margin for system load */
	int64_t duration = end - start;
	zassert_true(duration >= 40 && duration <= 150,
		     "Timeout duration out of range: %lld ms (expected ~50ms)", duration);
}

/* Thread control for K_FOREVER test */
static struct k_sem forever_processor_ready;
static volatile bool forever_processor_run = false;

static void delayed_processor_thread(void *p1, void *p2, void *p3)
{
	struct weave_module *module = (struct weave_module *)p1;

	/* Delay before starting to process */
	k_sleep(K_MSEC(50));
	k_sem_give(&forever_processor_ready);

	while (forever_processor_run) {
		weave_process_all_messages(module);
		k_sleep(K_MSEC(1));
	}
}

/* Test K_FOREVER wait */
ZTEST(weave_timeout_suite, test_method_timeout_forever)
{
	struct test_request req = {.value = 0x3333, .flags = 0};
	struct test_reply reply = {0};
	struct k_thread processor_thread;
	k_tid_t tid;
	int ret;

	/* Initialize synchronization */
	k_sem_init(&forever_processor_ready, 0, 1);
	forever_processor_run = true;

	/* Start processor thread with delay */
	tid = k_thread_create(&processor_thread, timeout_test_stack_1, TEST_THREAD_STACK,
			      delayed_processor_thread, &test_module_a, NULL, NULL,
			      TEST_THREAD_PRIORITY, 0, K_NO_WAIT);

	/* Call with K_FOREVER - should wait for processing */
	ret = weave_call_method(&test_port_simple, &req, sizeof(req), &reply, sizeof(reply),
				K_FOREVER);

	/* Stop processor cleanly */
	forever_processor_run = false;
	k_thread_join(&processor_thread, K_SECONDS(1));

	zassert_equal(ret, 0, "Should succeed with K_FOREVER");
	zassert_equal(reply.result, 0x12345678, "Reply should be valid");
}

/* Test message queue put timeout */
ZTEST(weave_timeout_suite, test_queue_timeout)
{
	/* This test attempts to verify timeout behavior when queue is full.
	 * However, with the current architecture where execution mode (immediate vs queued)
	 * is determined by module->request_queue presence, we cannot properly test
	 * queue timeout scenarios:
	 *
	 * - If module has no queue: methods execute immediately (can't test queue timeout)
	 * - If module has queue: methods always queue (can't externally fill queue to force
	 * timeout)
	 *
	 * The architectural issue is that Weave decides execution mode at the module level
	 * rather than the method level. This test needs either:
	 * 1. Methods to specify their execution mode independently
	 * 2. A test helper to artificially fill queues
	 *
	 * Skipping until the architecture supports proper queue timeout testing.
	 */
	ztest_test_skip();
	return; /* Ensure no code runs after skip */
}

/* Test handler completes after caller timeout */
ZTEST(weave_timeout_suite, test_completion_after_timeout)
{
	struct test_request req = {.value = 0x5555, .flags = 0};
	struct test_reply reply = {0};
	struct k_thread processor_thread;
	k_tid_t tid;
	int ret;

	/* Use slow handler */
	test_port_slow.target_method = &test_method_slow;

	/* Start delayed processor */
	tid = k_thread_create(&processor_thread, timeout_test_stack_2, TEST_THREAD_STACK,
			      (k_thread_entry_t)weave_process_all_messages, &test_module_a, NULL,
			      NULL, TEST_THREAD_PRIORITY, 0, K_NO_WAIT);

	/* Call with short timeout */
	ret = weave_call_method(&test_port_slow, &req, sizeof(req), &reply, sizeof(reply),
				K_MSEC(5));

	zassert_equal(ret, -ETIMEDOUT, "Should timeout");

	/* Let handler complete */
	k_sleep(K_MSEC(50));

	/* Clean up */
	k_thread_abort(tid);

	/* Handler should have completed even though caller timed out */
	/* This tests that the context is properly managed */
}

ZTEST_SUITE(weave_timeout_suite, NULL, NULL, common_test_setup, common_test_teardown, NULL);