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

	if (capture) {
		atomic_inc(&capture->count);
		if (buf->len >= sizeof(uint32_t)) {
			capture->last_value = net_buf_pull_le32(buf);
			net_buf_push_le32(buf, capture->last_value);
		}
	}
	/* Buffer unref handled by framework */
}

/* Define immediate execution sinks */
FLOW_SINK_DEFINE_IMMEDIATE(runtime_immediate_sink, runtime_immediate_handler, &runtime_capture1);

FLOW_SINK_DEFINE_IMMEDIATE(runtime_immediate_sink2, runtime_immediate_handler, &runtime_capture2);

/* Event queue for queued execution */
FLOW_EVENT_QUEUE_DEFINE(runtime_queue, TEST_RUNTIME_QUEUE_SIZE);

/* QUEUED handler - processes asynchronously (no unref needed) */
static void runtime_queued_handler(struct flow_sink *sink, struct net_buf *buf)
{
	struct test_capture *capture = (struct test_capture *)sink->user_data;

	if (capture) {
		atomic_inc(&capture->count);
		if (buf->len >= sizeof(uint32_t)) {
			capture->last_value = net_buf_pull_le32(buf);
			net_buf_push_le32(buf, capture->last_value);
		}
	}
	/* Buffer unref handled by framework */
}

/* Define queued execution sink */
FLOW_SINK_DEFINE_QUEUED(runtime_queued_sink, runtime_queued_handler, runtime_queue,
			&runtime_capture1);

/* Setup function - runs before each test */
static void runtime_test_setup(void *f)
{
	ARG_UNUSED(f);

	/* Reset capture contexts */
	atomic_set(&runtime_capture1.count, 0);
	runtime_capture1.last_value = 0;
	atomic_set(&runtime_capture2.count, 0);
	runtime_capture2.last_value = 0;

	/* Drain any pending messages in queues */
	while (flow_event_process(&runtime_queue, K_NO_WAIT) == 0) {
		/* Drain */
	}
}

/* Teardown function - runs after each test */
static void runtime_test_teardown(void *f)
{
	ARG_UNUSED(f);

	/* Clean up any remaining connections by trying to disconnect all possible combinations */
	/* These might fail but that's okay - we're just cleaning up */
	flow_runtime_disconnect(&runtime_source, &runtime_immediate_sink);
	flow_runtime_disconnect(&runtime_source, &runtime_immediate_sink2);
	flow_runtime_disconnect(&runtime_source, &runtime_queued_sink);
	flow_runtime_disconnect(&runtime_source2, &runtime_immediate_sink);
	flow_runtime_disconnect(&runtime_source2, &runtime_immediate_sink2);
	flow_runtime_disconnect(&runtime_source2, &runtime_queued_sink);
}

/* Helper function to send test packet */
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

/* Test basic connection and disconnection */
ZTEST(flow_runtime, test_basic_connect_disconnect)
{
	int ret;

	/* Connect source to sink */
	ret = flow_runtime_connect(&runtime_source, &runtime_immediate_sink);
	zassert_equal(ret, 0, "Failed to connect");

	/* Send packet and verify delivery */
	ret = send_test_packet(&runtime_source, TEST_VALUE_1);
	zassert_equal(ret, 1, "Should deliver to 1 sink");
	zassert_equal(runtime_capture1.last_value, TEST_VALUE_1, "Wrong value received");
	zassert_equal(atomic_get(&runtime_capture1.count), 1, "Wrong count");

	/* Disconnect */
	ret = flow_runtime_disconnect(&runtime_source, &runtime_immediate_sink);
	zassert_equal(ret, 0, "Failed to disconnect");

	/* Send after disconnect - should deliver to 0 sinks */
	ret = send_test_packet(&runtime_source, TEST_VALUE_2);
	zassert_equal(ret, 0, "Should deliver to 0 sinks after disconnect");
	zassert_equal(runtime_capture1.last_value, TEST_VALUE_1, "Value should not change");
	zassert_equal(atomic_get(&runtime_capture1.count), 1, "Count should not change");
}

