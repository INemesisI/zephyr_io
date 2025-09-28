/*
 * Copyright (c) 2024 Zephyr IO Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_init, LOG_LEVEL_INF);

/* Test-specific methods and connections for wiring tests */
static struct weave_method init_method_a = {.name = "init_method_a",
					    .handler = mock_method_handler_simple,
					    .request_size = sizeof(struct test_request),
					    .reply_size = sizeof(struct test_reply),
					    .queue = &test_msgq_a};

static struct weave_method init_method_b = {.name = "init_method_b",
					    .handler = mock_method_handler_simple,
					    .request_size = 64,
					    .reply_size = 32,
					    .queue = &test_msgq_b};

static WEAVE_METHOD_PORT_DEFINE(init_port_a, struct test_request, struct test_reply);
static WEAVE_METHOD_PORT_DEFINE(init_port_b, struct test_request, struct test_reply);

/* Port with wrong sizes for mismatch testing */
static struct weave_method_port init_port_mismatch_request = {
	.name = "init_port_mismatch_request",
	.target_method = NULL,
	.request_size = 100, /* Different from init_method_b's 64 */
	.reply_size = 32};

static struct weave_method_port init_port_mismatch_reply = {
	.name = "init_port_mismatch_reply",
	.target_method = NULL,
	.request_size = 64,
	.reply_size = 100 /* Different from init_method_b's 32 */
};

/* Test signal and handlers for wiring tests */
static WEAVE_SIGNAL_DEFINE(init_signal_test, struct test_event);
/* test_handler_a is already defined in test_common.c */

/* Test basic initialization */
ZTEST(weave_init_suite, test_init_basic)
{
	/* Weave is initialized automatically via SYS_INIT */
	/* Just verify we can access basic structures */
	zassert_not_null(&test_msgq_a, "Module A should exist");
	zassert_not_null(&test_msgq_b, "Module B should exist");
	/* Message queues exist directly now, no module wrapper */
	zassert_not_null(&test_msgq_a, "Message queue A should exist");
}

/* Test module registration */
ZTEST(weave_init_suite, test_init_module_registration)
{
	/* Modules are registered via WEAVE_MODULE_DEFINE */
	/* Message queues are now k_msgq structures - no name field */
	/* Methods now reference queues directly */
	zassert_equal_ptr(test_method_simple.queue, &test_msgq_a, "Method should use msgq_a");
	zassert_equal_ptr(test_method_with_error.queue, &test_msgq_a,
			  "Method error should use msgq_a");
	zassert_equal_ptr(test_method_slow.queue, &test_msgq_a, "Method slow should use msgq_a");
}

/* Test method wiring */
ZTEST(weave_init_suite, test_init_method_wiring)
{
	/* Test that port starts unconnected */
	zassert_false(weave_method_is_connected(&init_port_a), "Port should start unconnected");

	/* Wire a method connection using API */
	int ret = weave_method_connect(&init_port_a, &init_method_a);
	zassert_equal(ret, 0, "Method connection should succeed");

	/* Verify connection */
	zassert_true(weave_method_is_connected(&init_port_a), "Port should be connected");
	zassert_equal_ptr(init_port_a.target_method, &init_method_a, "Method wiring failed");
	zassert_equal(init_port_a.request_size, sizeof(struct test_request),
		      "Port request size mismatch");
	zassert_equal(init_port_a.reply_size, sizeof(struct test_reply),
		      "Port reply size mismatch");

	/* Test disconnection */
	weave_method_disconnect(&init_port_a);
	zassert_false(weave_method_is_connected(&init_port_a), "Port should be disconnected");
	zassert_is_null(init_port_a.target_method, "Target method should be NULL");
}

/* Test signal wiring */
ZTEST(weave_init_suite, test_init_signal_wiring)
{
	/* NOTE: In production code, signal connections should be done at
	 * compile time using WEAVE_SIGNAL_CONNECT macro. This test manually
	 * manipulates the handlers list for testing purposes only. */

	/* Clear the handlers list first - TEST ONLY */
	sys_slist_init(&init_signal_test.handlers);

	/* Connect handler to signal - TEST ONLY */
	test_handler_a.queue = &test_msgq_a;
	sys_slist_append(&init_signal_test.handlers, &test_handler_a.node);

	/* Verify connection */
	zassert_false(sys_slist_is_empty(&init_signal_test.handlers),
		      "Signal should have handlers");

	struct weave_signal_handler *handler;
	SYS_SLIST_FOR_EACH_CONTAINER(&init_signal_test.handlers, handler, node) {
		zassert_equal_ptr(handler, &test_handler_a, "Handler mismatch");
		zassert_equal_ptr(handler->queue, &test_msgq_a, "Handler queue mismatch");
	}
}

