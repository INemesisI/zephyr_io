/*
 * Copyright (c) 2026 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>
#include <weave/core.h>

/* Test configuration constants */
#define TEST_QUEUE_SIZE   50
#define TEST_TIMEOUT_MS   10
#define TEST_MAX_MESSAGES 100

/* =============================================================================
 * Test Payload Ops - Track ref/unref calls for lifecycle verification
 * =============================================================================
 */

static atomic_t ref_count;
static atomic_t unref_count;
static atomic_t filter_count;

static void reset_signal_counters(void)
{
	atomic_clear(&ref_count);
	atomic_clear(&unref_count);
	atomic_clear(&filter_count);
}

/* Standard ref - always succeeds */
static int test_ref(void *ptr, struct weave_sink *sink)
{
	ARG_UNUSED(ptr);
	ARG_UNUSED(sink);
	atomic_inc(&ref_count);
	return 0;
}

/* Standard unref */
static void test_unref(void *ptr)
{
	ARG_UNUSED(ptr);
	atomic_inc(&unref_count);
}

/* Payload ops with only unref (no ref) - tests line 31 branch */
static const struct weave_payload_ops test_ops_unref_only = {
	.ref = NULL,
	.unref = test_unref,
};

/* Payload ops with only ref (no unref) - tests line 42/57/130 branches */
static const struct weave_payload_ops test_ops_ref_only = {
	.ref = test_ref,
	.unref = NULL,
};

/* Filtering ref - rejects based on user_data value */
static int test_ref_filter(void *ptr, struct weave_sink *sink)
{
	ARG_UNUSED(ptr);
	atomic_inc(&filter_count);
	/* Reject sinks with user_data == NULL */
	if (sink->user_data == NULL) {
		return -EACCES;
	}
	atomic_inc(&ref_count);
	return 0;
}

static const struct weave_payload_ops test_ops = {
	.ref = test_ref,
	.unref = test_unref,
};

static const struct weave_payload_ops test_filter_ops = {
	.ref = test_ref_filter,
	.unref = test_unref,
};

/* =============================================================================
 * Test Capture Context - Track handler invocations
 * =============================================================================
 */

struct test_capture {
	atomic_t count;
	void *last_ptr;
	void *last_user_data;
};

static struct test_capture captures[10] = {0};

static void reset_all_captures(void)
{
	ARRAY_FOR_EACH_PTR(captures, capture) {
		atomic_clear(&capture->count);
		capture->last_ptr = NULL;
		capture->last_user_data = NULL;
	}
}

/* Handler that captures delivery info */
static void capture_handler(void *ptr, void *user_data)
{
	struct test_capture *capture = (struct test_capture *)user_data;

	zassert_not_null(capture, "Capture context should not be NULL");
	zassert_not_null(ptr, "Pointer should not be NULL");

	atomic_inc(&capture->count);
	capture->last_ptr = ptr;
	capture->last_user_data = user_data;
}

/* =============================================================================
 * Test Infrastructure - Sources, Sinks, Connections
 * =============================================================================
 */

/* Message queue for queued delivery */
WEAVE_MSGQ_DEFINE(test_queue, TEST_QUEUE_SIZE);

/* Additional test queues for specific tests (must be at file scope) */
WEAVE_MSGQ_DEFINE(tiny_queue, 1);         /* For overflow test */
WEAVE_MSGQ_DEFINE(null_sink_queue, 4);    /* For NULL sink test */
WEAVE_MSGQ_DEFINE(null_handler_queue, 4); /* For NULL handler test */
WEAVE_MSGQ_DEFINE(no_ops_queue, 4);       /* For no-ops sink test */
WEAVE_MSGQ_DEFINE(no_unref_queue, 4);     /* For no-unref test */

/* Sources with payload ops */
static struct weave_source sources[4] = {
	WEAVE_SOURCE_INITIALIZER(source0, &test_ops), WEAVE_SOURCE_INITIALIZER(source1, &test_ops),
	WEAVE_SOURCE_INITIALIZER(source2, &test_ops),
	WEAVE_SOURCE_INITIALIZER(isolated, &test_ops), /* No connections */
};
#define ISOLATED 3

/* Sinks - mix of immediate and queued
 * Note: Sinks have ops set for proper lifecycle in weave_process_messages.
 */
