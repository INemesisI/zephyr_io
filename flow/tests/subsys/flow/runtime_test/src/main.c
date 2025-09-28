/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/net/buf.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>
#include <zephyr_io/flow/flow.h>

#ifdef CONFIG_FLOW_RUNTIME_OBSERVERS

/* Test configuration constants */
#define TEST_BUFFER_POOL_SIZE   16
#define TEST_BUFFER_SIZE        64
#define TEST_RUNTIME_QUEUE_SIZE 32
#define TEST_VALUE_1            0x12345678
#define TEST_VALUE_2            0x87654321
#define TEST_VALUE_3            0xDEADBEEF

/* Define buffer pool for testing */
NET_BUF_POOL_DEFINE(runtime_pool, TEST_BUFFER_POOL_SIZE, TEST_BUFFER_SIZE, 4, NULL);

/* =============================================================================
 * Handler Buffer Management Rules:
 * - ALL handlers: MUST NOT call net_buf_unref() - framework handles it
 * - The flow framework automatically manages buffer references
 * =============================================================================
 */

/* Test context for counting packets */
struct test_capture {
	atomic_t count;
	uint32_t last_value;
};

/* Test capture contexts */
static struct test_capture runtime_capture1 = {
	.count = ATOMIC_INIT(0),
	.last_value = 0,
};

static struct test_capture runtime_capture2 = {
	.count = ATOMIC_INIT(0),
	.last_value = 0,
};

/* Define sources for runtime tests */
FLOW_SOURCE_DEFINE(runtime_source);
FLOW_SOURCE_DEFINE(runtime_source2);

/* IMMEDIATE handler - processes synchronously (no unref needed) */
static void runtime_immediate_handler(struct flow_sink *sink, struct net_buf *buf)
{
	struct test_capture *capture = (struct test_capture *)sink->user_data;

	if (capture && buf && buf->data && buf->len >= 4) {
		atomic_inc(&capture->count);
		capture->last_value = sys_le32_to_cpu(*(uint32_t *)buf->data);
	}
	/* Buffer unref handled by framework for ALL handlers */
}

/* QUEUED handler - deferred processing (no unref needed) */
static void runtime_queued_handler(struct flow_sink *sink, struct net_buf *buf)
{
	struct test_capture *capture = (struct test_capture *)sink->user_data;

	if (capture && buf && buf->data && buf->len >= 4) {
		atomic_inc(&capture->count);
		capture->last_value = sys_le32_to_cpu(*(uint32_t *)buf->data);
	}
	/* Buffer unref handled by framework for ALL handlers */
}

/* Define event queue for queued processing */
FLOW_EVENT_QUEUE_DEFINE(runtime_queue, TEST_RUNTIME_QUEUE_SIZE);

/* Define sinks using proper macros */
FLOW_SINK_DEFINE_IMMEDIATE(runtime_immediate_sink, runtime_immediate_handler);
FLOW_SINK_DEFINE_IMMEDIATE(runtime_secondary_sink, runtime_immediate_handler);
FLOW_SINK_DEFINE_QUEUED(runtime_queued_sink, runtime_queued_handler, runtime_queue);

/* Static connections for persistent testing */
static struct flow_connection static_conn1;
static struct flow_connection static_conn2;

/* Initialize sink user data */
static void init_sinks(void)
{
	runtime_immediate_sink.user_data = &runtime_capture1;
	runtime_secondary_sink.user_data = &runtime_capture2;
	runtime_queued_sink.user_data = &runtime_capture1; /* Share capture with immediate */
}

/* Process all pending events in the runtime queue */
static void process_all_events(void)
{
	int count = 0;

	while (flow_event_process(&runtime_queue, K_NO_WAIT) == 0) {
		count++;
		if (count > TEST_RUNTIME_QUEUE_SIZE * 2) {
			/* Safety check to avoid infinite loop */
			break;
		}
	}
}

/* Reset capture context */
static void reset_capture(struct test_capture *capture)
{
	atomic_clear(&capture->count);
	capture->last_value = 0;
}

/* Helper to send a test packet with a value */
static int send_test_packet(struct flow_source *source, uint32_t value)
{
	struct net_buf *buf;
	int ret;

	buf = net_buf_alloc(&runtime_pool, K_NO_WAIT);
	if (!buf) {
		return -ENOMEM;
	}

	net_buf_add_le32(buf, value);
	ret = flow_source_send(source, buf, K_NO_WAIT);
	net_buf_unref(buf);

	return ret;
}

