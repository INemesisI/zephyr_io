/*
 * Copyright (c) 2024 Zephyr IO Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_signals, LOG_LEVEL_INF);

/* Thread stacks for async tests */
K_THREAD_STACK_DEFINE(signal_test_stack_1, TEST_THREAD_STACK);
K_THREAD_STACK_DEFINE(signal_test_stack_2, TEST_THREAD_STACK);

/* Note: Signal connections should be done at compile time with WEAVE_SIGNAL_CONNECT.
 * For testing purposes, we're using runtime manipulation, but this is not
 * the recommended approach for production code. */

/* Setup for signal tests */
static void signal_test_setup(void)
{
	/* WARNING: Direct manipulation of signal handlers list is not recommended.
	 * In production code, use WEAVE_SIGNAL_CONNECT macro for compile-time wiring.
	 * This is only done here for test isolation. */
	sys_slist_init(&test_signal_basic.handlers);
	sys_slist_init(&test_signal_multi.handlers);
	sys_slist_init(&test_signal_empty.handlers);

	/* Reset signal tracker */
	reset_tracker(&signal_tracker);
}

/* Test basic signal emission to single handler */
ZTEST(weave_signal_suite, test_signal_emit_basic)
{
	struct test_event event = {.event_id = 0x1001, .data = 0xCAFE};
	int ret;

	signal_test_setup();

	/* WARNING: Direct manipulation for testing only */
	/* Use module without queue for immediate execution, or process the queue */
	test_handler_a.queue = NULL; /* Direct execution */
	sys_slist_append(&test_signal_basic.handlers, &test_handler_a.node);

	/* Emit signal */
	ret = weave_emit_signal(&test_signal_basic, &event);
	zassert_equal(ret, 0, "Signal emission failed: %d", ret);

	/* For direct handlers, execution is immediate */
	/* No need to wait or process queue */

	/* Verify handler was called */
	zassert_equal(atomic_get(&signal_tracker.call_count), 1, "Handler not called");
	zassert_equal(signal_tracker.last_event.event_id, 0x1001, "Event ID mismatch");
	zassert_equal(signal_tracker.last_event.data, 0xCAFE, "Event data mismatch");
	zassert_equal_ptr(signal_tracker.last_user_data, &test_user_data_a, "User data mismatch");
}

/* Test signal to multiple subscribers */
ZTEST(weave_signal_suite, test_signal_emit_multiple_handlers)
{
	struct test_event event = {.event_id = 0x2002, .data = 0xBEEF};
	int ret;

	signal_test_setup();

	/* WARNING: Direct manipulation for testing only */
	test_handler_a.queue = &test_msgq_a;
	test_handler_b.queue = &test_msgq_b;
	test_handler_c.queue = &test_msgq_c;

	sys_slist_append(&test_signal_multi.handlers, &test_handler_a.node);
	sys_slist_append(&test_signal_multi.handlers, &test_handler_b.node);
	sys_slist_append(&test_signal_multi.handlers, &test_handler_c.node);

	/* Emit signal */
	ret = weave_emit_signal(&test_signal_multi, &event);
	zassert_equal(ret, 0, "Signal emission failed: %d", ret);

	/* Wait for all handlers with proper synchronization */
	ret = wait_for_count(&signal_tracker.call_count, 3, K_MSEC(100));
	zassert_equal(ret, 0, "Not all handlers called within timeout");

	/* All handlers should be called */
	zassert_equal(atomic_get(&signal_tracker.call_count), 3, "All handlers should be called");
	zassert_equal(signal_tracker.last_event.event_id, 0x2002, "Event ID mismatch");
}

/* Test signal with no connected handlers */
ZTEST(weave_signal_suite, test_signal_emit_no_handlers)
{
	struct test_event event = {.event_id = 0x3003, .data = 0xDEAD};
	int ret;

	signal_test_setup();

	/* Ensure no handlers connected */
	zassert_true(sys_slist_is_empty(&test_signal_empty.handlers),
		     "Signal should have no handlers");

	/* Emit signal - should succeed with no handlers */
	ret = weave_emit_signal(&test_signal_empty, &event);
	zassert_equal(ret, 0, "Signal emission should succeed even with no handlers");

	/* No handlers should be called */
	zassert_equal(atomic_get(&signal_tracker.call_count), 0, "No handlers should be called");
}

/* Test direct handler execution (no queue) */
ZTEST(weave_signal_suite, test_signal_emit_direct)
{
	struct test_event event = {.event_id = 0x4004, .data = 0xF00D};
	int ret;

	signal_test_setup();

	/* Connect handler to module with no queue */
	test_handler_a.queue = NULL;
	sys_slist_append(&test_signal_basic.handlers, &test_handler_a.node);

	/* Emit signal - should execute directly */
	ret = weave_emit_signal(&test_signal_basic, &event);
	zassert_equal(ret, 0, "Direct signal emission failed: %d", ret);

	/* Handler should be called immediately */
	zassert_equal(atomic_get(&signal_tracker.call_count), 1, "Handler not called");
	zassert_equal(signal_tracker.last_event.event_id, 0x4004, "Event ID mismatch");
}

