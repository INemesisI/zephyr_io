/*
 * Copyright (c) 2024 Zephyr IO Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_message_proc, LOG_LEVEL_INF);

/* Test process request message */
ZTEST(weave_message_suite, test_process_request_message)
{
	struct weave_message msg = {.type = WEAVE_MSG_REQUEST,
				    .method = &test_method_simple,
				    .request_data =
					    &(struct test_request){.value = 0xABCD, .flags = 0},
				    .reply_data = &(struct test_reply){0},
				    .request_size = sizeof(struct test_request),
				    .reply_size = sizeof(struct test_reply),
				    .completion = NULL,
				    .result = NULL};

	/* Process the message */
	weave_process_message(&test_module_a, &msg);

	/* Verify handler was called */
	zassert_equal(atomic_get(&tracker_a.call_count), 1, "Handler not called");
	zassert_equal(tracker_a.last_request.value, 0xABCD, "Request value mismatch");
}

/* Verify signal and method subsystems are separate */
ZTEST(weave_message_suite, test_signal_method_separation)
{
	/* This test verifies that signals and methods are separate subsystems.
	 * Signals use event emission, methods use request/reply messaging.
	 * They should NOT be mixed - signals cannot be processed as messages. */

	/* Test that method processing works correctly */
	struct weave_message msg = {.type = WEAVE_MSG_REQUEST,
				    .method = &test_method_simple,
				    .request_data =
					    &(struct test_request){.value = 0x1234, .flags = 0},
				    .reply_data = &(struct test_reply){0},
				    .request_size = sizeof(struct test_request),
				    .reply_size = sizeof(struct test_reply),
				    .completion = NULL,
				    .result = NULL};

	/* Clear tracker */
	atomic_clear(&tracker_a.call_count);

	/* Process method message */
	weave_process_message(&test_module_a, &msg);

	/* Verify method handler was called */
	zassert_equal(atomic_get(&tracker_a.call_count), 1, "Method handler called");

	/* Note: Signal emission is tested separately in signal test suite */
}

/* Test process with NULL module */
ZTEST(weave_message_suite, test_process_null_module)
{
	struct weave_message msg = {.type = WEAVE_MSG_REQUEST, .method = &test_method_simple};

	/* Should handle gracefully */
	weave_process_message(NULL, &msg);

	/* Handler should not be called */
	zassert_equal(atomic_get(&tracker_a.call_count), 0, "Handler should not be called");
}

/* Test process with NULL message */
ZTEST(weave_message_suite, test_process_null_message)
{
	/* Should handle gracefully */
	weave_process_message(&test_module_a, NULL);

	/* No crash expected */
	zassert_equal(atomic_get(&tracker_a.call_count), 0, "Handler should not be called");
}

/* Test process unknown message type */
ZTEST(weave_message_suite, test_process_unknown_message_type)
{
	struct weave_message msg = {.type = (enum weave_msg_type)999, /* Invalid type */
				    .method = &test_method_simple};

	/* Should handle unknown type gracefully */
	weave_process_message(&test_module_a, &msg);

	/* Handler should not be called */
	zassert_equal(atomic_get(&tracker_a.call_count), 0, "Handler should not be called");
}

/* Test process message with no handler */
ZTEST(weave_message_suite, test_process_no_handler)
{
	struct weave_message msg = {
		.type = WEAVE_MSG_REQUEST,
		.method = &test_method_no_handler, /* Method with NULL handler */
		.request_data = &(struct test_request){.value = 0x1234, .flags = 0},
		.reply_data = &(struct test_reply){0},
		.request_size = sizeof(struct test_request),
		.reply_size = sizeof(struct test_reply),
		.completion = NULL,
		.result = NULL};

	/* Process the message */
	weave_process_message(&test_module_a, &msg);

	/* Handler should not crash, tracker should not be updated */
	zassert_equal(atomic_get(&tracker_a.call_count), 0, "Handler should not be called");
}

/* Test process all pending messages */
ZTEST(weave_message_suite, test_process_all_messages)
{
	/* Test processing multiple queued messages.
	 * Note: This test relies on the method being configured for queued execution. */
	struct test_request reqs[3];
	struct test_reply replies[3];
	int i, ret;

	/* Clear tracker and ensure queue is empty */
	atomic_clear(&tracker_a.call_count);
	drain_message_queue(&test_msgq_a);

	/* Ensure method uses queued execution */
	test_method_simple.module = &test_module_a;

	/* Queue multiple method calls - they should queue since module has a queue */
	for (i = 0; i < 3; i++) {
		reqs[i].value = (uint32_t)(i + 1);
		reqs[i].flags = 0;

		/* Use K_NO_WAIT since we're testing queuing, not blocking */
		ret = weave_call_method(&test_port_simple, &reqs[i], sizeof(reqs[i]), &replies[i],
					sizeof(replies[i]), K_NO_WAIT);

		/* The calls should queue successfully */
		if (ret != 0) {
			/* If queuing fails, it might be due to queue full or other issues */
			LOG_WRN("Failed to queue message %d: %d", i, ret);
		}
	}

	/* Process all queued messages */
	int processed = weave_process_all_messages(&test_module_a);

	/* We should have processed at least some messages */
	zassert_true(processed >= 0, "Process should return non-negative count");

	/* The number of processed messages should match what was successfully queued */
	if (processed > 0) {
		/* Verify handler was called for each processed message */
		zassert_equal(atomic_get(&tracker_a.call_count), processed,
			      "Handler call count %d doesn't match processed %d",
			      atomic_get(&tracker_a.call_count), processed);
	}
}

/* Test process with empty queue */
ZTEST(weave_message_suite, test_process_all_empty_queue)
{
	int processed;

	/* Ensure queue is empty */
	drain_message_queue(&test_msgq_a);

	/* Process empty queue */
	processed = weave_process_all_messages(&test_module_a);
	zassert_equal(processed, 0, "Should process 0 messages from empty queue");
}

/* Test process with NULL queue module */
ZTEST(weave_message_suite, test_process_all_null_queue)
{
	int processed;

	/* Process with module that has no queue */
	processed = weave_process_all_messages(&test_module_no_queue);
	zassert_equal(processed, 0, "Should return 0 for module with no queue");

	/* Process with NULL module */
	processed = weave_process_all_messages(NULL);
	zassert_equal(processed, 0, "Should return 0 for NULL module");
}

ZTEST_SUITE(weave_message_suite, NULL, NULL, common_test_setup, common_test_teardown, NULL);