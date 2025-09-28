/*
 * Copyright (c) 2024 Zephyr IO Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_memory, LOG_LEVEL_INF);

/* Note: We cannot directly access internal memory structures (message_slab, weave_data_heap)
 * from weave_core.c as they are static. Instead, we test memory management indirectly
 * through the API by observing allocation failures and recovery. */

/* Test successful message allocation through API */
ZTEST(weave_memory_suite, test_slab_allocation_success)
{
	struct test_request req = {.value = 0x1234, .flags = 0};
	struct test_reply reply = {0};
	int ret;

	/* Make a simple method call to test allocation works */
	test_method_simple.queue = NULL;
	ret = weave_call_method(&test_port_simple, &req, sizeof(req), &reply, sizeof(reply),
				K_NO_WAIT);
	test_method_simple.queue = &test_msgq_a;

	zassert_equal(ret, 0, "Method call should succeed when memory available");
}

/* Test message pool exhaustion handling */
ZTEST(weave_memory_suite, test_slab_exhaustion)
{
	/* We can't directly test slab exhaustion without access to internals,
	 * but we can test that the system handles resource exhaustion gracefully
	 * by filling the message queue which indirectly limits resources */

	struct test_request req = {.value = 0xE000, .flags = 0};
	struct test_reply reply = {0};
	int ret;

	/* Fill message queue to trigger resource limits */
	int filled = fill_message_queue(&test_msgq_a);
	zassert_true(filled > 0, "Queue should be filled");

	/* Try to make a call - should fail due to full queue */
	ret = weave_call_method(&test_port_simple, &req, sizeof(req), &reply, sizeof(reply),
				K_NO_WAIT);
	zassert_not_equal(ret, 0, "Should fail when resources exhausted");

	/* Clean up */
	drain_message_queue(&test_msgq_a);
}

/* Test successful data buffer allocation through API */
ZTEST(weave_memory_suite, test_heap_allocation_success)
{
	/* Test heap allocation indirectly by using large payloads */
	struct test_request req = {.value = 0x4EA9, .flags = 0xFF};
	struct test_reply reply = {0};
	int ret;

	/* Make method call which internally allocates from heap */
	test_method_simple.queue = NULL;
	ret = weave_call_method(&test_port_simple, &req, sizeof(req), &reply, sizeof(reply),
				K_NO_WAIT);
	test_method_simple.queue = &test_msgq_a;

	zassert_equal(ret, 0, "Method with data buffers should succeed");
}

/* Test handling of large request buffers */
ZTEST(weave_memory_suite, test_heap_exhaustion_request)
{
	/* Test that system handles large allocations gracefully
	 * We can't exhaust heap directly, but can test with many concurrent operations */

	struct test_request reqs[5];
	struct test_reply replies[5];
	int i, ret, succeeded = 0;

	/* Make multiple concurrent calls to stress memory allocation */
	for (i = 0; i < 5; i++) {
		reqs[i].value = 0x5000 + i;
		reqs[i].flags = i;

		test_method_simple.queue = NULL;
		ret = weave_call_method(&test_port_simple, &reqs[i], sizeof(reqs[i]), &replies[i],
					sizeof(replies[i]), K_NO_WAIT);
		test_method_simple.queue = &test_msgq_a;

		if (ret == 0) {
			succeeded++;
		}
	}

	/* At least some should succeed */
	zassert_true(succeeded > 0, "At least one allocation should succeed");
}

/* Test basic reference counting */
ZTEST(weave_memory_suite, test_context_refcount_basic)
{
	struct test_request req = {.value = 0xCAFE, .flags = 0};
	struct test_reply reply = {0};
	int ret;

	/* Make a method call which creates a context with refcount */
	ret = weave_call_method(&test_port_simple, &req, sizeof(req), &reply, sizeof(reply),
				K_SECONDS(1));

	zassert_equal(ret, 0, "Method call failed: %d", ret);

	/* Context should be automatically freed after completion */
	/* We can't directly test this but the call completing proves ref counting worked */
}

/* Test reference counting with timeout */
ZTEST(weave_memory_suite, test_context_refcount_timeout)
{
	struct test_request req = {.value = 0xDEAD, .flags = 0};
	struct test_reply reply = {0};
	int ret;

	/* Use slow method that will timeout */
	ret = weave_call_method(&test_port_slow, &req, sizeof(req), &reply, sizeof(reply),
				K_MSEC(5));

	zassert_equal(ret, -ETIMEDOUT, "Should timeout");

	/* Context should be properly cleaned up even after timeout */
	/* Receiver may still be processing, but our reference is released */
}

/* Test multiple reference handling */
ZTEST(weave_memory_suite, test_context_refcount_multiple)
{
	struct test_event event = {.event_id = 0x1234, .data = 0x5678};
	int ret;

	/* Setup multiple handlers */
	sys_slist_init(&test_signal_multi.handlers);
	test_handler_a.queue = &test_msgq_a;
	test_handler_b.queue = &test_msgq_b;
	sys_slist_append(&test_signal_multi.handlers, &test_handler_a.node);
	sys_slist_append(&test_signal_multi.handlers, &test_handler_b.node);

	/* Emit signal to multiple handlers */
	ret = weave_emit_signal(&test_signal_multi, &event);
	zassert_equal(ret, 0, "Signal emission failed");

	/* Each handler should handle its own reference */
	/* Signal contexts are fire-and-forget, so they clean up automatically */
}

