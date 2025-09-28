/*
 * Copyright (c) 2024 Zephyr IO Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "test_common.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(weave_test_common, LOG_LEVEL_INF);

/* Message processing threads */
static struct k_thread msg_processor_threads[3];
static K_THREAD_STACK_ARRAY_DEFINE(msg_processor_stacks, 3, TEST_THREAD_STACK);
static bool processor_threads_running = false;

/* Message processor thread function */
static void msg_processor_thread(void *queue, void *unused1, void *unused2)
{
	struct k_msgq *msgq = (struct k_msgq *)queue;

	while (processor_threads_running) {
		weave_process_all_messages(msgq);
		k_sleep(K_MSEC(1));
	}
}

/* Test trackers */
struct test_tracker tracker_a = {0};
struct test_tracker tracker_b = {0};
struct test_tracker tracker_c = {0};
struct test_tracker signal_tracker = {0};

/* Define test message queues */
K_MSGQ_DEFINE(test_msgq_a, sizeof(struct weave_message *), TEST_MSGQ_SIZE, 4);
K_MSGQ_DEFINE(test_msgq_b, sizeof(struct weave_message *), TEST_MSGQ_SIZE, 4);
K_MSGQ_DEFINE(test_msgq_c, sizeof(struct weave_message *), TEST_MSGQ_SIZE, 4);

/* Test user data for handlers */
int test_user_data_a = 0xA;
int test_user_data_b = 0xB;
int test_user_data_c = 0xC;

/* Mock method handlers */
int mock_method_handler_simple(void *user_data, const void *request, void *reply)
{
	struct test_tracker *tracker = &tracker_a;

	/* Determine which tracker to use based on user_data */
	if (user_data == &test_user_data_b) {
		tracker = &tracker_b;
	} else if (user_data == &test_user_data_c) {
		tracker = &tracker_c;
	}

	atomic_inc(&tracker->call_count);
	tracker->last_user_data = user_data;

	if (request) {
		memcpy(&tracker->last_request, request, sizeof(struct test_request));
	}

	if (reply) {
		struct test_reply *rep = (struct test_reply *)reply;
		rep->result = 0x12345678;
		rep->status = 0;
		memcpy(&tracker->last_reply, rep, sizeof(struct test_reply));
	}

	k_sem_give(&tracker->completion);
	tracker->last_result = 0;
	return 0;
}

int mock_method_handler_error(void *user_data, const void *request, void *reply)
{
	struct test_tracker *tracker = &tracker_a;

	atomic_inc(&tracker->call_count);
	atomic_inc(&tracker->error_count);
	tracker->last_result = -TEST_ERROR_HANDLER;

	k_sem_give(&tracker->completion);
	return -TEST_ERROR_HANDLER;
}

int mock_method_handler_slow(void *user_data, const void *request, void *reply)
{
	struct test_tracker *tracker = &tracker_a;

	atomic_inc(&tracker->call_count);

	/* Simulate slow processing - sleep longer than typical test timeouts */
	k_sleep(K_MSEC(200));

	if (reply) {
		struct test_reply *rep = (struct test_reply *)reply;
		rep->result = 0xDEADBEEF;
		rep->status = 0;
	}

	k_sem_give(&tracker->completion);
	return 0;
}

void mock_signal_handler(void *user_data, const void *event)
{
	struct test_tracker *tracker = &signal_tracker;

	atomic_inc(&tracker->call_count);
	tracker->last_user_data = user_data;

	if (event) {
		memcpy(&tracker->last_event, event, sizeof(struct test_event));
	}

	k_sem_give(&tracker->completion);
}

/* Define test methods with queued execution */
WEAVE_METHOD_DEFINE_QUEUED(test_method_simple, mock_method_handler_simple, &test_msgq_a,
			   struct test_request, struct test_reply, &test_user_data_a);

WEAVE_METHOD_DEFINE_QUEUED(test_method_with_error, mock_method_handler_error, &test_msgq_a,
			   struct test_request, struct test_reply, &test_user_data_a);

WEAVE_METHOD_DEFINE_QUEUED(test_method_slow, mock_method_handler_slow, &test_msgq_a,
			   struct test_request, struct test_reply, &test_user_data_a);

/* Method with no handler */
struct weave_method test_method_no_handler = {.name = "test_method_no_handler",
					      .handler = NULL,
					      .request_size = sizeof(struct test_request),
					      .reply_size = sizeof(struct test_reply),
					      .queue = &test_msgq_a,
					      .user_data = &test_user_data_a};