ZTEST(flow_runtime, test_basic_add_remove)
{
	int ret;
	struct flow_connection conn;

	/* Setup connection */
	conn.source = &runtime_source;
	conn.sink = &runtime_immediate_sink;
	conn.node.next = NULL;

	/* Add connection */
	ret = flow_connection_add(&conn);
	zassert_equal(ret, 0, "Failed to add runtime connection");

	/* Send packet and verify delivery */
	ret = send_test_packet(&runtime_source, TEST_VALUE_1);
	zassert_equal(ret, 1, "Should deliver to exactly 1 sink");
	zassert_equal(runtime_capture1.last_value, TEST_VALUE_1, "Wrong value received");
	zassert_equal(atomic_get(&runtime_capture1.count), 1, "Wrong count");

	/* Remove connection */
	ret = flow_connection_remove(&conn);
	zassert_equal(ret, 0, "Failed to remove connection");

	/* Verify disconnected - send should deliver to 0 sinks */
	ret = send_test_packet(&runtime_source, TEST_VALUE_2);
	zassert_equal(ret, 0, "Should deliver to 0 sinks after removal");
	zassert_equal(runtime_capture1.last_value, TEST_VALUE_1, "Value should not change");
	zassert_equal(atomic_get(&runtime_capture1.count), 1, "Count should not change");
}

ZTEST(flow_runtime, test_multiple_connections)
{
	int ret;
	struct flow_connection conn1, conn2, conn3;

	/* Setup multiple connections */
	conn1.source = &runtime_source;
	conn1.sink = &runtime_immediate_sink;
	conn1.node.next = NULL;

	conn2.source = &runtime_source;
	conn2.sink = &runtime_queued_sink;
	conn2.node.next = NULL;

	conn3.source = &runtime_source;
	conn3.sink = &runtime_secondary_sink;
	conn3.node.next = NULL;

	/* Add all connections */
	ret = flow_connection_add(&conn1);
	zassert_equal(ret, 0, "Failed to add connection 1");

	ret = flow_connection_add(&conn2);
	zassert_equal(ret, 0, "Failed to add connection 2");

	ret = flow_connection_add(&conn3);
	zassert_equal(ret, 0, "Failed to add connection 3");

	/* Send packet - should go to all three */
	ret = send_test_packet(&runtime_source, TEST_VALUE_3);
	zassert_equal(ret, 3, "Should deliver to exactly 3 sinks");

	/* Verify immediate sinks received (capture1 gets both immediate and queued)
	 */
	zassert_equal(runtime_capture1.last_value, TEST_VALUE_3, "Wrong value in capture1");
	zassert_equal(runtime_capture2.last_value, TEST_VALUE_3, "Wrong value in capture2");

	/* Process queue for queued sink */
	ret = flow_event_process(&runtime_queue, K_NO_WAIT);
	zassert_equal(ret, 0, "Failed to process queued event");

	/* capture1 should have count of 2 (immediate + queued) */
	zassert_equal(atomic_get(&runtime_capture1.count), 2, "capture1 should get both");
	zassert_equal(atomic_get(&runtime_capture2.count), 1, "capture2 should get one");

	/* Remove middle connection and test again */
	ret = flow_connection_remove(&conn2);
	zassert_equal(ret, 0, "Failed to remove connection 2");

	/* Reset captures for next test */
	reset_capture(&runtime_capture1);
	reset_capture(&runtime_capture2);

	ret = send_test_packet(&runtime_source, TEST_VALUE_1);
	zassert_equal(ret, 2, "Should deliver to exactly 2 sinks");

	zassert_equal(runtime_capture1.last_value, TEST_VALUE_1, "Wrong value in capture1");
	zassert_equal(runtime_capture2.last_value, TEST_VALUE_1, "Wrong value in capture2");

	/* Process queue - should be empty since queued sink was disconnected */
	ret = flow_event_process(&runtime_queue, K_NO_WAIT);
	zassert_equal(ret, -EAGAIN, "Queue should be empty");

	/* Clean up remaining connections */
	flow_connection_remove(&conn1);
	flow_connection_remove(&conn3);
}

ZTEST(flow_runtime, test_duplicate_connection)
{
	int ret;
	struct flow_connection conn1, conn2;

	/* Setup first connection */
	conn1.source = &runtime_source;
	conn1.sink = &runtime_immediate_sink;
	conn1.node.next = NULL;

	ret = flow_connection_add(&conn1);
	zassert_equal(ret, 0, "Failed to add first connection");

	/* Try to add duplicate (same source and sink) */
	conn2.source = &runtime_source;
	conn2.sink = &runtime_immediate_sink;
	conn2.node.next = NULL;

	ret = flow_connection_add(&conn2);
	zassert_equal(ret, -EALREADY, "Should reject duplicate connection");

	/* Clean up */
	flow_connection_remove(&conn1);
}

