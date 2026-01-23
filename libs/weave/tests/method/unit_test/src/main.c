/*
 * Copyright (c) 2026 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>
#include <weave/method.h>

/* Test configuration constants */
#define TEST_QUEUE_SIZE 4

/* =============================================================================
 * Test Request/Response Types
 * =============================================================================
 */

struct test_request {
	int32_t value;
	uint8_t cmd;
};

struct test_response {
	int32_t result;
	uint8_t status;
};

/* =============================================================================
 * Test Handlers
 * =============================================================================
 */

/* Capture context for tracking handler invocations */
struct handler_capture {
	atomic_t call_count;
	void *last_user_data;
	struct test_request last_request;
};

static struct handler_capture capture = {0};

static void reset_capture(void)
{
	atomic_clear(&capture.call_count);
	capture.last_user_data = NULL;
	memset(&capture.last_request, 0, sizeof(capture.last_request));
}

/* Standard handler - returns success */
static int test_handler_success(const struct test_request *req, struct test_response *res,
				void *user_data)
{
	atomic_inc(&capture.call_count);
	capture.last_user_data = user_data;
	capture.last_request = *req;

	res->result = req->value * 2;
	res->status = 0;
	return 0;
}

/* Handler that returns error */
static int test_handler_error(const struct test_request *req, struct test_response *res,
			      void *user_data)
{
	ARG_UNUSED(user_data);

	atomic_inc(&capture.call_count);
	capture.last_request = *req;

	res->result = 0;
	res->status = 0xFF;
	return -EIO;
}

/* Handler with void request - no input, returns response */
static int test_handler_void_request(const void *req, struct test_response *res, void *user_data)
{
	ARG_UNUSED(req);
	ARG_UNUSED(user_data);

	atomic_inc(&capture.call_count);

	res->result = 999;
	res->status = 0x42;
	return 0;
}

/* Handler with void response - takes request, no output */
static int test_handler_void_response(const struct test_request *req, void *res, void *user_data)
{
	ARG_UNUSED(res);
	ARG_UNUSED(user_data);

	atomic_inc(&capture.call_count);
	capture.last_request = *req;

	return 0;
}

/* Handler with both void - no input, no output (trigger) */
static int test_handler_void_both(const void *req, void *res, void *user_data)
{
	ARG_UNUSED(req);
	ARG_UNUSED(res);
	ARG_UNUSED(user_data);

	atomic_inc(&capture.call_count);

	return 0;
}

/* =============================================================================
 * Test Infrastructure
 * =============================================================================
 */

/* Message queue for queued method */
WEAVE_MSGQ_DEFINE(method_queue, TEST_QUEUE_SIZE);

/* Tiny queue for overflow testing */
WEAVE_MSGQ_DEFINE(tiny_queue, 1);

/* Methods for testing */
WEAVE_METHOD_DEFINE(method_immediate, test_handler_success, WV_IMMEDIATE, NULL, struct test_request,
		    struct test_response);

WEAVE_METHOD_DEFINE(method_queued, test_handler_success, &method_queue, NULL, struct test_request,
		    struct test_response);

WEAVE_METHOD_DEFINE(method_error, test_handler_error, WV_IMMEDIATE, NULL, struct test_request,
		    struct test_response);

WEAVE_METHOD_DEFINE(method_tiny_queue, test_handler_success, &tiny_queue, NULL, struct test_request,
		    struct test_response);

/* Declare methods for type-safe macro test */
WEAVE_METHOD_DECLARE(method_immediate, struct test_request, struct test_response);

/* User data for user_data test */
static int user_data_value = 0x12345678;

/* Method with user_data set at compile time */
WEAVE_METHOD_DEFINE(method_with_user_data, test_handler_success, WV_IMMEDIATE, &user_data_value,
		    struct test_request, struct test_response);

/* Methods with WV_VOID for void request/response testing */
WEAVE_METHOD_DEFINE(method_void_request, test_handler_void_request, WV_IMMEDIATE, NULL, WV_VOID,
		    struct test_response);