/* Define test method ports */
WEAVE_METHOD_PORT_DEFINE(test_port_simple, struct test_request, struct test_reply);
WEAVE_METHOD_PORT_DEFINE(test_port_error, struct test_request, struct test_reply);
WEAVE_METHOD_PORT_DEFINE(test_port_slow, struct test_request, struct test_reply);
WEAVE_METHOD_PORT_DEFINE(test_port_unconnected, struct test_request, struct test_reply);

/* Port with mismatched size */
struct weave_method_port test_port_size_mismatch = {.name = "test_port_size_mismatch",
						    .target_method = NULL,
						    .request_size = sizeof(struct test_request) +
								    10, /* Intentional mismatch */
						    .reply_size = sizeof(struct test_reply)};

/* Define test signals */
WEAVE_SIGNAL_DEFINE(test_signal_basic, struct test_event);
WEAVE_SIGNAL_DEFINE(test_signal_multi, struct test_event);
WEAVE_SIGNAL_DEFINE(test_signal_empty, struct test_event);

/* Define signal handlers with queued execution */
WEAVE_SIGNAL_HANDLER_DEFINE_QUEUED(test_handler_a, mock_signal_handler, &test_msgq_a,
				   struct test_event, &test_user_data_a);
WEAVE_SIGNAL_HANDLER_DEFINE_QUEUED(test_handler_b, mock_signal_handler, &test_msgq_b,
				   struct test_event, &test_user_data_b);
WEAVE_SIGNAL_HANDLER_DEFINE_QUEUED(test_handler_c, mock_signal_handler, &test_msgq_c,
				   struct test_event, &test_user_data_c);

/* Handler with no module */
struct weave_signal_handler test_handler_no_queue = {.node = {NULL},
						     .name = "test_handler_no_queue",
						     .handler = mock_signal_handler,
						     .queue = NULL,
						     .user_data = NULL};

/* Helper function implementations */
void reset_tracker(struct test_tracker *tracker)
{
	atomic_clear(&tracker->call_count);
	atomic_clear(&tracker->error_count);
	tracker->last_result = 0;
	memset(&tracker->last_request, 0, sizeof(tracker->last_request));
	memset(&tracker->last_reply, 0, sizeof(tracker->last_reply));
	memset(&tracker->last_event, 0, sizeof(tracker->last_event));
	tracker->last_user_data = NULL;
	/* Don't reset semaphore - it's initialized once */
}

void init_tracker(struct test_tracker *tracker)
{
	reset_tracker(tracker);
	k_sem_init(&tracker->completion, 0, 1);
}

void reset_test_environment(void)
{
	/* Reset all trackers */
	reset_tracker(&tracker_a);
	reset_tracker(&tracker_b);
	reset_tracker(&tracker_c);
	reset_tracker(&signal_tracker);

	/* Process any pending messages to release resources */
	for (int i = 0; i < 50; i++) {
		weave_process_all_messages(&test_msgq_a);
		weave_process_all_messages(&test_msgq_b);
		weave_process_all_messages(&test_msgq_c);
	}

	/* Drain all message queues */
	drain_message_queue(&test_msgq_a);
	drain_message_queue(&test_msgq_b);
	drain_message_queue(&test_msgq_c);

	/* Reset method queue associations */
	test_method_simple.queue = &test_msgq_a;
	test_method_with_error.queue = &test_msgq_a;
	test_method_slow.queue = &test_msgq_a;
	test_method_no_handler.queue = &test_msgq_a;

	/* Reset handler queue associations */
	test_handler_a.queue = &test_msgq_a;
	test_handler_b.queue = &test_msgq_b;
	test_handler_c.queue = &test_msgq_c;

	/* Clear signal handler lists */
	sys_slist_init(&test_signal_basic.handlers);
	sys_slist_init(&test_signal_multi.handlers);
	sys_slist_init(&test_signal_empty.handlers);

	/* Wire default connections using API */
	weave_method_connect(&test_port_simple, &test_method_simple);
	weave_method_connect(&test_port_error, &test_method_with_error);
	weave_method_connect(&test_port_slow, &test_method_slow);
	weave_method_disconnect(&test_port_unconnected); /* Explicitly unconnected */
}

int wait_for_completion(struct test_tracker *tracker, k_timeout_t timeout)
{
	return k_sem_take(&tracker->completion, timeout);
}