ZTEST(flow_runtime, test_connection_persistence)
{
	int ret;

	/* Use static connections that persist across function calls */
	static_conn1.source = &runtime_source;
	static_conn1.sink = &runtime_immediate_sink;
	static_conn1.node.next = NULL;

	static_conn2.source = &runtime_source2;
	static_conn2.sink = &runtime_secondary_sink; /* Different sink for clarity */
	static_conn2.node.next = NULL;

	/* Add connections */
	ret = flow_connection_add(&static_conn1);
	zassert_equal(ret, 0, "Failed to add static connection 1");

	ret = flow_connection_add(&static_conn2);
	zassert_equal(ret, 0, "Failed to add static connection 2");

	/* Send through first source */
	ret = send_test_packet(&runtime_source, TEST_VALUE_1);
	zassert_equal(ret, 1, "Should deliver to 1 sink on source 1");
	zassert_equal(runtime_capture1.last_value, TEST_VALUE_1, "Wrong value in capture1");

	/* Send through second source */
	ret = send_test_packet(&runtime_source2, TEST_VALUE_2);
	zassert_equal(ret, 1, "Should deliver to 1 sink on source 2");
	zassert_equal(runtime_capture2.last_value, TEST_VALUE_2, "Wrong value in capture2");

	/* Connections should still exist - send again */
	ret = send_test_packet(&runtime_source, TEST_VALUE_3);
	zassert_equal(ret, 1, "Connection should persist");
	zassert_equal(runtime_capture1.last_value, TEST_VALUE_3, "Connection still working");

	/* Verify second connection also persists */
	ret = send_test_packet(&runtime_source2, TEST_VALUE_1);
	zassert_equal(ret, 1, "Connection 2 should persist");
	zassert_equal(runtime_capture2.last_value, TEST_VALUE_1, "Connection 2 still working");

	/* Clean up */
	flow_connection_remove(&static_conn1);
	flow_connection_remove(&static_conn2);
}

ZTEST(flow_runtime, test_null_parameters)
{
	int ret;
	struct flow_connection conn;

	/* Test NULL connection */
	ret = flow_connection_add(NULL);
	zassert_equal(ret, -EINVAL, "Should reject NULL connection");

	ret = flow_connection_remove(NULL);
	zassert_equal(ret, -EINVAL, "Should reject NULL connection");

	/* Test connection with NULL source */
	conn.source = NULL;
	conn.sink = &runtime_immediate_sink;
	conn.node.next = NULL;

	ret = flow_connection_add(&conn);
	zassert_equal(ret, -EINVAL, "Should reject NULL source");

	/* Test connection with NULL sink */
	conn.source = &runtime_source;
	conn.sink = NULL;
	conn.node.next = NULL;

	ret = flow_connection_add(&conn);
	zassert_equal(ret, -EINVAL, "Should reject NULL sink");
}

ZTEST(flow_runtime, test_already_connected)
{
	int ret;
	struct flow_connection conn;

	/* Setup and add connection */
	conn.source = &runtime_source;
	conn.sink = &runtime_immediate_sink;
	conn.node.next = NULL;

	ret = flow_connection_add(&conn);
	zassert_equal(ret, 0, "Failed to add connection");

	/* Try to add same connection again without removing */
	ret = flow_connection_add(&conn);
	zassert_equal(ret, -EBUSY, "Should reject already-connected connection");

	/* Clean up */
	flow_connection_remove(&conn);
}

ZTEST(flow_runtime, test_remove_not_connected)
{
	int ret;
	struct flow_connection conn;

	/* Setup connection but don't add it */
	conn.source = &runtime_source;
	conn.sink = &runtime_immediate_sink;
	conn.node.next = NULL;

	/* Try to remove non-existent connection */
	ret = flow_connection_remove(&conn);
	zassert_equal(ret, -ENOENT, "Should reject removing non-existent connection");
}

ZTEST(flow_runtime, test_double_remove)
{
	int ret;
	struct flow_connection conn;

	/* Setup and add connection */
	conn.source = &runtime_source;
	conn.sink = &runtime_immediate_sink;
	conn.node.next = NULL;

	ret = flow_connection_add(&conn);
	zassert_equal(ret, 0, "Failed to add connection");

	/* Remove it once */
	ret = flow_connection_remove(&conn);
	zassert_equal(ret, 0, "Failed to remove connection");

	/* Try to remove again */
	ret = flow_connection_remove(&conn);
	zassert_equal(ret, -ENOENT, "Should reject double removal");
}