WEAVE_METHOD_DEFINE(method_void_response, test_handler_void_response, WV_IMMEDIATE, NULL,
		    struct test_request, WV_VOID);

WEAVE_METHOD_DEFINE(method_void_both, test_handler_void_both, WV_IMMEDIATE, NULL, WV_VOID, WV_VOID);

/* Declare WV_VOID methods for type-safe macro test */
WEAVE_METHOD_DECLARE(method_void_request, WV_VOID, struct test_response);
WEAVE_METHOD_DECLARE(method_void_response, struct test_request, WV_VOID);
WEAVE_METHOD_DECLARE(method_void_both, WV_VOID, WV_VOID);

/* Helper thread for queue processing tests */
static struct k_sem queue_helper_start;
static struct k_sem queue_helper_done;
static struct k_msgq *queue_to_process;

static void queue_helper_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (1) {
		/* Wait for signal to process queue */
		k_sem_take(&queue_helper_start, K_FOREVER);

		/* Small delay to ensure caller is blocked waiting for queue space */
		k_sleep(K_MSEC(10));

		/* Process messages to make room in queue */
		weave_process_messages(queue_to_process, K_NO_WAIT);

		/* Signal done */
		k_sem_give(&queue_helper_done);
	}
}

K_THREAD_DEFINE(queue_helper_tid, 1024, queue_helper_thread, NULL, NULL, NULL, 5, 0, 0);

/* =============================================================================
 * Test Setup/Teardown
 * =============================================================================
 */

static void test_setup(void *fixture)
{
	ARG_UNUSED(fixture);

	reset_capture();
	k_msgq_purge(&method_queue);
	k_msgq_purge(&tiny_queue);
	k_sem_init(&queue_helper_start, 0, 1);
	k_sem_init(&queue_helper_done, 0, 1);
}

static void test_teardown(void *fixture)
{
	ARG_UNUSED(fixture);

	/* Process any remaining messages */
	while (weave_process_messages(&method_queue, K_NO_WAIT) > 0) {
	}
	while (weave_process_messages(&tiny_queue, K_NO_WAIT) > 0) {
	}
}

ZTEST_SUITE(weave_method_unit_test, NULL, NULL, test_setup, test_teardown, NULL);

/* =============================================================================
 * Basic Functionality Tests
 * =============================================================================
 */

ZTEST(weave_method_unit_test, test_method_call_basic)
{
	struct test_request req = {.value = 42, .cmd = 1};
	struct test_response res = {0};
	int ret;

	ret = weave_method_call_unchecked(&method_immediate, &req, sizeof(req), &res, sizeof(res));

	zassert_equal(ret, 0, "Call should succeed");
	zassert_equal(atomic_get(&capture.call_count), 1, "Handler should be called once");
	zassert_equal(capture.last_request.value, 42, "Request value should match");
	zassert_equal(capture.last_request.cmd, 1, "Request cmd should match");
	zassert_equal(res.result, 84, "Response result should be 2x input");
	zassert_equal(res.status, 0, "Response status should be 0");
}

ZTEST(weave_method_unit_test, test_method_handler_error)
{
	struct test_request req = {.value = 10, .cmd = 0xFF};
	struct test_response res = {0};
	int ret;

	ret = weave_method_call_unchecked(&method_error, &req, sizeof(req), &res, sizeof(res));

	zassert_equal(ret, -EIO, "Should return handler's error code");
	zassert_equal(atomic_get(&capture.call_count), 1, "Handler should be called");
	zassert_equal(res.status, 0xFF, "Response should be set before return");
}

/* =============================================================================
 * Input Validation Tests
 * =============================================================================
 */

ZTEST(weave_method_unit_test, test_method_call_null_method)
{
	struct test_request req = {.value = 1, .cmd = 0};
	struct test_response res = {0};
	int ret;

	ret = weave_method_call_unchecked(NULL, &req, sizeof(req), &res, sizeof(res));

	zassert_equal(ret, -EINVAL, "Should reject NULL method");
	zassert_equal(atomic_get(&capture.call_count), 0, "Handler should not be called");
}