/* Test multiple connections */
ZTEST(flow_runtime, test_multiple_connections)
{
	int ret;

	/* Connect source to multiple sinks */
	ret = flow_runtime_connect(&runtime_source, &runtime_immediate_sink);
	zassert_equal(ret, 0, "Failed to connect sink 1");

	ret = flow_runtime_connect(&runtime_source, &runtime_immediate_sink2);
	zassert_equal(ret, 0, "Failed to connect sink 2");

	ret = flow_runtime_connect(&runtime_source, &runtime_queued_sink);
	zassert_equal(ret, 0, "Failed to connect queued sink");

	/* Send packet - should deliver to all 3 sinks */
	ret = send_test_packet(&runtime_source, TEST_VALUE_1);
	zassert_equal(ret, 3, "Should deliver to 3 sinks");

	/* Check immediate sinks received it */
	zassert_equal(runtime_capture1.last_value, TEST_VALUE_1, "Sink 1 wrong value");
	zassert_equal(runtime_capture2.last_value, TEST_VALUE_1, "Sink 2 wrong value");
	/* Only immediate_sink has been called so far (queued_sink is still queued) */
	zassert_equal(atomic_get(&runtime_capture1.count), 1,
		      "Wrong count for capture1 before queue");
	zassert_equal(atomic_get(&runtime_capture2.count), 1, "Wrong count for capture2");

	/* Process queued event */
	ret = flow_event_process(&runtime_queue, K_NO_WAIT);
	zassert_equal(ret, 0, "Failed to process queued event");
	/* Now queued_sink has also been processed, incrementing capture1 again */
	zassert_equal(atomic_get(&runtime_capture1.count), 2,
		      "Wrong count for capture1 after queue");

	/* Disconnect middle sink */
	ret = flow_runtime_disconnect(&runtime_source, &runtime_immediate_sink2);
	zassert_equal(ret, 0, "Failed to disconnect sink 2");

	/* Reset counts */
	atomic_set(&runtime_capture1.count, 0);
	atomic_set(&runtime_capture2.count, 0);

	/* Send another packet - should deliver to 2 sinks now */
	ret = send_test_packet(&runtime_source, TEST_VALUE_2);
	zassert_equal(ret, 2, "Should deliver to 2 sinks after one disconnected");

	/* Clean up remaining connections */
	flow_runtime_disconnect(&runtime_source, &runtime_immediate_sink);
	flow_runtime_disconnect(&runtime_source, &runtime_queued_sink);
}

/* Test duplicate connection detection */
ZTEST(flow_runtime, test_duplicate_connection)
{
	int ret;

	/* First connection should succeed */
	ret = flow_runtime_connect(&runtime_source, &runtime_immediate_sink);
	zassert_equal(ret, 0, "Failed to add first connection");

	/* Duplicate connection should be rejected */
	ret = flow_runtime_connect(&runtime_source, &runtime_immediate_sink);
	zassert_equal(ret, -EALREADY, "Should reject duplicate connection");

	/* Clean up */
	flow_runtime_disconnect(&runtime_source, &runtime_immediate_sink);
}

/* Test invalid parameters */
ZTEST(flow_runtime, test_invalid_params)
{
	int ret;

	/* Test NULL source */
	ret = flow_runtime_connect(NULL, &runtime_immediate_sink);
	zassert_equal(ret, -EINVAL, "Should reject NULL source");

	ret = flow_runtime_disconnect(NULL, &runtime_immediate_sink);
	zassert_equal(ret, -EINVAL, "Should reject NULL source in disconnect");

	/* Test NULL sink */
	ret = flow_runtime_connect(&runtime_source, NULL);
	zassert_equal(ret, -EINVAL, "Should reject NULL sink");

	ret = flow_runtime_disconnect(&runtime_source, NULL);
	zassert_equal(ret, -EINVAL, "Should reject NULL sink in disconnect");
}

/* Test removing non-existent connection */
ZTEST(flow_runtime, test_remove_nonexistent)
{
	int ret;

	/* Try to disconnect non-existent connection */
	ret = flow_runtime_disconnect(&runtime_source, &runtime_immediate_sink);
	zassert_equal(ret, -ENOENT, "Should reject removing non-existent connection");
}