static struct weave_sink sinks[4] = {
	WEAVE_SINK_INITIALIZER(capture_handler, &test_queue, &captures[0], &test_ops),
	WEAVE_SINK_INITIALIZER(capture_handler, &test_queue, &captures[1], &test_ops),
	WEAVE_SINK_INITIALIZER(capture_handler, WV_IMMEDIATE, &captures[2], &test_ops),
	WEAVE_SINK_INITIALIZER(capture_handler, WV_IMMEDIATE, &captures[3], &test_ops),
};

/* Connection matrix:
 * sources[0] -> sinks[0], sinks[1], sinks[2] (2 queued + 1 immediate)
 * sources[1] -> sinks[0], sinks[3] (1 queued + 1 immediate)
 * sources[2] -> sinks[2], sinks[3] (2 immediate)
 * sources[ISOLATED] -> (none)
 */
WEAVE_CONNECT(&sources[0], &sinks[0]);
WEAVE_CONNECT(&sources[0], &sinks[1]);
WEAVE_CONNECT(&sources[0], &sinks[2]);

WEAVE_CONNECT(&sources[1], &sinks[0]);
WEAVE_CONNECT(&sources[1], &sinks[3]);

WEAVE_CONNECT(&sources[2], &sinks[2]);
WEAVE_CONNECT(&sources[2], &sinks[3]);

/* Connectivity lookup tables */
static const bool connected[4][4] = {
	{true, true, true, false},   /* sources[0] */
	{true, false, false, true},  /* sources[1] */
	{false, false, true, true},  /* sources[2] */
	{false, false, false, false} /* sources[ISOLATED] */
};

static const size_t num_connected[] = {3, 2, 2, 0};
static const size_t num_immediate[] = {1, 1, 2, 0};
static const size_t num_queued[] = {2, 1, 0, 0};

/* =============================================================================
 * Helper Functions
 * =============================================================================
 */

static void process_all_messages(void)
{
	int count = 0;

	while (weave_process_messages(&test_queue, K_NO_WAIT) > 0 && count < TEST_MAX_MESSAGES) {
		count++;
	}
}

static void verify_immediate_deliveries(size_t source_idx, int expected_count, void *expected_ptr)
{
	for (size_t sink_idx = 0; sink_idx < ARRAY_SIZE(sinks); sink_idx++) {
		struct test_capture *capture = &captures[sink_idx];
		bool is_immediate = (sinks[sink_idx].queue == NULL);

		if (connected[source_idx][sink_idx] && is_immediate) {
			zassert_equal(atomic_get(&capture->count), expected_count,
				      "Immediate sink[%zu] should have %d deliveries", sink_idx,
				      expected_count);
			if (expected_count > 0) {
				zassert_equal(capture->last_ptr, expected_ptr,
					      "Sink[%zu] should have correct ptr", sink_idx);
			}
		}
	}
}

static void verify_all_deliveries(size_t source_idx, int expected_count, void *expected_ptr)
{
	for (size_t sink_idx = 0; sink_idx < ARRAY_SIZE(sinks); sink_idx++) {
		struct test_capture *capture = &captures[sink_idx];

		if (connected[source_idx][sink_idx]) {
			zassert_equal(atomic_get(&capture->count), expected_count,
				      "Sink[%zu] should have %d deliveries", sink_idx,
				      expected_count);
			if (expected_count > 0) {
				zassert_equal(capture->last_ptr, expected_ptr,
					      "Sink[%zu] should have correct ptr", sink_idx);
			}
		} else {
			zassert_equal(atomic_get(&capture->count), 0,
				      "Unconnected sink[%zu] should have 0 deliveries", sink_idx);
		}
	}
}

/* =============================================================================
 * Test Setup/Teardown
 * =============================================================================
 */

static void test_setup(void *fixture)
{
	ARG_UNUSED(fixture);

	reset_all_captures();
	reset_signal_counters();
	k_msgq_purge(&test_queue);
}

