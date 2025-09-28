/*
 * Copyright (c) 2024 Zephyr IO Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(test_stress, LOG_LEVEL_INF);

/* Thread stacks for stress tests */
K_THREAD_STACK_DEFINE(stress_test_stack, TEST_THREAD_STACK);
K_THREAD_STACK_DEFINE(stress_prod_stack, TEST_THREAD_STACK);
K_THREAD_STACK_DEFINE(stress_cons_stack, TEST_THREAD_STACK);

/* Thread control for stress test */
static volatile bool stress_processor_run = false;
static struct k_sem stress_processor_done;

/* Test configuration defaults if not defined */
#ifndef CONFIG_WEAVE_MAX_PENDING_REQUESTS
#define CONFIG_WEAVE_MAX_PENDING_REQUESTS 16
#endif

#ifndef CONFIG_WEAVE_MAX_REQUEST_SIZE
#define CONFIG_WEAVE_MAX_REQUEST_SIZE 256
#endif

#ifndef CONFIG_WEAVE_MAX_REPLY_SIZE
#define CONFIG_WEAVE_MAX_REPLY_SIZE 256
#endif

static void stress_processor_thread(void *p1, void *p2, void *p3)
{
	struct weave_module *module = (struct weave_module *)p1;
	int total_processed = 0;
	int max_iterations = 1000; /* Safety limit */

	/* Process messages until all are handled or stopped */
	while (stress_processor_run && total_processed < max_iterations) {
		int processed = weave_process_all_messages(module);
		if (processed == 0) {
			/* No more messages, exit early */
			break;
		}
		total_processed += processed;
		k_sleep(K_MSEC(1));
	}

	k_sem_give(&stress_processor_done);
}

/* Test maximum concurrent pending requests */
ZTEST(weave_stress_suite, test_maximum_pending_requests)
{
/* Use a reasonable limit for testing */
#define MAX_TEST_REQUESTS 16
	struct test_request reqs[MAX_TEST_REQUESTS];
	struct test_reply replies[MAX_TEST_REQUESTS];
	struct k_thread processor_thread;
	k_tid_t tid;
	int i, ret;

	/* Initialize synchronization */
	k_sem_init(&stress_processor_done, 0, 1);
	stress_processor_run = true;

	/* Start processor thread with delay to allow queuing */
	tid = k_thread_create(&processor_thread, stress_test_stack, TEST_THREAD_STACK,
			      stress_processor_thread, &test_module_a, NULL, NULL,
			      TEST_THREAD_PRIORITY, 0, K_MSEC(100));

	/* Queue maximum requests */
	for (i = 0; i < MAX_TEST_REQUESTS - 1; i++) {
		reqs[i].value = i;
		reqs[i].flags = 0;
		/* Use K_FOREVER to queue without waiting */
		ret = weave_call_method(&test_port_simple, &reqs[i], sizeof(reqs[i]), &replies[i],
					sizeof(replies[i]), K_SECONDS(5));
		zassert_equal(ret, 0, "Request %d failed", i);
	}

	/* Wait for processing to complete */
	k_sem_take(&stress_processor_done, K_SECONDS(10));
	stress_processor_run = false;
	k_thread_join(&processor_thread, K_SECONDS(1));

	/* Verify all were processed */
	zassert_true(atomic_get(&tracker_a.call_count) >= MAX_TEST_REQUESTS - 1,
		     "Not all requests processed");
#undef MAX_TEST_REQUESTS
}

/* Test rapid signal emission */
ZTEST(weave_stress_suite, test_rapid_signal_emission)
{
	struct test_event event = {.event_id = 0x8A91D, .data = 0};
	int i, ret;
	int64_t start, end;

	/* Setup direct handler for fastest processing */
	sys_slist_init(&test_signal_basic.handlers);
	test_handler_a.module = &test_module_no_queue;
	sys_slist_append(&test_signal_basic.handlers, &test_handler_a.node);

	/* Emit many signals rapidly */
	start = k_uptime_get();
	for (i = 0; i < 1000; i++) {
		event.data = i;
		ret = weave_emit_signal(&test_signal_basic, &event);
		zassert_equal(ret, 0, "Signal %d failed", i);
	}
	end = k_uptime_get();

	/* All should be processed */
	zassert_equal(atomic_get(&signal_tracker.call_count), 1000,
		      "All signals should be processed");

	LOG_INF("Processed 1000 signals in %lld ms", end - start);
}