#ifdef CONFIG_FLOW_RUNTIME_STACK_CHECK
ZTEST(flow_runtime, test_stack_allocation_detection)
{
	int ret;
	struct flow_connection stack_conn;

	/* Create stack-allocated connection */
	stack_conn.source = &runtime_source;
	stack_conn.sink = &runtime_immediate_sink;
	stack_conn.node.next = NULL;

	/* Try to add it - should be rejected in non-test builds */
	ret = flow_connection_add(&stack_conn);

/* In test builds, it's allowed but logged as an error */
#ifdef CONFIG_ZTEST
	zassert_equal(ret, 0, "Stack allocation allowed in test mode");
	flow_connection_remove(&stack_conn);
#else
	zassert_equal(ret, -EINVAL, "Stack allocation should be rejected");
#endif
}

ZTEST(flow_runtime, test_heap_allocation)
{
	int ret;
	struct flow_connection *heap_conn;

	/* Allocate connection on heap */
	heap_conn = k_malloc(sizeof(struct flow_connection));
	zassert_not_null(heap_conn, "Failed to allocate on heap");

	heap_conn->source = &runtime_source;
	heap_conn->sink = &runtime_immediate_sink;
	heap_conn->node.next = NULL;

	/* Should be accepted (not on stack) */
	ret = flow_connection_add(heap_conn);
	zassert_equal(ret, 0, "Heap allocation should be accepted");

	/* Clean up */
	flow_connection_remove(heap_conn);
	k_free(heap_conn);
}
#endif /* CONFIG_FLOW_RUNTIME_STACK_CHECK */

ZTEST(flow_runtime, test_cross_source_connections)
{
	int ret;
	struct flow_connection conn1, conn2, conn3;

	/* Connect different sources to same sink */
	conn1.source = &runtime_source;
	conn1.sink = &runtime_immediate_sink;
	conn1.node.next = NULL;

	conn2.source = &runtime_source2;
	conn2.sink = &runtime_immediate_sink;
	conn2.node.next = NULL;

	/* Connect same source to different sink */
	conn3.source = &runtime_source;
	conn3.sink = &runtime_secondary_sink;
	conn3.node.next = NULL;

	/* Add all connections */
	ret = flow_connection_add(&conn1);
	zassert_equal(ret, 0, "Failed to add connection 1");

	ret = flow_connection_add(&conn2);
	zassert_equal(ret, 0, "Failed to add connection 2");

	ret = flow_connection_add(&conn3);
	zassert_equal(ret, 0, "Failed to add connection 3");

	/* Send from first source - goes to immediate and secondary sinks */
	ret = send_test_packet(&runtime_source, TEST_VALUE_1);
	zassert_equal(ret, 2, "Should deliver to 2 sinks from source 1");
	zassert_equal(atomic_get(&runtime_capture1.count), 1, "capture1 should receive once");
	zassert_equal(atomic_get(&runtime_capture2.count), 1, "capture2 should receive once");

	/* Reset counts for clarity */
	reset_capture(&runtime_capture1);
	reset_capture(&runtime_capture2);

	/* Send from second source - only goes to immediate sink */
	ret = send_test_packet(&runtime_source2, TEST_VALUE_2);
	zassert_equal(ret, 1, "Should deliver to 1 sink from source 2");
	zassert_equal(atomic_get(&runtime_capture1.count), 1,
		      "capture1 should receive from source 2");
	zassert_equal(atomic_get(&runtime_capture2.count), 0, "capture2 should not receive");

	/* Clean up */
	flow_connection_remove(&conn1);
	flow_connection_remove(&conn2);
	flow_connection_remove(&conn3);
}

/* Test setup - called before each test */
static void test_setup(void *fixture)
{
	ARG_UNUSED(fixture);

	/* Initialize sinks on first run */
	static bool initialized = false;
	if (!initialized) {
		init_sinks();
		initialized = true;
	}

	/* Process any pending events from previous test */
	process_all_events();

	/* Reset all capture contexts */
	reset_capture(&runtime_capture1);
	reset_capture(&runtime_capture2);

#ifdef CONFIG_FLOW_STATS
	/* Reset all statistics */
	flow_source_reset_stats(&runtime_source);
	flow_source_reset_stats(&runtime_source2);
	flow_sink_reset_stats(&runtime_immediate_sink);
	flow_sink_reset_stats(&runtime_secondary_sink);
	flow_sink_reset_stats(&runtime_queued_sink);
#endif
}

/* Test teardown - called after each test */
static void test_teardown(void *fixture)
{
	ARG_UNUSED(fixture);

	/* Always drain all pending events to avoid test interference */
	process_all_events();
}

ZTEST_SUITE(flow_runtime, NULL, NULL, test_setup, test_teardown, NULL);

#else /* CONFIG_FLOW_RUNTIME_OBSERVERS */

ZTEST(flow_runtime, test_runtime_disabled)
{
	ztest_test_skip("Runtime observers not enabled");
}

ZTEST_SUITE(flow_runtime, NULL, NULL, NULL, NULL, NULL);

#endif /* CONFIG_FLOW_RUNTIME_OBSERVERS */