static void test_teardown(void *fixture)
{
	ARG_UNUSED(fixture);

	process_all_messages();

	/* Verify queue is empty */
	zassert_equal(k_msgq_num_used_get(&test_queue), 0, "Queue should be empty");

	/* Verify connection integrity */
	for (size_t src_idx = 0; src_idx < ARRAY_SIZE(sources); src_idx++) {
		struct weave_source *source = &sources[src_idx];
		struct weave_connection *conn;
		size_t conn_count = 0;

		SYS_SLIST_FOR_EACH_CONTAINER(&source->sinks, conn, node) {
			zassert_equal(conn->source, source, "Connection source mismatch");
			zassert_not_null(conn->sink, "Connection sink should not be NULL");
			conn_count++;
		}
		zassert_equal(conn_count, num_connected[src_idx],
			      "Source[%zu] connection count mismatch", src_idx);
	}
}

ZTEST_SUITE(weave_core_unit_test, NULL, NULL, test_setup, test_teardown, NULL);

/* =============================================================================
 * Basic Functionality Tests
 * =============================================================================
 */

ZTEST(weave_core_unit_test, test_emit_basic)
{
	int test_data = 0x1234;
	int ret;

	/* Emit from source[0] - should reach 3 sinks */
	ret = weave_source_emit(&sources[0], &test_data, K_NO_WAIT);
	zassert_equal(ret, num_connected[0], "Should deliver to %zu sinks", num_connected[0]);

	/* Immediate sinks should have received already */
	verify_immediate_deliveries(0, 1, &test_data);

	/* Process queued messages */
	process_all_messages();

	/* Now all connected sinks should have received */
	verify_all_deliveries(0, 1, &test_data);
}

ZTEST(weave_core_unit_test, test_emit_from_each_source)
{
	for (size_t src_idx = 0; src_idx < ARRAY_SIZE(sources); src_idx++) {
		int test_data = 0x1000 + src_idx;

		int ret = weave_source_emit(&sources[src_idx], &test_data, K_NO_WAIT);
		zassert_equal(ret, num_connected[src_idx],
			      "Source[%zu] should deliver to %zu sinks", src_idx,
			      num_connected[src_idx]);

		verify_immediate_deliveries(src_idx, 1, &test_data);
		process_all_messages();
		verify_all_deliveries(src_idx, 1, &test_data);

		reset_all_captures();
		reset_signal_counters();
	}
}

ZTEST(weave_core_unit_test, test_emit_isolated_source)
{
	int test_data = 0xDEAD;

	int ret = weave_source_emit(&sources[ISOLATED], &test_data, K_NO_WAIT);
	zassert_equal(ret, 0, "Isolated source should deliver to 0 sinks");

	/* No refs should have been taken */
	zassert_equal(atomic_get(&ref_count), 0, "No refs for isolated source");
}

/* =============================================================================
 * Payload Ops Tests (ref/unref lifecycle)
 * =============================================================================
 */

ZTEST(weave_core_unit_test, test_ref_called_before_delivery)
{
	int test_data = 0xABCD;

	weave_source_emit(&sources[0], &test_data, K_NO_WAIT);

	/* Ref should be called for each connected sink */
	zassert_equal(atomic_get(&ref_count), num_connected[0], "Ref should be called %zu times",
		      num_connected[0]);
}

ZTEST(weave_core_unit_test, test_unref_after_immediate)
{
	int test_data = 0xBEEF;

	weave_source_emit(&sources[2], &test_data, K_NO_WAIT);

	/* Source[2] has only immediate sinks */
	zassert_equal(atomic_get(&ref_count), num_connected[2], "Ref called for each sink");
	zassert_equal(atomic_get(&unref_count), num_immediate[2],
		      "Unref called immediately after immediate handlers");
}

ZTEST(weave_core_unit_test, test_unref_after_queued_process)
{
	int test_data = 0xCAFE;

	weave_source_emit(&sources[0], &test_data, K_NO_WAIT);

	/* After emit: immediate sinks unreffed, queued not yet */
	zassert_equal(atomic_get(&unref_count), num_immediate[0],
		      "Only immediate sinks unreffed after emit");

	process_all_messages();

	/* After processing: all sinks unreffed */
	zassert_equal(atomic_get(&unref_count), num_connected[0],
		      "All sinks unreffed after processing");
}