/* Test rapid memory pool cycling */
ZTEST(weave_stress_suite, test_memory_pool_cycling)
{
	struct test_request req = {.value = 0, .flags = 0};
	struct test_reply reply = {0};
	int i, ret;
	int64_t start, end;

	/* Use direct execution for speed */
	test_method_simple.module = &test_module_no_queue;

	/* Rapid allocate/free cycles */
	start = k_uptime_get();
	for (i = 0; i < 500; i++) {
		req.value = i;
		ret = weave_call_method(&test_port_simple, &req, sizeof(req), &reply, sizeof(reply),
					K_NO_WAIT);
		zassert_equal(ret, 0, "Cycle %d failed", i);
	}
	end = k_uptime_get();

	/* Restore module */
	test_method_simple.module = &test_module_a;

	LOG_INF("Completed 500 memory cycles in %lld ms", end - start);
}

/* Thread data for queue saturation test */
static atomic_t produce_count;
static atomic_t consume_count;

/* Producer thread function for queue saturation test */
static void producer_fn(void *p1, void *p2, void *p3)
{
	for (int i = 0; i < 100; i++) {
		struct test_request r = {.value = (uint32_t)i, .flags = 0};
		struct test_reply rep = {0};
		int ret = weave_call_method(&test_port_simple, &r, sizeof(r), &rep, sizeof(rep),
					    K_MSEC(100));
		if (ret == 0) {
			atomic_inc(&produce_count);
		}
		k_yield();
	}
}

/* Consumer thread function for queue saturation test */
static void consumer_fn(void *p1, void *p2, void *p3)
{
	for (int i = 0; i < 200; i++) {
		int processed = weave_process_all_messages(&test_module_a);
		atomic_add(&consume_count, processed);
		k_sleep(K_MSEC(1));
	}
}

/* Test sustained queue pressure */
ZTEST(weave_stress_suite, test_queue_saturation)
{
	struct k_thread producer_thread, consumer_thread;
	k_tid_t prod_tid, cons_tid;

	/* Initialize counters */
	atomic_clear(&produce_count);
	atomic_clear(&consume_count);

	/* Start threads */
	prod_tid =
		k_thread_create(&producer_thread, stress_prod_stack, TEST_THREAD_STACK, producer_fn,
				NULL, NULL, NULL, TEST_THREAD_PRIORITY, 0, K_NO_WAIT);

	cons_tid =
		k_thread_create(&consumer_thread, stress_cons_stack, TEST_THREAD_STACK, consumer_fn,
				NULL, NULL, NULL, TEST_THREAD_PRIORITY - 1, 0, K_NO_WAIT);

	/* Let them run */
	k_sleep(K_SECONDS(1));

	/* Clean up */
	k_thread_abort(prod_tid);
	k_thread_abort(cons_tid);

	/* Verify work was done */
	zassert_true(atomic_get(&produce_count) > 0, "Producer should have sent messages");
	zassert_true(atomic_get(&consume_count) > 0, "Consumer should have processed messages");
}

/* Test large payload transfer */
ZTEST(weave_stress_suite, test_large_payload_transfer)
{
/* Large data structures - use fixed sizes for testing */
#define TEST_LARGE_REQUEST_SIZE 512
#define TEST_LARGE_REPLY_SIZE   512
	struct large_request {
		uint8_t header[64];
		uint8_t payload[TEST_LARGE_REQUEST_SIZE - 64];
	} large_req;

	struct large_reply {
		uint8_t header[64];
		uint8_t payload[TEST_LARGE_REPLY_SIZE - 64];
	} large_reply;

	int ret, i;

	/* Setup large method */
	struct weave_method large_method = {.name = "large_method",
					    .handler = mock_method_handler_simple,
					    .request_size = sizeof(large_req),
					    .reply_size = sizeof(large_reply),
					    .module = &test_module_no_queue};

	struct weave_method_port large_port = {.name = "large_port",
					       .target_method = &large_method,
					       .request_size = sizeof(large_req),
					       .reply_size = sizeof(large_reply)};

	/* Fill with test pattern */
	for (i = 0; i < sizeof(large_req.header); i++) {
		large_req.header[i] = i;
	}
	memset(large_req.payload, 0xAA, sizeof(large_req.payload));

	/* Transfer large payloads */
	for (i = 0; i < 10; i++) {
		ret = weave_call_method(&large_port, &large_req, sizeof(large_req), &large_reply,
					sizeof(large_reply), K_SECONDS(1));
		zassert_equal(ret, 0, "Large transfer %d failed", i);
	}

	LOG_INF("Successfully transferred %d large payloads", 10);
#undef TEST_LARGE_REQUEST_SIZE
#undef TEST_LARGE_REPLY_SIZE
}

ZTEST_SUITE(weave_stress_suite, NULL, NULL, common_test_setup, common_test_teardown, NULL);