ZTEST(weave_method_unit_test, test_method_call_request_too_small)
{
	struct test_request req = {.value = 1, .cmd = 0};
	struct test_response res = {0};
	int ret;

	/* Pass request size smaller than expected */
	ret = weave_method_call_unchecked(&method_immediate, &req, sizeof(req) - 1, /* Too small! */
					  &res, sizeof(res));

	zassert_equal(ret, -EINVAL, "Should reject undersized request");
	zassert_equal(atomic_get(&capture.call_count), 0, "Handler should not be called");
}

ZTEST(weave_method_unit_test, test_method_call_response_too_small)
{
	struct test_request req = {.value = 1, .cmd = 0};
	struct test_response res = {0};
	int ret;

	/* Pass response size smaller than expected */
	ret = weave_method_call_unchecked(&method_immediate, &req, sizeof(req), &res,
					  sizeof(res) - 1 /* Too small! */);

	zassert_equal(ret, -EINVAL, "Should reject undersized response");
	zassert_equal(atomic_get(&capture.call_count), 0, "Handler should not be called");
}

ZTEST(weave_method_unit_test, test_method_call_larger_sizes_ok)
{
	/* Larger buffers should be accepted */
	struct {
		struct test_request req;
		uint8_t extra[16];
	} large_req = {.req = {.value = 77, .cmd = 3}};

	struct {
		struct test_response res;
		uint8_t extra[16];
	} large_res = {0};

	int ret;

	ret = weave_method_call_unchecked(
		&method_immediate, &large_req.req, sizeof(large_req), /* Larger than expected */
		&large_res.res, sizeof(large_res) /* Larger than expected */);

	zassert_equal(ret, 0, "Larger buffers should be accepted");
	zassert_equal(large_res.res.result, 154, "Result should be 2x77");
}

/* =============================================================================
 * Dispatch Edge Case Tests
 * =============================================================================
 */

ZTEST(weave_method_unit_test, test_dispatch_null_method)
{
	/* Directly test weave_method_dispatch with NULL method but valid ctx */
	struct weave_method_context ctx = {0};

	k_sem_init(&ctx.completion, 0, 1);
	ctx.result = 0;

	/* Should handle gracefully, set result to -EINVAL, and signal */
	weave_method_dispatch(&ctx, NULL);

	/* Verify ctx.result is set to -EINVAL */
	zassert_equal(ctx.result, -EINVAL, "Result should be -EINVAL for NULL method");

	/* Semaphore should be signaled (count > 0 or take succeeds) */
	int ret = k_sem_take(&ctx.completion, K_NO_WAIT);
	zassert_equal(ret, 0, "Completion should be signaled");
}

ZTEST(weave_method_unit_test, test_dispatch_null_ctx)
{
	/* Directly test weave_method_dispatch with NULL ctx */
	/* This should return early without crashing */
	weave_method_dispatch(NULL, &method_immediate);

	/* Handler should NOT be called */
	zassert_equal(atomic_get(&capture.call_count), 0, "Handler should not be called");
}

ZTEST(weave_method_unit_test, test_dispatch_both_null)
{
	/* Both NULL - should return early without crashing */
	weave_method_dispatch(NULL, NULL);

	/* Just verify no crash */
	zassert_true(true, "Should not crash with both NULL");
}

/* =============================================================================
 * User Data Test
 * =============================================================================
 */

ZTEST(weave_method_unit_test, test_method_with_user_data)
{
	struct test_request req = {.value = 5, .cmd = 0};
	struct test_response res = {0};
	int ret;

	/* user_data is set at compile time via WEAVE_METHOD_DEFINE */
	ret = weave_method_call_unchecked(&method_with_user_data, &req, sizeof(req), &res,
					  sizeof(res));

	zassert_equal(ret, 0, "Call should succeed");
	zassert_equal(capture.last_user_data, &user_data_value,
		      "User data should be passed to handler");
}

/* =============================================================================
 * Type-Safe Macro Test
 * =============================================================================
 */