ZTEST(weave_core_unit_test, test_source_without_ops)
{
	/* Create a source without payload ops */
	struct weave_source no_ops_source = WEAVE_SOURCE_INITIALIZER(no_ops, WV_NO_OPS);
	struct weave_sink test_sink =
		WEAVE_SINK_INITIALIZER(capture_handler, WV_IMMEDIATE, &captures[9], WV_NO_OPS);

	/* Manually connect (can't use WEAVE_CONNECT for runtime sources) */
	static struct weave_connection manual_conn;
	manual_conn.source = &no_ops_source;
	manual_conn.sink = &test_sink;
	sys_slist_append(&no_ops_source.sinks, &manual_conn.node);

	int test_data = 0x5555;
	int ret = weave_source_emit(&no_ops_source, &test_data, K_NO_WAIT);

	/* Should succeed with single sink */
	zassert_equal(ret, 1, "Should deliver to 1 sink without ops");
	zassert_equal(atomic_get(&captures[9].count), 1, "Handler should be called");
}

ZTEST(weave_core_unit_test, test_source_without_ops_multi_sink_error)
{
	/* Create a source without payload ops */
	struct weave_source no_ops_source = WEAVE_SOURCE_INITIALIZER(no_ops, WV_NO_OPS);
	struct weave_sink sink1 =
		WEAVE_SINK_INITIALIZER(capture_handler, WV_IMMEDIATE, &captures[8], WV_NO_OPS);
	struct weave_sink sink2 =
		WEAVE_SINK_INITIALIZER(capture_handler, WV_IMMEDIATE, &captures[9], WV_NO_OPS);

	/* Manually connect two sinks */
	static struct weave_connection conn1, conn2;
	conn1.source = &no_ops_source;
	conn1.sink = &sink1;
	conn2.source = &no_ops_source;
	conn2.sink = &sink2;
	sys_slist_append(&no_ops_source.sinks, &conn1.node);
	sys_slist_append(&no_ops_source.sinks, &conn2.node);

	int test_data = 0x6666;
	int ret = weave_source_emit(&no_ops_source, &test_data, K_NO_WAIT);

	/* Should fail - can't have multiple sinks without ops (no ref counting) */
	zassert_equal(ret, -EINVAL, "Should reject multiple sinks without ops");
}

ZTEST(weave_core_unit_test, test_ops_with_null_ref)
{
	/* Source with ops that has NULL ref - tests line 31 branch (ops && ops->ref) */
	struct weave_source source = WEAVE_SOURCE_INITIALIZER(null_ref, &test_ops_unref_only);
	struct weave_sink sink =
		WEAVE_SINK_INITIALIZER(capture_handler, WV_IMMEDIATE, &captures[8], WV_NO_OPS);

	static struct weave_connection conn;
	conn.source = &source;
	conn.sink = &sink;
	sys_slist_append(&source.sinks, &conn.node);

	reset_signal_counters();
	int test_data = 0x8888;
	int ret = weave_source_emit(&source, &test_data, K_NO_WAIT);

	/* Should succeed - ref is skipped when NULL */
	zassert_equal(ret, 1, "Should deliver without ref");
	zassert_equal(atomic_get(&ref_count), 0, "Ref not called when NULL");
	zassert_equal(atomic_get(&unref_count), 1, "Unref still called");
	zassert_equal(atomic_get(&captures[8].count), 1, "Handler called");
}

ZTEST(weave_core_unit_test, test_ops_with_null_unref_immediate)
{
	/* Source with ops that has NULL unref - tests line 42 branch (ops && ops->unref) */
	struct weave_source source = WEAVE_SOURCE_INITIALIZER(null_unref, &test_ops_ref_only);
	struct weave_sink sink =
		WEAVE_SINK_INITIALIZER(capture_handler, WV_IMMEDIATE, &captures[8], WV_NO_OPS);

	static struct weave_connection conn;
	conn.source = &source;
	conn.sink = &sink;
	sys_slist_append(&source.sinks, &conn.node);

	reset_signal_counters();
	int test_data = 0x9999;
	int ret = weave_source_emit(&source, &test_data, K_NO_WAIT);

	/* Should succeed - unref is skipped when NULL */
	zassert_equal(ret, 1, "Should deliver without unref");
	zassert_equal(atomic_get(&ref_count), 1, "Ref called");
	zassert_equal(atomic_get(&unref_count), 0, "Unref not called when NULL");
	zassert_equal(atomic_get(&captures[8].count), 1, "Handler called");
}

/* =============================================================================
 * Filtering Tests (using payload ops ref)
 * =============================================================================
 */

