/*
 * Copyright (c) 2024 Zephyr IO Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_method_calls, LOG_LEVEL_INF);

/* Thread stack for async tests */
K_THREAD_STACK_DEFINE(method_test_stack, TEST_THREAD_STACK);

/* Test basic synchronous method call with reply */
ZTEST(weave_method_suite, test_method_call_basic)
{
	struct test_request req = {.value = 0xCAFE, .flags = 0x01};
	struct test_reply reply = {0};
	int ret;

	/* Make method call */
	ret = weave_call_method(&test_port_simple, &req, sizeof(req), &reply, sizeof(reply),
				K_SECONDS(1));

	zassert_equal(ret, 0, "Method call failed: %d", ret);
	zassert_equal(reply.result, 0x12345678, "Reply result mismatch");
	zassert_equal(reply.status, 0, "Reply status should be 0");

	/* Verify handler was called */
	zassert_equal(atomic_get(&tracker_a.call_count), 1, "Handler not called");
	zassert_equal(tracker_a.last_request.value, 0xCAFE, "Request value mismatch");
}

/* Test method call without expecting reply (fire-and-forget) */
ZTEST(weave_method_suite, test_method_call_no_reply)
{
	struct test_request req = {.value = 0xBEEF, .flags = 0x02};
	int ret;

	/* Call with NULL reply buffer */
	ret = weave_call_method(&test_port_simple, &req, sizeof(req), NULL, 0, K_SECONDS(1));

	zassert_equal(ret, 0, "Method call failed: %d", ret);

	/* Verify handler was called */
	zassert_equal(atomic_get(&tracker_a.call_count), 1, "Handler not called");
	zassert_equal(tracker_a.last_request.value, 0xBEEF, "Request value mismatch");
}

/* Test call with NULL request (zero size) */
ZTEST(weave_method_suite, test_method_call_zero_size_request)
{
	struct test_reply reply = {0};
	int ret;

	/* Create a port that expects zero-size request */
	struct weave_method_port zero_req_port = {.name = "zero_req_port",
						  .target_method = &test_method_simple,
						  .request_size = 0,
						  .reply_size = sizeof(struct test_reply)};

	/* Temporarily adjust method size */
	size_t orig_size = test_method_simple.request_size;
	test_method_simple.request_size = 0;

	/* Call with NULL request and size 0 */
	ret = weave_call_method(&zero_req_port, NULL, 0, &reply, sizeof(reply), K_SECONDS(1));

	/* Restore original size */
	test_method_simple.request_size = orig_size;

	zassert_equal(ret, 0, "Method call failed: %d", ret);
	zassert_equal(atomic_get(&tracker_a.call_count), 1, "Handler not called");
}

/* Test call with NULL reply buffer but non-zero size */
ZTEST(weave_method_suite, test_method_call_zero_size_reply)
{
	struct test_request req = {.value = 0x1234, .flags = 0};
	int ret;

	/* Call with reply_size = 0 */
	ret = weave_call_method(&test_port_simple, &req, sizeof(req), NULL, 0, K_SECONDS(1));

	zassert_equal(ret, 0, "Method call failed: %d", ret);
	zassert_equal(atomic_get(&tracker_a.call_count), 1, "Handler not called");
}

/* Test direct execution (no queue) */
ZTEST(weave_method_suite, test_method_call_direct_execution)
{
	struct test_request req = {.value = 0xDEAD, .flags = 0};
	struct test_reply reply = {0};
	int ret;

	/* Set method to module with no queue */
	test_method_simple.module = &test_module_no_queue;

	/* Make method call - should execute directly */
	ret = weave_call_method(&test_port_simple, &req, sizeof(req), &reply, sizeof(reply),
				K_SECONDS(1));

	/* Restore module */
	test_method_simple.module = &test_module_a;

	zassert_equal(ret, 0, "Direct execution failed: %d", ret);
	zassert_equal(reply.result, 0x12345678, "Reply result mismatch");
	zassert_equal(atomic_get(&tracker_a.call_count), 1, "Handler not called");
}

/* Thread control for proper processor lifecycle */
static volatile bool processor_should_run = false;
static struct k_sem processor_started;

static void controlled_processor_thread(void *p1, void *p2, void *p3)
{
	struct weave_module *module = (struct weave_module *)p1;

	k_sem_give(&processor_started);

	while (processor_should_run) {
		weave_process_all_messages(module);
		k_sleep(K_MSEC(1));
	}
}

/* Test queued execution through message queue */
ZTEST(weave_method_suite, test_method_call_queued_execution)
{
	struct test_request req = {.value = 0xF00D, .flags = 0};
	struct test_reply reply = {0};
	struct k_thread processor_thread;
	k_tid_t tid;
	int ret;

	/* Ensure method has a queue */
	test_method_simple.module = &test_module_a;
	zassert_not_null(test_module_a.request_queue, "Module should have queue");

	/* Initialize synchronization */
	k_sem_init(&processor_started, 0, 1);
	processor_should_run = true;

	/* Start a thread to process messages with controlled lifecycle */
	tid = k_thread_create(&processor_thread, method_test_stack, TEST_THREAD_STACK,
			      controlled_processor_thread, &test_module_a, NULL, NULL,
			      TEST_THREAD_PRIORITY, 0, K_NO_WAIT);

	/* Wait for processor to start */
	k_sem_take(&processor_started, K_SECONDS(1));

	/* Make method call */
	ret = weave_call_method(&test_port_simple, &req, sizeof(req), &reply, sizeof(reply),
				K_SECONDS(1));

	/* Stop processor cleanly */
	processor_should_run = false;
	k_thread_join(&processor_thread, K_SECONDS(1));

	zassert_equal(ret, 0, "Queued execution failed: %d", ret);
	zassert_equal(reply.result, 0x12345678, "Reply result mismatch");
}