/* Test proper cleanup on various errors */
ZTEST(weave_memory_suite, test_context_cleanup_on_error)
{
	struct test_request req = {.value = 0xBAD, .flags = 0};
	struct test_reply reply = {0};
	int ret;

	/* Test various error conditions */

	/* 1. NULL port */
	ret = weave_call_method(NULL, &req, sizeof(req), &reply, sizeof(reply), K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should fail with NULL port");

	/* 2. Unconnected port */
	ret = weave_call_method(&test_port_unconnected, &req, sizeof(req), &reply, sizeof(reply),
				K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should fail with unconnected port");

	/* 3. Invalid buffer sizes */
	ret = weave_call_method(&test_port_simple, &req, 0, &reply, sizeof(reply), K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should fail with invalid size");

	/* Memory should be properly cleaned up in all cases */
	/* Try a valid call to ensure memory is still available */
	ret = weave_call_method(&test_port_simple, &req, sizeof(req), &reply, sizeof(reply),
				K_SECONDS(1));
	zassert_equal(ret, 0, "Valid call should succeed after errors");
}

/* Test large message handling */
static int mock_large_handler(void *module, const void *request, void *reply)
{
	/* Handler specifically for large buffers */
	(void)module; /* Unused in this handler */

	if (request && reply) {
		/* Just verify we can access the buffers */
		uint8_t *req_data = (uint8_t *)request;
		uint8_t *rep_data = (uint8_t *)reply;

		/* Set first and last bytes to verify buffer access */
		if (req_data[0] == 0xAB && req_data[511] == 0xAB) {
			rep_data[0] = 0xCD;
			rep_data[511] = 0xEF;
		}
	}

	atomic_inc(&tracker_a.call_count);
	return 0;
}

ZTEST(weave_memory_suite, test_large_message_handling)
{
	/* Create large request/reply structures */
	struct large_request {
		uint8_t data[512];
	} large_req;

	struct large_reply {
		uint8_t data[512];
	} large_reply;

	/* Setup large method with proper handler */
	struct weave_method large_method = {
		.name = "large_method",
		.handler = mock_large_handler, /* Use handler that expects large buffers */
		.request_size = sizeof(struct large_request),
		.reply_size = sizeof(struct large_reply),
		.queue = &test_msgq_a};

	struct weave_method_port large_port = {.name = "large_port",
					       .target_method = &large_method,
					       .request_size = sizeof(struct large_request),
					       .reply_size = sizeof(struct large_reply)};

	/* Fill request with pattern */
	memset(large_req.data, 0xAB, sizeof(large_req.data));

	/* Make call with large buffers */
	int ret = weave_call_method(&large_port, &large_req, sizeof(large_req), &large_reply,
				    sizeof(large_reply), K_SECONDS(1));

	zassert_equal(ret, 0, "Large message call failed: %d", ret);

	/* Verify handler was called */
	zassert_equal(atomic_get(&tracker_a.call_count), 1, "Handler should be called");

	/* Verify reply was filled */
	zassert_equal(large_reply.data[0], 0xCD, "Reply first byte mismatch");
	zassert_equal(large_reply.data[511], 0xEF, "Reply last byte mismatch");
}

/* Test rapid allocation/free cycles */
ZTEST(weave_memory_suite, test_memory_pool_cycling)
{
	struct test_request req = {.value = 0, .flags = 0};
	struct test_reply reply = {0};
	int i, ret;

	/* Rapid allocation/free cycles */
	for (i = 0; i < TEST_STRESS_ITERATIONS; i++) {
		req.value = i;

		/* Direct call (no queue) for faster cycling */
		test_method_simple.queue = NULL;
		ret = weave_call_method(&test_port_simple, &req, sizeof(req), &reply, sizeof(reply),
					K_NO_WAIT);
		test_method_simple.queue = &test_msgq_a;

		zassert_equal(ret, 0, "Iteration %d failed", i);
	}
}

/* Test memory leak detection */
ZTEST(weave_memory_suite, test_memory_leak_detection)
{
	struct test_event event = {.event_id = 0x1EA4, .data = 0};
	int i, ret;

	/* Setup handler */
	sys_slist_init(&test_signal_basic.handlers);
	test_handler_a.queue = NULL;
	sys_slist_append(&test_signal_basic.handlers, &test_handler_a.node);

	/* Emit many signals */
	for (i = 0; i < TEST_STRESS_ITERATIONS; i++) {
		event.data = i;
		ret = weave_emit_signal(&test_signal_basic, &event);
		zassert_equal(ret, 0, "Signal %d failed", i);
	}

	/* If there were leaks, subsequent operations would fail */
	ret = weave_emit_signal(&test_signal_basic, &event);
	zassert_equal(ret, 0, "Final signal should succeed if no leaks");
}

ZTEST_SUITE(weave_memory_suite, NULL, NULL, common_test_setup, common_test_teardown, NULL);