ZTEST(weave_core_unit_test, test_ref_filter_skips_sink)
{
	/* Create source with filtering ops */
	struct weave_source filter_source = WEAVE_SOURCE_INITIALIZER(filter, &test_filter_ops);

	/* Sinks with different user_data - NULL rejected, non-NULL accepted */
	struct weave_sink sink_reject =
		WEAVE_SINK_INITIALIZER(capture_handler, WV_IMMEDIATE, NULL, WV_NO_OPS);
	struct weave_sink sink_accept =
		WEAVE_SINK_INITIALIZER(capture_handler, WV_IMMEDIATE, &captures[9], WV_NO_OPS);

	static struct weave_connection conn1, conn2;
	conn1.source = &filter_source;
	conn1.sink = &sink_reject;
	conn2.source = &filter_source;
	conn2.sink = &sink_accept;
	sys_slist_append(&filter_source.sinks, &conn1.node);
	sys_slist_append(&filter_source.sinks, &conn2.node);

	reset_signal_counters();

	int test_data = 0x7777;
	int ret = weave_source_emit(&filter_source, &test_data, K_NO_WAIT);

	/* Only 1 should succeed (sink_accept) */
	zassert_equal(ret, 1, "Only 1 sink should accept");
	zassert_equal(atomic_get(&filter_count), 2, "Filter checked for both sinks");
	zassert_equal(atomic_get(&ref_count), 1, "Only 1 ref taken");
	zassert_equal(atomic_get(&captures[8].count), 0, "Rejected sink not called");
	zassert_equal(atomic_get(&captures[9].count), 1, "Accepted sink called");
}

/* =============================================================================
 * Queue Handling Tests
 * =============================================================================
 */

ZTEST(weave_core_unit_test, test_process_messages_empty_queue)
{
	int ret = weave_process_messages(&test_queue, K_NO_WAIT);
	zassert_equal(ret, 0, "Empty queue should return 0 processed");
}

ZTEST(weave_core_unit_test, test_multiple_messages)
{
	const int num_sends = 5;

	for (int i = 0; i < num_sends; i++) {
		int test_data = 0x100 + i;
		weave_source_emit(&sources[0], &test_data, K_NO_WAIT);
	}

	/* Immediate sinks called during emit */
	zassert_equal(atomic_get(&captures[2].count), num_sends,
		      "Immediate sink should have all messages");

	/* Queued sinks pending */
	zassert_equal(k_msgq_num_used_get(&test_queue), num_sends * num_queued[0],
		      "Queue should have pending messages");

	process_all_messages();

	/* All sinks should now have all messages */
	zassert_equal(atomic_get(&captures[0].count), num_sends, "Queued sink[0]");
	zassert_equal(atomic_get(&captures[1].count), num_sends, "Queued sink[1]");
}

ZTEST(weave_core_unit_test, test_queue_overflow)
{
	size_t queued_per_emit = num_queued[0]; /* 2 for source[0] */

	/* Fill the queue by sending enough messages */
	size_t sent = 0;
	while (k_msgq_num_free_get(&test_queue) >= queued_per_emit) {
		int test_data = sent;
		int ret = weave_source_emit(&sources[0], &test_data, K_NO_WAIT);
		zassert_equal(ret, num_connected[0], "Should deliver all");
		sent++;
	}

	zassert_true(k_msgq_num_free_get(&test_queue) < queued_per_emit,
		     "Queue should be nearly full");

	/* Next emit should only deliver to immediate sinks (queued fail) */
	int overflow_data = 0xFFFF;
	int ret = weave_source_emit(&sources[0], &overflow_data, K_NO_WAIT);
	zassert_equal(ret, num_immediate[0], "Only immediate sinks when queue full");
}

