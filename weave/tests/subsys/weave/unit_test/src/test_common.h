/*
 * Copyright (c) 2024 Zephyr IO Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef WEAVE_TEST_COMMON_H_
#define WEAVE_TEST_COMMON_H_

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/sys/atomic.h>
#include <zephyr_io/weave/weave.h>
#include <string.h>
#include <errno.h>

/* Test configuration constants */
#define TEST_MSGQ_SIZE         10
#define TEST_TIMEOUT_MS        10
#define TEST_MAX_ITERATIONS    100
#define TEST_STRESS_ITERATIONS 50
#define TEST_THREAD_STACK      2048
#define TEST_THREAD_PRIORITY   5

/* Test request/reply structures */
struct test_request {
	uint32_t value;
	uint32_t flags;
};

struct test_reply {
	uint32_t result;
	uint32_t status;
};

struct test_event {
	uint32_t event_id;
	uint32_t data;
};

/* Tracking structure for test handlers */
struct test_tracker {
	atomic_t call_count;
	atomic_t error_count;
	int last_result;
	struct test_request last_request;
	struct test_reply last_reply;
	struct test_event last_event;
	struct k_sem completion;
	void *last_user_data;
};

/* External test infrastructure */
extern struct test_tracker tracker_a;
extern struct test_tracker tracker_b;
extern struct test_tracker tracker_c;
extern struct test_tracker signal_tracker;

/* Test message queues (used directly now, no modules) */
extern struct k_msgq test_msgq_a;
extern struct k_msgq test_msgq_b;
extern struct k_msgq test_msgq_c;

/* Test methods */
extern struct weave_method test_method_simple;
extern struct weave_method test_method_with_error;
extern struct weave_method test_method_slow;
extern struct weave_method test_method_no_handler;

/* Test method ports */
extern struct weave_method_port test_port_simple;
extern struct weave_method_port test_port_error;
extern struct weave_method_port test_port_slow;
extern struct weave_method_port test_port_unconnected;
extern struct weave_method_port test_port_size_mismatch;

/* Test signals */
extern struct weave_signal test_signal_basic;
extern struct weave_signal test_signal_multi;
extern struct weave_signal test_signal_empty;

/* Test signal handlers */
extern struct weave_signal_handler test_handler_a;
extern struct weave_signal_handler test_handler_b;
extern struct weave_signal_handler test_handler_c;
extern struct weave_signal_handler test_handler_no_queue;

/* Test user data for handlers */
extern int test_user_data_a;
extern int test_user_data_b;
extern int test_user_data_c;

/* Helper functions */
void reset_test_environment(void);
void reset_tracker(struct test_tracker *tracker);
void init_tracker(struct test_tracker *tracker);

/* Wait helpers with timeout */
int wait_for_completion(struct test_tracker *tracker, k_timeout_t timeout);
int wait_for_count(atomic_t *counter, int expected, k_timeout_t timeout);

/* Message creation helpers */
struct weave_message_context *create_test_message_context(enum weave_msg_type type);
void free_test_message_context(struct weave_message_context *ctx);

/* Queue helpers */
void drain_message_queue(struct k_msgq *queue);
int fill_message_queue(struct k_msgq *queue);

/* Memory testing helpers */
int get_free_slab_count(void);
size_t get_heap_free_size(void);
void exhaust_memory_slab(void);
void exhaust_data_heap(void);

/* Thread helpers for concurrency tests */
void start_test_thread(k_thread_entry_t entry, void *p1, void *p2, void *p3);
void wait_for_threads_complete(int count);

/* Validation helpers */
void validate_request(struct test_request *req, uint32_t expected_value);
void validate_reply(struct test_reply *rep, uint32_t expected_result);
void validate_event(struct test_event *evt, uint32_t expected_id);

/* Test setup/teardown that can be shared */
void common_test_setup(void *fixture);
void common_test_teardown(void *fixture);

/* Mock handler implementations */
int mock_method_handler_simple(void *module, const void *request, void *reply);
int mock_method_handler_error(void *module, const void *request, void *reply);
int mock_method_handler_slow(void *module, const void *request, void *reply);
void mock_signal_handler(void *module, const void *event);

/* Test-specific error codes */
#define TEST_ERROR_BASE     1000
#define TEST_ERROR_HANDLER  (TEST_ERROR_BASE + 1)
#define TEST_ERROR_TIMEOUT  (TEST_ERROR_BASE + 2)
#define TEST_ERROR_OVERFLOW (TEST_ERROR_BASE + 3)

#endif /* WEAVE_TEST_COMMON_H_ */