/* Test method call timeout */
ZTEST(weave_method_suite, test_method_call_timeout)
{
	struct test_request req = {.value = 0xABCD, .flags = 0};
	struct test_reply reply = {0};
	int ret;

	/* Use slow method that takes longer than timeout */
	ret = weave_call_method(&test_port_slow, &req, sizeof(req), &reply, sizeof(reply),
				K_MSEC(TEST_TIMEOUT_MS));

	zassert_equal(ret, -ETIMEDOUT, "Should timeout, got: %d", ret);
}

/* Test call through unconnected port */
ZTEST(weave_method_suite, test_method_call_unconnected_port)
{
	struct test_request req = {.value = 0x5555, .flags = 0};
	struct test_reply reply = {0};
	int ret;

	/* Ensure port is not connected */
	zassert_is_null(test_port_unconnected.target_method, "Port should be unconnected");

	ret = weave_call_method(&test_port_unconnected, &req, sizeof(req), &reply, sizeof(reply),
				K_SECONDS(1));

	zassert_equal(ret, -EINVAL, "Should return -EINVAL for unconnected port");
}

/* Test call with NULL port */
ZTEST(weave_method_suite, test_method_call_null_port)
{
	struct test_request req = {.value = 0x6666, .flags = 0};
	struct test_reply reply = {0};
	int ret;

	ret = weave_call_method(NULL, &req, sizeof(req), &reply, sizeof(reply), K_SECONDS(1));

	zassert_equal(ret, -EINVAL, "Should return -EINVAL for NULL port");
}

/* Test method with no parent module */
ZTEST(weave_method_suite, test_method_call_invalid_module)
{
	struct test_request req = {.value = 0x7777, .flags = 0};
	struct test_reply reply = {0};
	int ret;

	/* Create method with no module */
	struct weave_method orphan_method = {.name = "orphan",
					     .handler = mock_method_handler_simple,
					     .request_size = sizeof(struct test_request),
					     .reply_size = sizeof(struct test_reply),
					     .module = NULL};

	struct weave_method_port orphan_port = {.name = "orphan_port",
						.target_method = &orphan_method,
						.request_size = sizeof(struct test_request),
						.reply_size = sizeof(struct test_reply)};

	ret = weave_call_method(&orphan_port, &req, sizeof(req), &reply, sizeof(reply),
				K_SECONDS(1));

	zassert_equal(ret, -EINVAL, "Should return -EINVAL for method with no module");
}

/* Test request buffer too small */
ZTEST(weave_method_suite, test_method_call_buffer_too_small_request)
{
	struct test_request req = {.value = 0x8888, .flags = 0};
	struct test_reply reply = {0};
	int ret;

	/* Call with smaller request size than expected */
	ret = weave_call_method(&test_port_simple, &req, sizeof(req) - 1, &reply, sizeof(reply),
				K_SECONDS(1));

	zassert_equal(ret, -EINVAL, "Should return -EINVAL for small request buffer");
}

/* Test reply buffer too small */
ZTEST(weave_method_suite, test_method_call_buffer_too_small_reply)
{
	struct test_request req = {.value = 0x9999, .flags = 0};
	struct test_reply reply = {0};
	int ret;

	/* Call with smaller reply size than expected */
	ret = weave_call_method(&test_port_simple, &req, sizeof(req), &reply, sizeof(reply) - 1,
				K_SECONDS(1));

	zassert_equal(ret, -EINVAL, "Should return -EINVAL for small reply buffer");
}

/* Test NULL request with non-zero size */
ZTEST(weave_method_suite, test_method_call_null_request_nonzero_size)
{
	struct test_reply reply = {0};
	int ret;

	/* Call with NULL request but non-zero size */
	ret = weave_call_method(&test_port_simple, NULL, sizeof(struct test_request), &reply,
				sizeof(reply), K_SECONDS(1));

	zassert_equal(ret, -EINVAL, "Should return -EINVAL for NULL request with size");
}

/* Test handler returning error */
ZTEST(weave_method_suite, test_method_call_handler_error)
{
	struct test_request req = {.value = 0xAAAA, .flags = 0};
	struct test_reply reply = {0};
	int ret;

	/* Use method with error handler */
	ret = weave_call_method(&test_port_error, &req, sizeof(req), &reply, sizeof(reply),
				K_SECONDS(1));

	zassert_equal(ret, -TEST_ERROR_HANDLER, "Should return handler error");
	zassert_equal(atomic_get(&tracker_a.error_count), 1, "Error count mismatch");
}

/* Test K_NO_WAIT timeout */
ZTEST(weave_method_suite, test_method_call_no_wait)
{
	struct test_request req = {.value = 0xBBBB, .flags = 0};
	struct test_reply reply = {0};
	int ret;

	/* Fill the queue first */
	int filled = fill_message_queue(&test_msgq_a);
	zassert_true(filled > 0, "Queue should be filled");

	/* Try to call with K_NO_WAIT - should fail immediately */
	ret = weave_call_method(&test_port_simple, &req, sizeof(req), &reply, sizeof(reply),
				K_NO_WAIT);

	/* Drain queue */
	drain_message_queue(&test_msgq_a);

	zassert_not_equal(ret, 0, "Should fail with full queue and K_NO_WAIT");
}

ZTEST_SUITE(weave_method_suite, NULL, NULL, common_test_setup, common_test_teardown, NULL);