ZTEST(weave_core_unit_test, test_queue_overflow_unref_called)
{
	/* Tests line 57: unref is called when queue put fails */
	/* Uses tiny_queue defined at file scope (size 1) */
	k_msgq_purge(&tiny_queue);

	struct weave_source source = WEAVE_SOURCE_INITIALIZER(overflow, &test_ops);
	struct weave_sink sink =
		WEAVE_SINK_INITIALIZER(capture_handler, &tiny_queue, &captures[8], &test_ops);

	static struct weave_connection conn;
	conn.source = &source;
	conn.sink = &sink;
	sys_slist_init(&source.sinks);
	sys_slist_append(&source.sinks, &conn.node);

	reset_signal_counters();

	/* First emit fills the queue */
	int data1 = 0x1111;
	int ret = weave_source_emit(&source, &data1, K_NO_WAIT);
	zassert_equal(ret, 1, "First emit should succeed");
	zassert_equal(atomic_get(&ref_count), 1, "Ref called");
	zassert_equal(atomic_get(&unref_count), 0, "No unref yet (queued)");

	/* Second emit should fail - queue full, unref must be called */
	int data2 = 0x2222;
	ret = weave_source_emit(&source, &data2, K_NO_WAIT);
	zassert_equal(ret, 0, "Second emit should fail (queue full)");
	zassert_equal(atomic_get(&ref_count), 2, "Ref called for both");
	zassert_equal(atomic_get(&unref_count), 1, "Unref called on failure (line 57)");

	/* Drain queue and verify first message is processed */
	int processed = weave_process_messages(&tiny_queue, K_NO_WAIT);
	zassert_equal(processed, 1, "One message processed");
	zassert_equal(atomic_get(&unref_count), 2, "Unref called for processed message too");
}

ZTEST(weave_core_unit_test, test_queue_overflow_no_unref)
{
	/* Tests line 57 FALSE branch: ops->unref is NULL when queue fails */
	k_msgq_purge(&tiny_queue);

	struct weave_source source =
		WEAVE_SOURCE_INITIALIZER(overflow_no_unref, &test_ops_ref_only);
	struct weave_sink sink = WEAVE_SINK_INITIALIZER(capture_handler, &tiny_queue, &captures[8],
							&test_ops_ref_only);

	static struct weave_connection conn2;
	conn2.source = &source;
	conn2.sink = &sink;
	sys_slist_init(&source.sinks);
	sys_slist_append(&source.sinks, &conn2.node);

	reset_signal_counters();

	/* First emit fills the queue */
	int data1 = 0x3333;
	int ret = weave_source_emit(&source, &data1, K_NO_WAIT);
	zassert_equal(ret, 1, "First emit should succeed");

	/* Second emit should fail - queue full, but unref is NULL so not called */
	int data2 = 0x4444;
	ret = weave_source_emit(&source, &data2, K_NO_WAIT);
	zassert_equal(ret, 0, "Second emit should fail (queue full)");
	zassert_equal(atomic_get(&ref_count), 2, "Ref called for both");
	zassert_equal(atomic_get(&unref_count), 0, "Unref NOT called (NULL)");

	/* Drain queue */
	weave_process_messages(&tiny_queue, K_NO_WAIT);
}

ZTEST(weave_core_unit_test, test_queue_overflow_null_ops)
{
	/* Tests line 57: ops is NULL when queue fails (short-circuit) */
	k_msgq_purge(&tiny_queue);

	/* Source without ops can only have one sink */
	struct weave_source source = WEAVE_SOURCE_INITIALIZER(null_ops, WV_NO_OPS);
	struct weave_sink sink =
		WEAVE_SINK_INITIALIZER(capture_handler, &tiny_queue, &captures[8], WV_NO_OPS);

	static struct weave_connection conn3;
	conn3.source = &source;
	conn3.sink = &sink;
	sys_slist_init(&source.sinks);
	sys_slist_append(&source.sinks, &conn3.node);

	reset_signal_counters();

	/* First emit fills the queue */
	int data1 = 0x5555;
	int ret = weave_source_emit(&source, &data1, K_NO_WAIT);
	zassert_equal(ret, 1, "First emit should succeed");

	/* Second emit should fail - queue full, ops is NULL so no unref */
	int data2 = 0x6666;
	ret = weave_source_emit(&source, &data2, K_NO_WAIT);
	zassert_equal(ret, 0, "Second emit should fail (queue full)");
	zassert_equal(atomic_get(&unref_count), 0, "Unref NOT called (NULL ops)");

	/* Drain queue */
	weave_process_messages(&tiny_queue, K_NO_WAIT);
}

/* =============================================================================
 * Direct Sink Send Tests
 * =============================================================================
 */

ZTEST(weave_core_unit_test, test_sink_send_immediate)
{
	int test_data = 0xAAAA;

	int ret = weave_sink_send(&sinks[2], &test_data, K_NO_WAIT);
	zassert_equal(ret, 0, "Direct send should succeed");
	zassert_equal(atomic_get(&captures[2].count), 1, "Handler should be called");
	zassert_equal(captures[2].last_ptr, &test_data, "Should receive correct ptr");
}