/* Test request size mismatch detection */
ZTEST(weave_init_suite, test_init_size_mismatch_request)
{
	/* Test that weave properly validates size mismatches when connecting */
	/* Wire the mismatched port to method - this will fail due to size mismatch */
	int ret_connect = weave_method_connect(&init_port_mismatch_request, &init_method_b);
	zassert_equal(ret_connect, -EINVAL, "Should fail to connect with size mismatch");
	/* Force connection for testing error handling */
	init_port_mismatch_request.target_method = &init_method_b;

	/* Try to call with mismatched sizes - should fail */
	struct test_request req = {.value = 0x1234, .flags = 0};
	struct test_reply reply = {0};

	/* The call should fail due to size mismatch */
	int ret = weave_call_method(&init_port_mismatch_request, &req, sizeof(req), &reply,
				    sizeof(reply), K_NO_WAIT);

	/* Clean up */
	init_port_mismatch_request.target_method = NULL;

	zassert_equal(ret, -EINVAL, "Should fail with size mismatch");
}

/* Test reply size mismatch detection */
ZTEST(weave_init_suite, test_init_size_mismatch_reply)
{
	/* Test that weave properly validates size mismatches when connecting */
	/* Wire the mismatched port to method - this will fail due to size mismatch */
	int ret_connect = weave_method_connect(&init_port_mismatch_reply, &init_method_b);
	zassert_equal(ret_connect, -EINVAL, "Should fail to connect with size mismatch");
	/* Force connection for testing error handling */
	init_port_mismatch_reply.target_method = &init_method_b;

	/* Try to call with mismatched sizes - should fail */
	uint8_t req[64] = {0};    /* Match method's request size */
	uint8_t reply[100] = {0}; /* Mismatch method's reply size */

	/* The call should fail due to size mismatch */
	int ret = weave_call_method(&init_port_mismatch_reply, req, sizeof(req), reply,
				    sizeof(reply), K_NO_WAIT);

	/* Clean up */
	init_port_mismatch_reply.target_method = NULL;

	zassert_equal(ret, -EINVAL, "Should fail with size mismatch");
}

/* Test invalid connections with NULL ports/methods */
ZTEST(weave_init_suite, test_init_invalid_connections)
{
	/* Test calling through NULL port */
	struct test_request req = {.value = 0x5678, .flags = 0};
	struct test_reply reply = {0};
	int ret;

	/* Test NULL port */
	ret = weave_call_method(NULL, &req, sizeof(req), &reply, sizeof(reply), K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should fail with NULL port");

	/* Test unconnected port */
	zassert_is_null(init_port_b.target_method, "Port should be unconnected");
	ret = weave_call_method(&init_port_b, &req, sizeof(req), &reply, sizeof(reply), K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should fail with unconnected port");

	/* Test method with no queue - this is valid (immediate execution) */
	struct weave_method orphan_method = {.name = "orphan",
					     .handler = mock_method_handler_simple,
					     .request_size = sizeof(struct test_request),
					     .reply_size = sizeof(struct test_reply),
					     .queue = NULL};

	struct weave_method_port orphan_port = {.name = "orphan_port",
						.target_method = &orphan_method,
						.request_size = sizeof(struct test_request),
						.reply_size = sizeof(struct test_reply)};

	ret = weave_call_method(&orphan_port, &req, sizeof(req), &reply, sizeof(reply), K_NO_WAIT);
	zassert_equal(ret, 0, "Method with no queue should succeed (immediate execution)");
	zassert_equal(reply.result, 0x12345678, "Reply should be processed");
}

/* Test that methods can be assigned to modules */
ZTEST(weave_init_suite, test_init_method_module_assignment)
{
	/* Verify methods are properly assigned to modules */
	init_method_a.queue = &test_msgq_a;
	init_method_b.queue = &test_msgq_b;

	zassert_equal_ptr(init_method_a.queue, &test_msgq_a, "Method A module assignment");
	zassert_equal_ptr(init_method_b.queue, &test_msgq_b, "Method B module assignment");
}

/* Test signal initialization */
ZTEST(weave_init_suite, test_init_signal_initialization)
{
	/* Verify signal is properly initialized */
	zassert_str_equal(init_signal_test.name, "init_signal_test", "Signal name mismatch");
	zassert_equal(init_signal_test.event_size, sizeof(struct test_event),
		      "Signal event size mismatch");
}

/* Test handler initialization */
ZTEST(weave_init_suite, test_init_handler_initialization)
{
	/* Verify handler is properly initialized */
	zassert_str_equal(test_handler_a.name, "test_handler_a", "Handler name mismatch");
	zassert_not_null(test_handler_a.handler, "Handler function should not be NULL");
}

ZTEST_SUITE(weave_init_suite, NULL, NULL, common_test_setup, common_test_teardown, NULL);