/* Test queued handler execution */
ZTEST(weave_signal_suite, test_signal_emit_queued)
{
	struct test_event event = {.event_id = 0x5005, .data = 0xABCD};
	int ret;

	signal_test_setup();

	/* Connect handler to module with queue */
	test_handler_a.queue = &test_msgq_a;
	sys_slist_append(&test_signal_basic.handlers, &test_handler_a.node);

	/* Emit signal */
	ret = weave_emit_signal(&test_signal_basic, &event);
	zassert_equal(ret, 0, "Queued signal emission failed: %d", ret);

	/* Wait for processing by the background processor thread */
	k_sleep(K_MSEC(10));

	/* Handler should be called */
	zassert_equal(atomic_get(&signal_tracker.call_count), 1, "Handler not called");
	zassert_equal(signal_tracker.last_event.event_id, 0x5005, "Event ID mismatch");
}

/* Test emit with NULL signal */
ZTEST(weave_signal_suite, test_signal_emit_null_signal)
{
	struct test_event event = {.event_id = 0x6006, .data = 0x1234};
	int ret;

	ret = weave_emit_signal(NULL, &event);
	zassert_equal(ret, -EINVAL, "Should return -EINVAL for NULL signal");
}

/* Test emit with NULL event data */
ZTEST(weave_signal_suite, test_signal_emit_null_event)
{
	int ret;

	signal_test_setup();

	/* Connect handler */
	test_handler_a.queue = &test_msgq_a;
	sys_slist_append(&test_signal_basic.handlers, &test_handler_a.node);

	/* Emit with NULL event - should still work */
	ret = weave_emit_signal(&test_signal_basic, NULL);
	zassert_equal(ret, 0, "Signal emission with NULL event should succeed");
}

/* Test handler with no parent module */
ZTEST(weave_signal_suite, test_signal_handler_null_module)
{
	struct test_event event = {.event_id = 0x7007, .data = 0x5678};
	int ret;

	signal_test_setup();

	/* Connect handler with no module */
	test_handler_no_queue.queue = NULL;
	sys_slist_append(&test_signal_basic.handlers, &test_handler_no_queue.node);

	/* Emit signal - handler with NULL queue executes immediately */
	ret = weave_emit_signal(&test_signal_basic, &event);
	zassert_equal(ret, 0, "Signal emission should succeed");

	/* Handler should be called immediately */
	zassert_equal(atomic_get(&signal_tracker.call_count), 1,
		      "Handler with no queue should execute immediately");
	zassert_equal(signal_tracker.last_event.event_id, 0x7007, "Event ID mismatch");
	zassert_equal(signal_tracker.last_event.data, 0x5678, "Event data mismatch");
}

/* Test signal emission from ISR context (K_NO_WAIT) */
ZTEST(weave_signal_suite, test_signal_emit_from_isr)
{
	struct test_event event = {.event_id = 0x8008, .data = 0x9ABC};
	int ret;

	signal_test_setup();

	/* Connect handler to module with queue */
	test_handler_a.queue = &test_msgq_a;
	sys_slist_append(&test_signal_basic.handlers, &test_handler_a.node);

	/* Fill the queue to test K_NO_WAIT behavior */
	int filled = fill_message_queue(&test_msgq_a);
	zassert_true(filled > 0, "Queue should be filled");

	/* Emit signal - uses K_NO_WAIT internally for signals */
	ret = weave_emit_signal(&test_signal_basic, &event);

	/* Should succeed even if queue is full (signals are fire-and-forget) */
	zassert_equal(ret, 0, "Signal emission should succeed");

	/* Drain queue */
	drain_message_queue(&test_msgq_a);
}

/* Thread control for mixed handler test */
static volatile bool mixed_processor_run = false;
static struct k_sem mixed_processor_done;

static void mixed_processor_thread(void *p1, void *p2, void *p3)
{
	struct k_msgq *queue = (struct k_msgq *)p1;
	int processed = 0;

	while (mixed_processor_run && processed < 10) {
		processed += weave_process_all_messages(queue);
		k_sleep(K_MSEC(1));
	}

	k_sem_give(&mixed_processor_done);
}

/* Test mixed direct and queued handlers */
ZTEST(weave_signal_suite, test_signal_mixed_handlers)
{
	struct test_event event = {.event_id = 0x9009, .data = 0xDEF0};
	struct k_thread processor_thread;
	k_tid_t tid;
	int ret;

	signal_test_setup();

	/* WARNING: Direct manipulation for testing only */
	test_handler_a.queue = NULL;         /* Direct */
	test_handler_b.queue = &test_msgq_a; /* Queued */

	sys_slist_append(&test_signal_multi.handlers, &test_handler_a.node);
	sys_slist_append(&test_signal_multi.handlers, &test_handler_b.node);

	/* Initialize synchronization */
	k_sem_init(&mixed_processor_done, 0, 1);
	mixed_processor_run = true;

	/* Start processor thread with controlled lifecycle */
	tid = k_thread_create(&processor_thread, signal_test_stack_2, TEST_THREAD_STACK,
			      mixed_processor_thread, &test_msgq_a, NULL, NULL,
			      TEST_THREAD_PRIORITY, 0, K_NO_WAIT);

	/* Emit signal */
	ret = weave_emit_signal(&test_signal_multi, &event);
	zassert_equal(ret, 0, "Mixed signal emission failed: %d", ret);

	/* Wait for processing */
	ret = wait_for_count(&signal_tracker.call_count, 2, K_MSEC(100));

	/* Stop processor cleanly */
	mixed_processor_run = false;
	k_sem_take(&mixed_processor_done, K_SECONDS(1));
	k_thread_join(&processor_thread, K_SECONDS(1));

	/* Both handlers should be called */
	zassert_true(atomic_get(&signal_tracker.call_count) >= 2, "Both handlers should be called");
}

ZTEST_SUITE(weave_signal_suite, NULL, NULL, common_test_setup, common_test_teardown, NULL);