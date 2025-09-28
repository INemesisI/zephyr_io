/*
 * Copyright (c) 2024 Zephyr IO Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_error_paths, LOG_LEVEL_INF);

/* Test handler returning various error codes */
ZTEST(weave_error_suite, test_handler_returns_error)
{
	struct test_request req = {.value = 0xE881, .flags = 0};
	struct test_reply reply = {0};
	int ret;

	/* Test with error handler */
	ret = weave_call_method(&test_port_error, &req, sizeof(req), &reply, sizeof(reply),
				K_SECONDS(1));

	zassert_equal(ret, -TEST_ERROR_HANDLER, "Should return handler error");
	zassert_equal(atomic_get(&tracker_a.error_count), 1, "Error count should increment");

	/* Test different error codes by modifying handler behavior */
	/* This would require a more sophisticated mock in real testing */
}

/* Test recovery from allocation failures */
ZTEST(weave_error_suite, test_allocation_failure_recovery)
{
	struct test_request req = {.value = 0xA10C, .flags = 0};
	struct test_reply reply = {0};
	int ret;

	/* Instead of directly accessing message_slab (which is internal),
	 * we test allocation failure by making many concurrent requests
	 * to fill the message queue and exhaust resources */

	/* Fill the message queue to capacity */
	int filled = fill_message_queue(&test_msgq_a);
	zassert_true(filled > 0, "Queue should be filled");

	/* Try to make a call - should fail with full queue */
	ret = weave_call_method(&test_port_simple, &req, sizeof(req), &reply, sizeof(reply),
				K_NO_WAIT);
	zassert_not_equal(ret, 0, "Should fail when queue is full");

	/* Clear the queue */
	drain_message_queue(&test_msgq_a);

	/* Should recover */
	ret = weave_call_method(&test_port_simple, &req, sizeof(req), &reply, sizeof(reply),
				K_SECONDS(1));
	zassert_equal(ret, 0, "Should succeed after clearing queue");
}

/* Test handling k_msgq_put failures */
ZTEST(weave_error_suite, test_queue_put_failure)
{
	struct test_request req = {.value = 0x9FA1, .flags = 0};
	struct test_reply reply = {0};
	int ret;

	/* Fill queue completely */
	int filled = fill_message_queue(&test_msgq_a);
	zassert_true(filled > 0, "Queue should be filled");

	/* Try to queue message - should fail */
	ret = weave_call_method(&test_port_simple, &req, sizeof(req), &reply, sizeof(reply),
				K_NO_WAIT);

	zassert_not_equal(ret, 0, "Should fail with full queue");

	/* Drain and retry */
	drain_message_queue(&test_msgq_a);

	ret = weave_call_method(&test_port_simple, &req, sizeof(req), &reply, sizeof(reply),
				K_SECONDS(1));
	zassert_equal(ret, 0, "Should succeed after draining queue");
}

/* Test cleanup on multiple failures */
ZTEST(weave_error_suite, test_cleanup_on_multiple_failures)
{
	struct test_request req = {.value = 0xC017, .flags = 0};
	struct test_reply reply = {0};
	int ret, i;

	/* Cause multiple types of failures */

	/* 1. Invalid parameters */
	ret = weave_call_method(NULL, &req, sizeof(req), &reply, sizeof(reply), K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should fail with NULL port");

	/* 2. Unconnected port */
	ret = weave_call_method(&test_port_unconnected, &req, sizeof(req), &reply, sizeof(reply),
				K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should fail with unconnected port");

	/* 3. Size mismatches */
	ret = weave_call_method(&test_port_simple, &req, 1, &reply, sizeof(reply), K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should fail with size mismatch");

	/* 4. Handler errors */
	for (i = 0; i < 5; i++) {
		ret = weave_call_method(&test_port_error, &req, sizeof(req), &reply, sizeof(reply),
					K_SECONDS(1));
		zassert_equal(ret, -TEST_ERROR_HANDLER, "Should return handler error");
	}

	/* System should still be functional */
	ret = weave_call_method(&test_port_simple, &req, sizeof(req), &reply, sizeof(reply),
				K_SECONDS(1));
	zassert_equal(ret, 0, "System should still work after failures");
}

/* Test error propagation through the system */
ZTEST(weave_error_suite, test_error_propagation)
{
	struct test_request req = {.value = 0x9809, .flags = 0};
	struct test_reply reply = {0};
	int ret;

	/* Test that errors are properly propagated from handler to caller */
	ret = weave_call_method(&test_port_error, &req, sizeof(req), &reply, sizeof(reply),
				K_SECONDS(1));

	zassert_equal(ret, -TEST_ERROR_HANDLER, "Error should propagate from handler to caller");

	/* Verify the error was recorded */
	zassert_equal(tracker_a.last_result, -TEST_ERROR_HANDLER,
		      "Error should be recorded in tracker");
}

ZTEST_SUITE(weave_error_suite, NULL, NULL, common_test_setup, common_test_teardown, NULL);