ZTEST(weave_method_unit_test, test_method_call_macro)
{
	struct test_request req = {.value = 33, .cmd = 7};
	struct test_response res = {0};
	int ret;

	/* Use type-safe macro - this verifies compile-time type checking */
	ret = WEAVE_METHOD_CALL(method_immediate, &req, &res);

	zassert_equal(ret, 0, "Macro call should succeed");
	zassert_equal(res.result, 66, "Result should be 2x33");
	zassert_equal(atomic_get(&capture.call_count), 1, "Handler called once");
}

/* =============================================================================
 * Queue Blocking Test - Methods block until queue has space
 * =============================================================================
 */

ZTEST(weave_method_unit_test, test_method_call_blocks_until_queue_space)
{
	/* Test that method calls block when queue is full and complete when space becomes available
	 */

	/* Fill the tiny queue (size 1) with a real method call context */
	struct test_request blocking_req = {.value = 11, .cmd = 1};
	struct test_response blocking_res = {0};
	struct weave_method_context blocking_ctx;

	/* Queue an async call that will fill the queue */
	int ret = WEAVE_METHOD_CALL_ASYNC(method_tiny_queue, &blocking_req, &blocking_res,
					  &blocking_ctx);
	zassert_equal(ret, 0, "First async call should succeed");
	zassert_equal(k_msgq_num_free_get(&tiny_queue), 0, "Queue should be full");

	/* Set up helper thread to process the queue after we start blocking */
	queue_to_process = &tiny_queue;
	k_sem_give(&queue_helper_start);

	/* This call will block until the helper thread processes the queue */
	struct test_request req = {.value = 22, .cmd = 2};
	struct test_response res = {0};

	ret = WEAVE_METHOD_CALL(method_tiny_queue, &req, &res);

	/* Call should have succeeded after queue was processed */
	zassert_equal(ret, 0, "Call should succeed after queue space freed");
	zassert_equal(res.result, 44, "Response should be 2x22");

	/* Wait for helper to signal it's done */
	k_sem_take(&queue_helper_done, K_MSEC(100));

	/* Both handlers should have been called (first from queue processing, second from our call)
	 */
	zassert_equal(atomic_get(&capture.call_count), 2, "Both handlers should have been called");

	/* Wait for the first async call to complete */
	ret = WEAVE_METHOD_WAIT(&blocking_ctx, K_FOREVER);
	zassert_equal(ret, 0, "First call should have completed");
	zassert_equal(blocking_res.result, 22, "First response should be 2x11");
}

/* =============================================================================
 * Method Structure Verification Tests
 * =============================================================================
 */

ZTEST(weave_method_unit_test, test_method_define_structure)
{
	/* Verify WEAVE_METHOD_DEFINE creates correct structure */
	zassert_equal(method_immediate.request_size, sizeof(struct test_request),
		      "Request size should match");
	zassert_equal(method_immediate.response_size, sizeof(struct test_response),
		      "Response size should match");
	zassert_not_null(method_immediate.handler, "Handler should be set");
	zassert_is_null(method_immediate.sink.queue, "Immediate method has NULL queue");

	/* Queued method */
	zassert_equal(method_queued.sink.queue, &method_queue,
		      "Queued method should have queue pointer");
}

ZTEST(weave_method_unit_test, test_method_sizes_exact)
{
	struct test_request req = {.value = 1, .cmd = 0};
	struct test_response res = {0};
	int ret;

	/* Exact sizes should work */
	ret = weave_method_call_unchecked(&method_immediate, &req,
					  sizeof(struct test_request), /* Exact */
					  &res, sizeof(struct test_response) /* Exact */);

	zassert_equal(ret, 0, "Exact sizes should succeed");
}

/* =============================================================================
 * Multiple Calls Test
 * =============================================================================
 */

ZTEST(weave_method_unit_test, test_multiple_calls)
{
	struct test_request req;
	struct test_response res;
	int ret;

	for (int i = 0; i < 10; i++) {
		req.value = i * 10;
		req.cmd = i;
		memset(&res, 0, sizeof(res));

		ret = weave_method_call_unchecked(&method_immediate, &req, sizeof(req), &res,
						  sizeof(res));

		zassert_equal(ret, 0, "Call %d should succeed", i);
		zassert_equal(res.result, i * 20, "Result %d should be correct", i);
	}

	zassert_equal(atomic_get(&capture.call_count), 10, "10 handler calls expected");
}