ZTEST(weave_core_unit_test, test_sink_send_queued)
{
	int test_data = 0xBBBB;

	int ret = weave_sink_send(&sinks[0], &test_data, K_NO_WAIT);
	zassert_equal(ret, 0, "Direct send should succeed");
	zassert_equal(atomic_get(&captures[0].count), 0, "Handler not called yet");

	process_all_messages();

	zassert_equal(atomic_get(&captures[0].count), 1, "Handler called after process");
	zassert_equal(captures[0].last_ptr, &test_data, "Should receive correct ptr");
}

ZTEST(weave_core_unit_test, test_process_queued_event_null_sink)
{
	/* Tests that processing handles corrupt event with NULL sink gracefully */
	k_msgq_purge(&null_sink_queue);

	struct weave_event corrupt_event = {
		.sink = NULL,
		.ptr = (void *)0xDEAD,
	};
	int ret = k_msgq_put(&null_sink_queue, &corrupt_event, K_NO_WAIT);
	zassert_equal(ret, 0, "Put should succeed");

	int processed = weave_process_messages(&null_sink_queue, K_NO_WAIT);
	zassert_equal(processed, 0, "No valid messages processed");
	zassert_equal(k_msgq_num_used_get(&null_sink_queue), 0, "Queue should be drained");
}

ZTEST(weave_core_unit_test, test_process_queued_event_null_handler)
{
	/* Tests that processing handles event with NULL handler gracefully */
	k_msgq_purge(&null_handler_queue);

	struct weave_sink bad_sink =
		WEAVE_SINK_INITIALIZER(NULL, &null_handler_queue, NULL, WV_NO_OPS);

	struct weave_event corrupt_event = {
		.sink = &bad_sink,
		.ptr = (void *)0xBEEF,
	};
	int ret = k_msgq_put(&null_handler_queue, &corrupt_event, K_NO_WAIT);
	zassert_equal(ret, 0, "Put should succeed");

	int processed = weave_process_messages(&null_handler_queue, K_NO_WAIT);
	zassert_equal(processed, 0, "Event with NULL handler should be skipped");
	zassert_equal(k_msgq_num_used_get(&null_handler_queue), 0, "Queue should be drained");
}

ZTEST(weave_core_unit_test, test_process_queued_sink_without_ops)
{
	/* Tests line 130: if (sink->ops && sink->ops->unref) with NULL ops */
	k_msgq_purge(&no_ops_queue);

	/* Create sink without ops */
	struct weave_sink no_ops_sink =
		WEAVE_SINK_INITIALIZER(capture_handler, &no_ops_queue, &captures[8], WV_NO_OPS);

	/* Put event via sink_send */
	reset_signal_counters();
	int test_data = 0xCCCC;
	int ret = weave_sink_send(&no_ops_sink, &test_data, K_NO_WAIT);
	zassert_equal(ret, 0, "Send should succeed");

	/* Process - should call handler but NOT unref (no ops) */
	int processed = weave_process_messages(&no_ops_queue, K_NO_WAIT);
	zassert_equal(processed, 1, "One message processed");
	zassert_equal(atomic_get(&captures[8].count), 1, "Handler called");
	zassert_equal(atomic_get(&unref_count), 0, "Unref NOT called (no ops)");
}

ZTEST(weave_core_unit_test, test_process_queued_sink_ops_without_unref)
{
	/* Tests line 130: if (sink->ops && sink->ops->unref) with ops but NULL unref */
	k_msgq_purge(&no_unref_queue);

	/* Create sink with ops that has NULL unref */
	struct weave_sink sink = WEAVE_SINK_INITIALIZER(capture_handler, &no_unref_queue,
							&captures[8], &test_ops_ref_only);

	/* Put event via sink_send */
	reset_signal_counters();
	int test_data = 0xDDDD;
	int ret = weave_sink_send(&sink, &test_data, K_NO_WAIT);
	zassert_equal(ret, 0, "Send should succeed");

	/* Process - should call handler but NOT unref (NULL unref) */
	int processed = weave_process_messages(&no_unref_queue, K_NO_WAIT);
	zassert_equal(processed, 1, "One message processed");
	zassert_equal(atomic_get(&captures[8].count), 1, "Handler called");
	zassert_equal(atomic_get(&unref_count), 0, "Unref NOT called (NULL unref)");
}