int wait_for_count(atomic_t *counter, int expected, k_timeout_t timeout)
{
	int64_t end_time = k_uptime_get() + k_ticks_to_ms_floor64(timeout.ticks);

	while (atomic_get(counter) < expected) {
		if (k_uptime_get() >= end_time) {
			return -ETIMEDOUT;
		}
		k_sleep(K_MSEC(1));
	}
	return 0;
}

void drain_message_queue(struct k_msgq *queue)
{
	void *msg_ptr;
	while (k_msgq_get(queue, &msg_ptr, K_NO_WAIT) == 0) {
		/* Message drained */
	}
}

int fill_message_queue(struct k_msgq *queue)
{
	/* Fill queue with dummy pointers - queue holds pointers to messages */
	static void *dummy_ptr = NULL;
	int count = 0;

	/* Fill until queue is full */
	while (k_msgq_put(queue, &dummy_ptr, K_NO_WAIT) == 0) {
		count++;
		if (count >= TEST_MSGQ_SIZE) {
			break;
		}
	}
	return count;
}

void validate_request(struct test_request *req, uint32_t expected_value)
{
	zassert_not_null(req, "Request should not be NULL");
	zassert_equal(req->value, expected_value, "Request value mismatch: got %u, expected %u",
		      req->value, expected_value);
}

void validate_reply(struct test_reply *rep, uint32_t expected_result)
{
	zassert_not_null(rep, "Reply should not be NULL");
	zassert_equal(rep->result, expected_result, "Reply result mismatch: got %u, expected %u",
		      rep->result, expected_result);
}

void validate_event(struct test_event *evt, uint32_t expected_id)
{
	zassert_not_null(evt, "Event should not be NULL");
	zassert_equal(evt->event_id, expected_id, "Event ID mismatch: got %u, expected %u",
		      evt->event_id, expected_id);
}

/* Common test setup */
void common_test_setup(void *fixture)
{
	ARG_UNUSED(fixture);

	/* Initialize all trackers once */
	static bool trackers_initialized = false;
	if (!trackers_initialized) {
		init_tracker(&tracker_a);
		init_tracker(&tracker_b);
		init_tracker(&tracker_c);
		init_tracker(&signal_tracker);
		trackers_initialized = true;
	}

	/* Reset test environment for each test */
	reset_test_environment();

	/* Restore default method queue wiring to prevent cross-test interference */
	test_method_simple.queue = &test_msgq_a;
	test_method_with_error.queue = &test_msgq_a;
	test_method_slow.queue = &test_msgq_a;
	test_method_no_handler.queue = &test_msgq_a;

	/* Restore default port-to-method wiring using API */
	weave_method_connect(&test_port_simple, &test_method_simple);
	weave_method_connect(&test_port_error, &test_method_with_error);
	weave_method_connect(&test_port_slow, &test_method_slow);
	weave_method_disconnect(&test_port_unconnected); /* Explicitly unconnected */

	/* Clear signal handler lists to prevent cross-test interference */
	/* Note: In production, use WEAVE_SIGNAL_CONNECT for compile-time wiring */
	sys_slist_init(&test_signal_basic.handlers);
	sys_slist_init(&test_signal_multi.handlers);
	sys_slist_init(&test_signal_empty.handlers);

	/* Start message processor threads if not already running */
	if (!processor_threads_running) {
		processor_threads_running = true;

		k_thread_create(&msg_processor_threads[0], msg_processor_stacks[0],
				TEST_THREAD_STACK, msg_processor_thread, &test_msgq_a, NULL, NULL,
				TEST_THREAD_PRIORITY, 0, K_NO_WAIT);

		k_thread_create(&msg_processor_threads[1], msg_processor_stacks[1],
				TEST_THREAD_STACK, msg_processor_thread, &test_msgq_b, NULL, NULL,
				TEST_THREAD_PRIORITY, 0, K_NO_WAIT);

		k_thread_create(&msg_processor_threads[2], msg_processor_stacks[2],
				TEST_THREAD_STACK, msg_processor_thread, &test_msgq_c, NULL, NULL,
				TEST_THREAD_PRIORITY, 0, K_NO_WAIT);
	}
}

/* Common test teardown */
void common_test_teardown(void *fixture)
{
	ARG_UNUSED(fixture);

	/* Process any pending messages to release resources */
	for (int i = 0; i < 50; i++) {
		weave_process_all_messages(&test_msgq_a);
		weave_process_all_messages(&test_msgq_b);
		weave_process_all_messages(&test_msgq_c);
	}

	/* Drain all queues to prevent interference */
	drain_message_queue(&test_msgq_a);
	drain_message_queue(&test_msgq_b);
	drain_message_queue(&test_msgq_c);
}