/* =============================================================================
 * WV_VOID Tests - Void Request/Response Support
 * =============================================================================
 */

ZTEST(weave_method_unit_test, test_void_request)
{
	struct test_response res = {0};
	int ret;

	/* Method with no request - use WV_VOID */
	ret = WEAVE_METHOD_CALL(method_void_request, WV_VOID, &res);

	zassert_equal(ret, 0, "Void request call should succeed");
	zassert_equal(atomic_get(&capture.call_count), 1, "Handler should be called");
	zassert_equal(res.result, 999, "Response should be filled");
	zassert_equal(res.status, 0x42, "Response status should be set");
}

ZTEST(weave_method_unit_test, test_void_response)
{
	struct test_request req = {.value = 123, .cmd = 5};
	int ret;

	/* Method with no response - use WV_VOID */
	ret = WEAVE_METHOD_CALL(method_void_response, &req, WV_VOID);

	zassert_equal(ret, 0, "Void response call should succeed");
	zassert_equal(atomic_get(&capture.call_count), 1, "Handler should be called");
	zassert_equal(capture.last_request.value, 123, "Request should be passed");
	zassert_equal(capture.last_request.cmd, 5, "Request cmd should be passed");
}

ZTEST(weave_method_unit_test, test_void_both)
{
	int ret;

	/* Method with no request and no response - trigger/action */
	ret = WEAVE_METHOD_CALL(method_void_both, WV_VOID, WV_VOID);

	zassert_equal(ret, 0, "Void both call should succeed");
	zassert_equal(atomic_get(&capture.call_count), 1, "Handler should be called");
}

ZTEST(weave_method_unit_test, test_void_method_sizes)
{
	/* Verify WEAVE_METHOD_DEFINE sets correct sizes with WV_VOID */
	zassert_equal(method_void_request.request_size, 0, "Void request should have size 0");
	zassert_equal(method_void_request.response_size, sizeof(struct test_response),
		      "Response size should match");

	zassert_equal(method_void_response.request_size, sizeof(struct test_request),
		      "Request size should match");
	zassert_equal(method_void_response.response_size, 0, "Void response should have size 0");

	zassert_equal(method_void_both.request_size, 0, "Void request should have size 0");
	zassert_equal(method_void_both.response_size, 0, "Void response should have size 0");
}

ZTEST(weave_method_unit_test, test_void_request_unchecked_api)
{
	struct test_response res = {0};
	int ret;

	/* Test unchecked API with NULL request and size 0 */
	ret = weave_method_call_unchecked(&method_void_request, NULL, 0, /* No request */
					  &res, sizeof(res));

	zassert_equal(ret, 0, "Unsafe call with NULL request should succeed");
	zassert_equal(res.result, 999, "Response should be filled");
}

ZTEST(weave_method_unit_test, test_void_response_unchecked_api)
{
	struct test_request req = {.value = 456, .cmd = 7};
	int ret;

	/* Test unchecked API with NULL response and size 0 */
	ret = weave_method_call_unchecked(&method_void_response, &req, sizeof(req), NULL,
					  0 /* No response */);

	zassert_equal(ret, 0, "Unsafe call with NULL response should succeed");
	zassert_equal(capture.last_request.value, 456, "Request should be passed");
}

/* =============================================================================
 * Async API Tests
 * =============================================================================
 */

ZTEST(weave_method_unit_test, test_async_call_immediate)
{
	struct test_request req = {.value = 50, .cmd = 2};
	struct test_response res = {0};
	struct weave_method_context ctx;
	int ret;

	/* Async call on immediate method - handler runs during call_async */
	ret = WEAVE_METHOD_CALL_ASYNC(method_immediate, &req, &res, &ctx);
	zassert_equal(ret, 0, "Async call should succeed");

	/* Handler already executed for immediate methods */
	zassert_equal(atomic_get(&capture.call_count), 1, "Handler should have run");

	/* Wait returns immediately with result */
	ret = WEAVE_METHOD_WAIT(&ctx, K_FOREVER);
	zassert_equal(ret, 0, "Wait should return handler result");
	zassert_equal(res.result, 100, "Response should be 2x50");
}