/* Test connection pool exhaustion */
ZTEST(flow_runtime, test_pool_exhaustion)
{
	int ret;
	int connected = 0;

	/* Try to create more connections than pool size allows */
	/* Create unique connections to exhaust the pool */
	/* With pool size 4, we can create 4 unique connections */

	/* Connection 1 */
	ret = flow_runtime_connect(&runtime_source, &runtime_immediate_sink);
	zassert_equal(ret, 0, "Failed to connect 1");
	connected++;

	/* Connection 2 */
	ret = flow_runtime_connect(&runtime_source, &runtime_immediate_sink2);
	zassert_equal(ret, 0, "Failed to connect 2");
	connected++;

	/* Connection 3 */
	ret = flow_runtime_connect(&runtime_source2, &runtime_immediate_sink);
	zassert_equal(ret, 0, "Failed to connect 3");
	connected++;

	/* Connection 4 */
	ret = flow_runtime_connect(&runtime_source2, &runtime_immediate_sink2);
	zassert_equal(ret, 0, "Failed to connect 4");
	connected++;

	/* Connection 5 - should fail with ENOMEM */
	ret = flow_runtime_connect(&runtime_source, &runtime_queued_sink);
	zassert_equal(ret, -ENOMEM, "Should get ENOMEM when pool exhausted");

	/* We should have exactly CONFIG_FLOW_RUNTIME_CONNECTION_POOL_SIZE connections */
	zassert_equal(connected, CONFIG_FLOW_RUNTIME_CONNECTION_POOL_SIZE,
		      "Should have exactly %d connections",
		      CONFIG_FLOW_RUNTIME_CONNECTION_POOL_SIZE);

	/* Clean up - disconnect all */
	flow_runtime_disconnect(&runtime_source, &runtime_immediate_sink);
	flow_runtime_disconnect(&runtime_source, &runtime_immediate_sink2);
	flow_runtime_disconnect(&runtime_source, &runtime_queued_sink);
	flow_runtime_disconnect(&runtime_source2, &runtime_immediate_sink);
	flow_runtime_disconnect(&runtime_source2, &runtime_immediate_sink2);
	flow_runtime_disconnect(&runtime_source2, &runtime_queued_sink);
}

/* Test reconnection after disconnect */
ZTEST(flow_runtime, test_reconnect)
{
	int ret;

	/* Connect */
	ret = flow_runtime_connect(&runtime_source, &runtime_immediate_sink);
	zassert_equal(ret, 0, "Failed to connect");

	/* Disconnect */
	ret = flow_runtime_disconnect(&runtime_source, &runtime_immediate_sink);
	zassert_equal(ret, 0, "Failed to disconnect");

	/* Reconnect - should succeed */
	ret = flow_runtime_connect(&runtime_source, &runtime_immediate_sink);
	zassert_equal(ret, 0, "Failed to reconnect");

	/* Verify it works */
	ret = send_test_packet(&runtime_source, TEST_VALUE_3);
	zassert_equal(ret, 1, "Should deliver to reconnected sink");
	zassert_equal(runtime_capture1.last_value, TEST_VALUE_3, "Wrong value after reconnect");

	/* Clean up */
	flow_runtime_disconnect(&runtime_source, &runtime_immediate_sink);
}

/* Test queued sink with runtime connection */
ZTEST(flow_runtime, test_runtime_queued)
{
	int ret;

	/* Connect to queued sink */
	ret = flow_runtime_connect(&runtime_source, &runtime_queued_sink);
	zassert_equal(ret, 0, "Failed to connect to queued sink");

	/* Send multiple packets */
	ret = send_test_packet(&runtime_source, TEST_VALUE_1);
	zassert_equal(ret, 1, "Failed to send packet 1");

	ret = send_test_packet(&runtime_source, TEST_VALUE_2);
	zassert_equal(ret, 1, "Failed to send packet 2");

	ret = send_test_packet(&runtime_source, TEST_VALUE_3);
	zassert_equal(ret, 1, "Failed to send packet 3");

	/* Process all queued events */
	int processed = 0;
	while (flow_event_process(&runtime_queue, K_NO_WAIT) == 0) {
		processed++;
	}
	zassert_equal(processed, 3, "Should process 3 queued events");
	zassert_equal(atomic_get(&runtime_capture1.count), 3, "Should have received 3 packets");
	zassert_equal(runtime_capture1.last_value, TEST_VALUE_3, "Should have last value");

	/* Clean up */
	flow_runtime_disconnect(&runtime_source, &runtime_queued_sink);
}

ZTEST_SUITE(flow_runtime, NULL, NULL, runtime_test_setup, runtime_test_teardown, NULL);

#else /* !CONFIG_FLOW_RUNTIME_OBSERVERS */

ZTEST_SUITE(flow_runtime_disabled, NULL, NULL, NULL, NULL, NULL);

ZTEST(flow_runtime_disabled, test_disabled)
{
	ztest_test_skip();
}

#endif /* CONFIG_FLOW_RUNTIME_OBSERVERS */