/* =============================================================================
 * Input Validation Tests
 * =============================================================================
 */

ZTEST(weave_core_unit_test, test_emit_null_source)
{
	int test_data = 0x1234;
	int ret = weave_source_emit(NULL, &test_data, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject NULL source");
}

ZTEST(weave_core_unit_test, test_emit_null_ptr)
{
	int ret = weave_source_emit(&sources[0], NULL, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject NULL ptr");
}

ZTEST(weave_core_unit_test, test_sink_send_null_sink)
{
	int test_data = 0x1234;
	int ret = weave_sink_send(NULL, &test_data, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject NULL sink");
}

ZTEST(weave_core_unit_test, test_sink_send_null_ptr)
{
	int ret = weave_sink_send(&sinks[0], NULL, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject NULL ptr");
}

ZTEST(weave_core_unit_test, test_process_messages_null_queue)
{
	int ret = weave_process_messages(NULL, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject NULL queue");
}

/* =============================================================================
 * Defensive Programming Tests
 * =============================================================================
 */

ZTEST(weave_core_unit_test, test_sink_null_handler)
{
	/* Tests that emit skips sinks with NULL handler */
	weave_handler_t original = sinks[2].handler;
	sinks[2].handler = NULL;

	int test_data = 0xBAD;
	int ret = weave_source_emit(&sources[0], &test_data, K_NO_WAIT);

	zassert_equal(ret, num_connected[0] - 1, "Should skip sink with NULL handler");

	sinks[2].handler = original;
}

ZTEST(weave_core_unit_test, test_connection_null_sink)
{
	/* Tests that emit skips connections with NULL sink */
	struct weave_connection *conn =
		SYS_SLIST_PEEK_HEAD_CONTAINER(&sources[0].sinks, conn, node);
	zassert_not_null(conn, "Should have connection");

	struct weave_sink *original = conn->sink;
	conn->sink = NULL;

	int test_data = 0xBAD;
	int ret = weave_source_emit(&sources[0], &test_data, K_NO_WAIT);

	zassert_equal(ret, num_connected[0] - 1, "Should skip connection with NULL sink");

	conn->sink = original;
}

/* =============================================================================
 * Stress Tests
 * =============================================================================
 */

ZTEST(weave_core_unit_test, test_many_emits)
{
	const int num_emits = 20;

	for (int i = 0; i < num_emits; i++) {
		int test_data = i;
		weave_source_emit(&sources[2], &test_data, K_NO_WAIT);
	}

	/* Source[2] only has immediate sinks */
	zassert_equal(atomic_get(&captures[2].count), num_emits, "Immediate sink[2]");
	zassert_equal(atomic_get(&captures[3].count), num_emits, "Immediate sink[3]");
	zassert_equal(atomic_get(&ref_count), num_emits * num_connected[2], "All refs taken");
	zassert_equal(atomic_get(&unref_count), num_emits * num_connected[2], "All unrefs done");
}

ZTEST(weave_core_unit_test, test_concurrent_sources)
{
	const int num_emits = 5;

	/* Emit from multiple sources */
	for (int i = 0; i < num_emits; i++) {
		int data0 = 0x1000 + i;
		int data1 = 0x2000 + i;
		int data2 = 0x3000 + i;

		weave_source_emit(&sources[0], &data0, K_NO_WAIT);
		weave_source_emit(&sources[1], &data1, K_NO_WAIT);
		weave_source_emit(&sources[2], &data2, K_NO_WAIT);
	}

	process_all_messages();

	/* Verify each sink received from expected sources */
	/* sink[0]: source[0], source[1] -> 2 * num_emits */
	/* sink[1]: source[0] -> num_emits */
	/* sink[2]: source[0], source[2] -> 2 * num_emits */
	/* sink[3]: source[1], source[2] -> 2 * num_emits */
	zassert_equal(atomic_get(&captures[0].count), 2 * num_emits, "sink[0]");
	zassert_equal(atomic_get(&captures[1].count), num_emits, "sink[1]");
	zassert_equal(atomic_get(&captures[2].count), 2 * num_emits, "sink[2]");
	zassert_equal(atomic_get(&captures[3].count), 2 * num_emits, "sink[3]");
}