ZTEST(weave_method_unit_test, test_async_call_null_ctx)
{
	struct test_request req = {.value = 1, .cmd = 0};
	struct test_response res = {0};
	int ret;

	ret = weave_method_call_async(&method_immediate, &req, sizeof(req), &res, sizeof(res),
				      NULL);

	zassert_equal(ret, -EINVAL, "Should reject NULL context");
	zassert_equal(atomic_get(&capture.call_count), 0, "Handler should not run");
}

ZTEST(weave_method_unit_test, test_async_call_null_method)
{
	struct test_request req = {.value = 1, .cmd = 0};
	struct test_response res = {0};
	struct weave_method_context ctx;
	int ret;

	ret = weave_method_call_async(NULL, &req, sizeof(req), &res, sizeof(res), &ctx);

	zassert_equal(ret, -EINVAL, "Should reject NULL method");
}

ZTEST(weave_method_unit_test, test_async_wait_null_ctx)
{
	int ret;

	ret = weave_method_wait(NULL, K_FOREVER);

	zassert_equal(ret, -EINVAL, "Should reject NULL context");
}

ZTEST(weave_method_unit_test, test_async_wait_timeout)
{
	struct weave_method_context ctx;
	int ret;

	/* Initialize context but don't signal completion */
	k_sem_init(&ctx.completion, 0, 1);
	ctx.result = 42;

	/* Wait should timeout */
	ret = WEAVE_METHOD_WAIT(&ctx, K_NO_WAIT);
	zassert_equal(ret, -EAGAIN, "Should return -EAGAIN on timeout");
}

ZTEST(weave_method_unit_test, test_async_with_void_request)
{
	struct test_response res = {0};
	struct weave_method_context ctx;
	int ret;

	ret = WEAVE_METHOD_CALL_ASYNC(method_void_request, WV_VOID, &res, &ctx);
	zassert_equal(ret, 0, "Async call with void request should succeed");

	ret = WEAVE_METHOD_WAIT(&ctx, K_FOREVER);
	zassert_equal(ret, 0, "Wait should succeed");
	zassert_equal(res.result, 999, "Response should be filled");
}

ZTEST(weave_method_unit_test, test_async_with_void_response)
{
	struct test_request req = {.value = 789, .cmd = 9};
	struct weave_method_context ctx;
	int ret;

	ret = WEAVE_METHOD_CALL_ASYNC(method_void_response, &req, WV_VOID, &ctx);
	zassert_equal(ret, 0, "Async call with void response should succeed");

	ret = WEAVE_METHOD_WAIT(&ctx, K_FOREVER);
	zassert_equal(ret, 0, "Wait should succeed");
	zassert_equal(capture.last_request.value, 789, "Request should be passed");
}

ZTEST(weave_method_unit_test, test_async_call_request_too_small)
{
	struct test_request req = {.value = 1, .cmd = 0};
	struct test_response res = {0};
	struct weave_method_context ctx;
	int ret;

	/* Pass request size smaller than expected */
	ret = weave_method_call_async(&method_immediate, &req, sizeof(req) - 1, /* Too small! */
				      &res, sizeof(res), &ctx);

	zassert_equal(ret, -EINVAL, "Should reject undersized request");
	zassert_equal(atomic_get(&capture.call_count), 0, "Handler should not be called");
}

ZTEST(weave_method_unit_test, test_async_call_response_too_small)
{
	struct test_request req = {.value = 1, .cmd = 0};
	struct test_response res = {0};
	struct weave_method_context ctx;
	int ret;

	/* Pass response size smaller than expected */
	ret = weave_method_call_async(&method_immediate, &req, sizeof(req), &res,
				      sizeof(res) - 1, /* Too small! */
				      &ctx);

	zassert_equal(ret, -EINVAL, "Should reject undersized response");
	zassert_equal(atomic_get(&capture.call_count), 0, "Handler should not be called");
}
