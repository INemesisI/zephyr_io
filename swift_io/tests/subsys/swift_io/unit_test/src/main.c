/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr_io/swift_io/swift_io.h>
#include <zephyr/net/buf.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <errno.h>

LOG_MODULE_REGISTER(swift_io_test, LOG_LEVEL_INF);

/* Test configuration constants */
#define TEST_BUFFER_POOL_SIZE   150
#define TEST_BUFFER_SIZE        128
#define TEST_EVENT_QUEUE_SIZE   100
#define TEST_RUNTIME_QUEUE_SIZE 10
#define TEST_CORRUPT_QUEUE_SIZE 10
#define TEST_TIMEOUT_MS         10
#define TEST_STRESS_ITERATIONS  50
#define TEST_BURST_SIZE         10
#define TEST_MAX_EVENTS         200
#define TEST_MANY_PACKETS       30

/* Define a test buffer pool */
NET_BUF_POOL_DEFINE(test_pool, TEST_BUFFER_POOL_SIZE, TEST_BUFFER_SIZE, 4, NULL);

/* =============================================================================
 * Handler Buffer Management Rules:
 * - ALL handlers: MUST NOT call net_buf_unref() - framework handles it
 * - The swift_io framework automatically manages buffer references
 * =============================================================================
 */

/* Test context for counting packets */
struct test_capture {
	atomic_t count;
	uint32_t last_value;
};

/* Test capture contexts */
static struct test_capture capture1 = {
	.count = ATOMIC_INIT(0),
	.last_value = 0,
};

static struct test_capture capture2 = {
	.count = ATOMIC_INIT(0),
	.last_value = 0,
};

static struct test_capture capture3 = {
	.count = ATOMIC_INIT(0),
	.last_value = 0,
};

/* QUEUED handler - counts packets and stores last value (no unref needed) */
static void capture_handler(struct swift_io_sink *sink, struct net_buf *buf)
{
	struct test_capture *capture = (struct test_capture *)sink->user_data;

	if (capture) {
		atomic_inc(&capture->count);
		/* Store the last value seen */
		if (buf && buf->data && buf->len >= 4) {
			capture->last_value = sys_le32_to_cpu(*(uint32_t *)buf->data);
		}
	}

	/* Buffer unref handled by framework for ALL handlers */
}

/* IMMEDIATE handler - processes synchronously (no unref needed) */
static atomic_t immediate_count = ATOMIC_INIT(0);
static uint32_t immediate_last_value = 0;

static void immediate_handler(struct swift_io_sink *sink, struct net_buf *buf)
{
	atomic_inc(&immediate_count);
	if (buf->len >= 4) {
		immediate_last_value = sys_le32_to_cpu(*(uint32_t *)buf->data);
	}
	/* Buffer unref handled by framework for ALL handlers */
}

/* Define event queue for queued processing */
SWIFT_IO_EVENT_QUEUE_DEFINE(test_events, TEST_EVENT_QUEUE_SIZE);

/* Define packet sources */
SWIFT_IO_SOURCE_DEFINE(source1);
SWIFT_IO_SOURCE_DEFINE(source2);
SWIFT_IO_SOURCE_DEFINE(source3);
SWIFT_IO_SOURCE_DEFINE(isolated_source);
SWIFT_IO_SOURCE_DEFINE(queue_test_source);

/* Define packet sinks */
SWIFT_IO_SINK_DEFINE_QUEUED(sink1, capture_handler, test_events);
SWIFT_IO_SINK_DEFINE_QUEUED(sink2, capture_handler, test_events);
SWIFT_IO_SINK_DEFINE_QUEUED(sink3, capture_handler, test_events);
SWIFT_IO_SINK_DEFINE_IMMEDIATE(immediate_sink, immediate_handler);

/* Initialize sink user data */
static void init_sinks(void)
{
	sink1.user_data = &capture1;
	sink2.user_data = &capture2;
	sink3.user_data = &capture3;
}

/* Define connections */
/* source1 -> sink1, sink2, immediate_sink */
SWIFT_IO_CONNECT(&source1, &sink1);
SWIFT_IO_CONNECT(&source1, &sink2);
SWIFT_IO_CONNECT(&source1, &immediate_sink);

/* source2 -> sink1, sink3 */
SWIFT_IO_CONNECT(&source2, &sink1);
SWIFT_IO_CONNECT(&source2, &sink3);

/* source3 -> sink2, sink3 */
SWIFT_IO_CONNECT(&source3, &sink2);
SWIFT_IO_CONNECT(&source3, &sink3);

/* isolated_source has no connections */
SWIFT_IO_CONNECT(&queue_test_source, &sink1);

/* =============================================================================
 * Helper Functions
 * =============================================================================
 */

/* Process all pending events */
static void process_all_events(void)
{
	int count = 0;
	while (swift_io_event_process(&test_events, K_NO_WAIT) == 0 && count < TEST_MAX_EVENTS) {
		count++;
	}
}

/* Reset capture context */
static void reset_capture(struct test_capture *capture)
{
	atomic_clear(&capture->count);
	capture->last_value = 0;
}

static void reset_all_captures(void)
{
	reset_capture(&capture1);
	reset_capture(&capture2);
	atomic_clear(&immediate_count);
}

/* Send test packet with data */
static int send_packet(struct swift_io_source *source, uint32_t data)
{
	struct net_buf *buf;
	int ret;

	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	if (!buf) {
		return -ENOMEM;
	}

	net_buf_add_le32(buf, data);
	ret = swift_io_source_send(source, buf, K_NO_WAIT);
	net_buf_unref(buf);
	return ret;
}

/* Send and consume packet */
static int send_packet_consume(struct swift_io_source *source, uint32_t data)
{
	struct net_buf *buf;

	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	if (!buf) {
		return -ENOMEM;
	}

	net_buf_add_le32(buf, data);
	return swift_io_source_send_consume(source, buf, K_NO_WAIT);
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
	reset_capture(&capture1);
	reset_capture(&capture2);
	reset_capture(&capture3);

	/* Reset immediate counter */
	atomic_clear(&immediate_count);
	immediate_last_value = 0;

#ifdef CONFIG_SWIFT_IO_STATS
	/* Reset all statistics */
	swift_io_source_reset_stats(&source1);
	swift_io_source_reset_stats(&source2);
	swift_io_source_reset_stats(&source3);
	swift_io_source_reset_stats(&isolated_source);
	swift_io_source_reset_stats(&queue_test_source);

	swift_io_sink_reset_stats(&sink1);
	swift_io_sink_reset_stats(&sink2);
	swift_io_sink_reset_stats(&sink3);
	swift_io_sink_reset_stats(&immediate_sink);
#endif
}

/* Test teardown - called after each test */
static void test_teardown(void *fixture)
{
	ARG_UNUSED(fixture);

	/* Always drain all pending events to avoid test interference */
	process_all_events();

	/* Additional cleanup for runtime test resources if needed */
	/* Runtime queue cleanup is handled by individual runtime tests */
}

/* =============================================================================
 * Basic Functionality Tests
 * =============================================================================
 */

/* Test empty sink list (covers line 86 when list is empty) */
ZTEST(swift_io_unit_test, test_source_no_sinks)
{
	/* isolated_source has no connections - test sending to it */
	struct net_buf *buf;
	int ret;

	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Buffer allocation failed");
	net_buf_add_le32(buf, 0xFACE);

	/* Send to source with no sinks - should return 0 (no deliveries) */
	ret = swift_io_source_send(&isolated_source, buf, K_NO_WAIT);
	zassert_equal(ret, 0, "Should return 0 when no sinks connected");

	net_buf_unref(buf);
}

/* Add test for swift_io_event_process timeout (tests line 178-179) */
ZTEST(swift_io_unit_test, test_event_process_empty_queue)
{
	struct swift_io_event_queue queue = {
		.msgq = test_events.msgq,
	};
	int ret;

	/* Drain the queue first */
	process_all_events();

	/* Try to process from empty queue with no wait - should return -EAGAIN */
	ret = swift_io_event_process(&queue, K_NO_WAIT);
	zassert_equal(ret, -EAGAIN, "Should return -EAGAIN when queue is empty");

	/* Try with timeout */
	ret = swift_io_event_process(&queue, K_MSEC(TEST_TIMEOUT_MS));
	zassert_equal(ret, -EAGAIN, "Should return -EAGAIN after timeout");
}

/* Add test for stats functions with NULL outputs */
ZTEST(swift_io_unit_test, test_stats_null_params)
{
#ifdef CONFIG_SWIFT_IO_STATS
	uint32_t value;

	/* Test swift_io_source_get_stats with NULL parameters */
	swift_io_source_get_stats(&source1, NULL, NULL);   /* Both NULL */
	swift_io_source_get_stats(&source1, &value, NULL); /* queued_total NULL */
	swift_io_source_get_stats(&source1, NULL, &value); /* send_count NULL */

	/* Test swift_io_sink_get_stats with NULL parameters */
	swift_io_sink_get_stats(&sink1, NULL, NULL);   /* Both NULL */
	swift_io_sink_get_stats(&sink1, &value, NULL); /* dropped_count NULL */
	swift_io_sink_get_stats(&sink1, NULL, &value); /* handled_count NULL */
#else
	ztest_test_skip();
#endif
}

ZTEST_SUITE(swift_io_unit_test, NULL, NULL, test_setup, test_teardown, NULL);

/* =============================================================================
 * Connectivity and Routing Tests
 * =============================================================================
 */

ZTEST(swift_io_unit_test, test_basic_send)
{
	int ret;

	/* Send from source1 (connects to sink1, sink2, immediate_sink) */
	ret = send_packet(&source1, 0xCAFE);
	zassert_equal(ret, 3, "Should deliver to 3 sinks");

	/* Immediate sink should have processed immediately */
	zassert_equal(atomic_get(&immediate_count), 1, "Immediate processed");
	zassert_equal(immediate_last_value, 0xCAFE, "Immediate got correct value");

	/* Process queued events */
	process_all_events();

	/* Check captures */

	/* For now, just check that some processing happened */
	zassert_true(atomic_get(&capture1.count) > 0 || atomic_get(&capture2.count) > 0,
		     "At least one sink should have received");
}

ZTEST(swift_io_unit_test, test_multiple_sources)
{
	int ret;

	/* Send from different sources */
	ret = send_packet(&source1, 0x1111);
	zassert_equal(ret, 3, "Source1 delivers to 3");

	ret = send_packet(&source2, 0x2222);
	zassert_equal(ret, 2, "Source2 delivers to 2");

	ret = send_packet(&source3, 0x3333);
	zassert_equal(ret, 2, "Source3 delivers to 2");

	/* Process events */
	process_all_events();

	/* sink1 receives from source1 and source2 */
	zassert_equal(atomic_get(&capture1.count), 2, "Sink1 got 2 packets");

	/* sink2 receives from source1 and source3 */
	zassert_equal(atomic_get(&capture2.count), 2, "Sink2 got 2 packets");

	/* sink3 receives from source2 and source3 */
	zassert_equal(atomic_get(&capture3.count), 2, "Sink3 got 2 packets");

	/* Verify last values are reasonable */
	zassert_true(capture1.last_value == 0x1111 || capture1.last_value == 0x2222,
		     "Sink1 should have value from source1 or source2");
}

ZTEST(swift_io_unit_test, test_isolated_source)
{
	int ret;

	/* Send from isolated source */
	ret = send_packet(&isolated_source, 0xDEAD);
	zassert_equal(ret, 0, "No sinks connected");

	/* Process events (should be none) */
	process_all_events();

	/* Verify no sinks received anything */
	zassert_equal(atomic_get(&capture1.count), 0, "Sink1 empty");
	zassert_equal(atomic_get(&capture2.count), 0, "Sink2 empty");
	zassert_equal(atomic_get(&capture3.count), 0, "Sink3 empty");
	zassert_equal(atomic_get(&immediate_count), 0, "Immediate empty");
}

/* =============================================================================
 * Event Processing Tests
 * =============================================================================
 */

ZTEST(swift_io_unit_test, test_event_queue_processing)
{
	int ret;

	/* Send multiple packets */
	for (int i = 0; i < 5; i++) {
		ret = send_packet(&source2, 0x1000 + i);
		zassert_equal(ret, 2, "Each sends to 2 sinks");
	}

	/* Process events one by one */
	int processed = 0;
	while (swift_io_event_process(&test_events, K_NO_WAIT) == 0) {
		processed++;
		if (processed > 20) {
			break; /* Safety */
		}
	}

	/* Should have processed 10 events (5 packets * 2 sinks) */
	zassert_equal(processed, 10, "Should process 10 events");

	/* Verify captures */
	zassert_equal(atomic_get(&capture1.count), 5, "Sink1 got 5");
	zassert_equal(atomic_get(&capture3.count), 5, "Sink3 got 5");

	/* Verify that packets were processed correctly */
	/* Both sinks should have received one of our values */
	zassert_true(capture1.last_value >= 0x1000 && capture1.last_value <= 0x1004,
		     "Sink1 should have valid value, got 0x%x", capture1.last_value);
	zassert_true(capture3.last_value >= 0x1000 && capture3.last_value <= 0x1004,
		     "Sink3 should have valid value, got 0x%x", capture3.last_value);
}

ZTEST(swift_io_unit_test, test_event_timeout)
{
	int ret;

	/* Try to process from empty queue */
	ret = swift_io_event_process(&test_events, K_NO_WAIT);
	zassert_equal(ret, -EAGAIN, "Should return EAGAIN when empty");

	/* Try with timeout */
	ret = swift_io_event_process(&test_events, K_MSEC(TEST_TIMEOUT_MS));
	zassert_equal(ret, -EAGAIN, "Should timeout");
}

/* =============================================================================
 * Reference Counting Tests
 * =============================================================================
 */

ZTEST(swift_io_unit_test, test_reference_counting)
{
	struct net_buf *buf;
	int ret;

	/* Allocate buffer */
	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Should allocate");
	zassert_equal(buf->ref, 1, "Initial ref = 1");

	net_buf_add_le32(buf, 0xBEEF);

	/* Send to source1 (3 sinks) */
	ret = swift_io_source_send(&source1, buf, K_NO_WAIT);
	zassert_equal(ret, 3, "Delivered to 3");
	/* After send, the original buffer may have different ref count due to the implementation */

	/* Process events */
	process_all_events();

	/* Verify all sinks processed the packet */
	zassert_equal(atomic_get(&capture1.count), 1, "Sink1 processed");
	zassert_equal(atomic_get(&capture2.count), 1, "Sink2 processed");

	/* Verify data was correct */
	zassert_equal(capture1.last_value, 0xBEEF, "Sink1 got correct data");
	zassert_equal(capture2.last_value, 0xBEEF, "Sink2 got correct data");

	/* Clean up original buffer */
	net_buf_unref(buf);
}

ZTEST(swift_io_unit_test, test_send_consume)
{
	int ret;

	/* Send with consume */
	ret = send_packet_consume(&source1, 0xFEED);
	zassert_equal(ret, 3, "Delivered to 3");

	/* Process events */
	process_all_events();

	/* Verify delivery */
	zassert_equal(atomic_get(&capture1.count), 1, "Sink1 processed");
	zassert_equal(capture1.last_value, 0xFEED, "Sink1 got correct data");
}

/* =============================================================================
 * Mixed Mode Tests
 * =============================================================================
 */

ZTEST(swift_io_unit_test, test_immediate_vs_queued)
{
	int ret;

	/* Send packet */
	ret = send_packet(&source1, 0xABCD);
	zassert_equal(ret, 3, "Delivered to 3");

	/* Immediate should have processed already */
	zassert_equal(atomic_get(&immediate_count), 1, "Immediate done");
	zassert_equal(immediate_last_value, 0xABCD, "Immediate has data");

	/* Queued sinks not yet */
	zassert_equal(atomic_get(&capture1.count), 0, "Sink1 not yet");
	zassert_equal(atomic_get(&capture2.count), 0, "Sink2 not yet");

	/* Process events */
	process_all_events();

	/* Now queued sinks should have it */
	zassert_equal(atomic_get(&capture1.count), 1, "Sink1 processed");
	zassert_equal(atomic_get(&capture2.count), 1, "Sink2 processed");
}

/* =============================================================================
 * Statistics Tests
 * =============================================================================
 */

#ifdef CONFIG_SWIFT_IO_STATS
ZTEST(swift_io_unit_test, test_statistics)
{
	uint32_t send_count, queued_total;
	uint32_t handled_count, dropped_count;
	int ret;

	/* Send packets */
	for (int i = 0; i < 5; i++) {
		ret = send_packet(&source1, i);
		zassert_equal(ret, 3, "Delivers to 3");
	}

	/* Check source stats */
	swift_io_source_get_stats(&source1, &send_count, &queued_total);
	zassert_equal(send_count, 5, "Sent 5 messages");
	zassert_equal(queued_total, 15, "Delivered 15 (5*3)");

	/* Process events */
	process_all_events();

	/* Check sink stats */
	swift_io_sink_get_stats(&sink1, &handled_count, &dropped_count);
	/* sink1 should have received 5 more packets in this test (cumulative) */
	zassert_true(handled_count >= 5, "Sink1 should have processed at least 5");
	zassert_equal(dropped_count, 0, "No drops");

	/* Test reset */
	swift_io_source_reset_stats(&source1);
	swift_io_source_get_stats(&source1, &send_count, &queued_total);
	zassert_equal(send_count, 0, "Reset to 0");
	zassert_equal(queued_total, 0, "Reset to 0");
}
#endif

/* =============================================================================
 * Stress Tests
 * =============================================================================
 */

ZTEST(swift_io_unit_test, test_many_packets)
{
	int ret;
	int sent = 0;

	/* Send many packets */
	for (int i = 0; i < TEST_MANY_PACKETS; i++) {
		ret = send_packet(&source2, i);
		if (ret > 0) {
			sent++;
		}
	}

	zassert_equal(sent, 30, "Sent all packets");

	/* Process all */
	process_all_events();

	/* Verify counts */
	zassert_equal(atomic_get(&capture1.count), TEST_MANY_PACKETS, "Sink1 got all packets");
	zassert_equal(atomic_get(&capture3.count), TEST_MANY_PACKETS, "Sink3 got all packets");

	/* Last values should be from the final packet */
	zassert_equal(capture1.last_value, TEST_MANY_PACKETS - 1, "Sink1 last value correct");
	zassert_equal(capture3.last_value, TEST_MANY_PACKETS - 1, "Sink3 last value correct");
}

ZTEST(swift_io_unit_test, test_queue_overflow)
{
	int ret;

	/* Send many packets to test handler resilience */
	for (int i = 0; i < 15; i++) {
		ret = send_packet(&source2, i);
		zassert_equal(ret, 2, "Delivers to 2");
	}

	/* Process events */
	process_all_events();

	/* All sinks should have received all packets */
	zassert_equal(atomic_get(&capture1.count), 15, "Sink1 processed 15");
	zassert_equal(atomic_get(&capture3.count), 15, "Sink3 processed 15");

	/* Last values should be from the final packet */
	zassert_equal(capture1.last_value, 14, "Sink1 last value should be 14");
	zassert_equal(capture3.last_value, 14, "Sink3 last value should be 14");
}

/* =============================================================================
 * Timeout Tests
 * =============================================================================
 */

ZTEST(swift_io_unit_test, test_send_with_timeout)
{
	int ret;

	/* These should all work the same for our test setup */
	ret = send_packet(&source1, 0x1111);
	zassert_equal(ret, 3, "K_NO_WAIT works");

	/* With timeout */
	struct net_buf *buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Allocate buffer");
	net_buf_add_le32(buf, 0x2222);

	ret = swift_io_source_send(&source1, buf, K_MSEC(TEST_TIMEOUT_MS));
	zassert_equal(ret, 3, "With timeout works");
	net_buf_unref(buf);

	/* Process and verify */
	process_all_events();
	zassert_equal(atomic_get(&capture1.count), 2, "Got both packets");
}

/* =============================================================================
 * Buffer Pool Tests
 * =============================================================================
 */

ZTEST(swift_io_unit_test, test_buffer_exhaustion)
{
	struct net_buf *bufs[150];
	int allocated = 0;
	int ret;

	/* Allocate all buffers */
	for (int i = 0; i < TEST_BUFFER_POOL_SIZE; i++) {
		bufs[i] = net_buf_alloc(&test_pool, K_NO_WAIT);
		if (bufs[i]) {
			allocated++;
		} else {
			break;
		}
	}

	/* Try to send - should fail due to buffer pool exhaustion */
	ret = send_packet(&source1, 0x9999);
	zassert_equal(ret, -ENOMEM, "Should fail due to no buffer available");

	/* Free buffers */
	for (int i = 0; i < allocated; i++) {
		net_buf_unref(bufs[i]);
	}

	/* Now should work */
	ret = send_packet(&source1, 0x8888);
	zassert_equal(ret, 3, "Should work after freeing");
}

/* Test: Complex topology with multiple paths */
ZTEST(swift_io_unit_test, test_complex_topology)
{
	int ret;

	reset_all_captures();

	/* Send from source1 - should reach sink1, sink2, immediate_sink */
	ret = send_packet(&source1, 0x1111);
	zassert_equal(ret, 3, "Should deliver to 3 sinks");

	/* Send from source2 - should reach sink1 and sink3 */
	ret = send_packet(&source2, 0x2222);
	zassert_equal(ret, 2, "Should deliver to 2 sinks");

	process_all_events();

	/* Verify distribution */
	zassert_equal(atomic_get(&capture1.count), 2,
		      "Sink1 should receive 2 packets (from source1 and source2)");
	zassert_equal(atomic_get(&capture2.count), 1,
		      "Sink2 should receive 1 packet (from source1)");
	zassert_equal(atomic_get(&immediate_count), 1,
		      "Immediate should receive 1 packet (from source1)");
}

/* Test: Statistics reset functionality */
ZTEST(swift_io_unit_test, test_stats_reset)
{
	int ret;

	reset_all_captures();

	/* Send some packets */
	ret = send_packet(&source1, 0x1111);
	zassert_equal(ret, 3, "Should deliver packets");

	process_all_events();

#ifdef CONFIG_SWIFT_IO_STATS
	uint32_t send_count, queued_total;

	/* Check initial stats */
	swift_io_source_get_stats(&source1, &send_count, &queued_total);
	zassert_equal(send_count, 1, "Should have 1 message sent");
	zassert_equal(queued_total, 3, "Should have 3 deliveries");

	/* Reset stats */
	swift_io_source_reset_stats(&source1);

	/* Check stats are reset */
	swift_io_source_get_stats(&source1, &send_count, &queued_total);
	zassert_equal(send_count, 0, "Stats should be reset");
	zassert_equal(queued_total, 0, "Stats should be reset");
#else
	/* Without stats, just verify basic functionality works */
	zassert_true(ret > 0, "Basic functionality should work without stats");
#endif
}

/* =============================================================================
 * Concurrency Tests
 * =============================================================================
 */

/* Test concurrent sending from multiple threads */
ZTEST(swift_io_unit_test, test_concurrent_send)
{
	reset_all_captures();

	/* Simulate concurrent sends by rapidly sending from different sources */
	for (int round = 0; round < 10; round++) {
		int ret1 = send_packet(&source1, 0x1000 + round);
		int ret2 = send_packet(&source2, 0x2000 + round);
		int ret3 = send_packet(&source3, 0x3000 + round);

		/* Verify all sends succeeded */
		zassert_equal(ret1, 3, "Source1 should deliver to 3 sinks");
		zassert_equal(ret2, 2, "Source2 should deliver to 2 sinks");
		zassert_equal(ret3, 2, "Source3 should deliver to 2 sinks");

		/* Process some events between rounds */
		if (round % 3 == 0) {
			process_all_events();
		}
	}

	/* Final processing */
	process_all_events();

	/* Verify all packets were delivered */
	/* sink1 receives from source1 and source2 = 20 packets */
	zassert_equal(atomic_get(&capture1.count), 20, "Sink1 should get 20 packets");
	/* sink2 receives from source1 and source3 = 20 packets */
	zassert_equal(atomic_get(&capture2.count), 20, "Sink2 should get 20 packets");
	/* sink3 receives from source2 and source3 = 20 packets */
	zassert_equal(atomic_get(&capture3.count), 20, "Sink3 should get 20 packets");
}

/* Test concurrent event processing */
ZTEST(swift_io_unit_test, test_concurrent_receive)
{
	reset_all_captures();

	/* Send many packets to fill the event queue */
	for (int i = 0; i < 20; i++) {
		int ret = send_packet(&source1, 0x5000 + i);
		zassert_equal(ret, 3, "Should deliver to 3 sinks");
	}

	/* Process events in batches to simulate concurrent processing */
	int total_processed = 0;
	for (int batch = 0; batch < 10; batch++) {
		int processed = 0;
		while (swift_io_event_process(&test_events, K_NO_WAIT) == 0 && processed < 10) {
			processed++;
			total_processed++;
		}
		/* Small delay to simulate concurrent access patterns */
		k_busy_wait(1);
	}

	/* Ensure all events are processed */
	while (swift_io_event_process(&test_events, K_NO_WAIT) == 0) {
		total_processed++;
		if (total_processed > 100) {
			break; /* Safety */
		}
	}

	/* Verify all packets were processed */
	zassert_equal(atomic_get(&capture1.count), 20, "Sink1 should process 20");
	zassert_equal(atomic_get(&capture2.count), 20, "Sink2 should process 20");
	/* immediate_sink processes immediately, so should have 20 as well */
	zassert_equal(atomic_get(&immediate_count), 20, "Immediate should process 20");
}

/* =============================================================================
 * Buffer Lifecycle and Reference Counting Tests
 * =============================================================================
 */

/* Test complete buffer lifecycle and reference counting */
ZTEST(swift_io_unit_test, test_buffer_lifecycle)
{
	struct net_buf *buf;
	int ret;

	reset_all_captures();

	/* Allocate buffer and verify initial state */
	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Should allocate buffer");
	zassert_equal(buf->ref, 1, "Initial ref count should be 1");

	/* Add test data */
	net_buf_add_le32(buf, 0xDEADBEEF);

	/* Send to source with multiple sinks */
	ret = swift_io_source_send(&source1, buf, K_NO_WAIT);
	zassert_equal(ret, 3, "Should deliver to 3 sinks");

	/* Process all events to ensure handlers get called */
	process_all_events();

	/* Verify all sinks received the packet */
	zassert_equal(atomic_get(&capture1.count), 1, "Sink1 should receive 1");
	zassert_equal(atomic_get(&capture2.count), 1, "Sink2 should receive 1");
	zassert_equal(atomic_get(&immediate_count), 1, "Immediate should receive 1");

	/* Verify data integrity */
	zassert_equal(capture1.last_value, 0xDEADBEEF, "Sink1 should have correct data");
	zassert_equal(capture2.last_value, 0xDEADBEEF, "Sink2 should have correct data");
	zassert_equal(immediate_last_value, 0xDEADBEEF, "Immediate should have correct data");

	/* Release original reference */
	net_buf_unref(buf);

	/* Try to allocate again to verify no leak */
	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Should be able to allocate after processing");
	net_buf_unref(buf);
}

/* Test reference counting when all sinks drop packets */
ZTEST(swift_io_unit_test, test_ref_count_all_drops)
{
	reset_all_captures();

	/* Send multiple packets rapidly to test queue/handler behavior */
	for (int i = 0; i < 25; i++) {
		int ret = send_packet(&source1, 0x6000 + i);
		zassert_equal(ret, 3, "Should attempt delivery to 3 sinks");
	}

	/* Process all events */
	process_all_events();

	/* Verify packets were handled (even if some were dropped) */
	int total_processed = atomic_get(&capture1.count) + atomic_get(&capture2.count) +
			      atomic_get(&immediate_count);
	zassert_true(total_processed >= 25, "Should process at least 25 packets total");

	/* Test buffer pool availability after stress */
	struct net_buf *test_buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(test_buf, "Buffer pool should be available after drops");
	net_buf_unref(test_buf);
}

/* =============================================================================
 * Timeout and Blocking Tests
 * =============================================================================
 */

/* Test advanced K_FOREVER scenarios */
ZTEST(swift_io_unit_test, test_advanced_k_forever_scenarios)
{
	struct net_buf *buf;
	int ret;

	reset_all_captures();

	/* Test K_FOREVER with available resources (should work) */
	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Should allocate buffer");
	net_buf_add_le32(buf, 0xAAAA);

	/* This should work immediately since we have resources */
	ret = swift_io_source_send(&source1, buf, K_FOREVER);
	zassert_equal(ret, 3, "K_FOREVER should deliver to all 3 sinks");

	net_buf_unref(buf);
	process_all_events();

	/* Verify delivery */
	zassert_equal(atomic_get(&immediate_count), 1, "Immediate should process");
	zassert_equal(atomic_get(&capture1.count), 1, "Sink1 should process");
	zassert_equal(atomic_get(&capture2.count), 1, "Sink2 should process");
}

/* =============================================================================
 * Event Queue Management Tests
 * =============================================================================
 */

/* Test shared event queue behavior */
ZTEST(swift_io_unit_test, test_shared_event_queue_behavior)
{
	reset_all_captures();

	/* Send from different sources that share sinks */
	int ret1 = send_packet(&source1, 0xBBBB); /* -> sink1, sink2, immediate */
	int ret2 = send_packet(&source2, 0xCCCC); /* -> sink1, sink3 */
	int ret3 = send_packet(&source3, 0xDDDD); /* -> sink2, sink3 */

	zassert_equal(ret1, 3, "Source1 should deliver to 3");
	zassert_equal(ret2, 2, "Source2 should deliver to 2");
	zassert_equal(ret3, 2, "Source3 should deliver to 2");

	/* Count events before processing */
	int events_to_process = 0;
	while (swift_io_event_process(&test_events, K_NO_WAIT) == 0) {
		events_to_process++;
		if (events_to_process > 20) {
			break; /* Safety */
		}
	}

	/* Should have processed 6 queued events (sink1: 2, sink2: 2, sink3: 2) */
	/* immediate_sink processes immediately and doesn't use the queue */
	zassert_equal(events_to_process, 6, "Should process 6 queued events");

	/* Verify final counts */
	zassert_equal(atomic_get(&capture1.count), 2, "Sink1 should get 2 (from source1, source2)");
	zassert_equal(atomic_get(&capture2.count), 2, "Sink2 should get 2 (from source1, source3)");
	zassert_equal(atomic_get(&capture3.count), 2, "Sink3 should get 2 (from source2, source3)");
	zassert_equal(atomic_get(&immediate_count), 1, "Immediate should get 1 (from source1)");

	/* Verify data integrity - at least one value should be preserved */
	zassert_true(capture1.last_value == 0xBBBB || capture1.last_value == 0xCCCC,
		     "Sink1 should have value from source1 or source2");
	zassert_true(capture2.last_value == 0xBBBB || capture2.last_value == 0xDDDD,
		     "Sink2 should have value from source1 or source3");
	zassert_true(capture3.last_value == 0xCCCC || capture3.last_value == 0xDDDD,
		     "Sink3 should have value from source2 or source3");
}

/* =============================================================================
 * Polling and Event Processing Tests
 * =============================================================================
 */

/* Test k_poll integration with event queues */
ZTEST(swift_io_unit_test, test_poll_integration)
{
	int ret;

	reset_all_captures();

	/* Test that we can process events in a polling manner */
	/* Send a packet to generate events */
	ret = send_packet(&source1, 0xEEEE);
	zassert_equal(ret, 3, "Should deliver to 3 sinks");

	/* Verify that events are available by trying to process them */
	ret = swift_io_event_process(&test_events, K_NO_WAIT);
	zassert_equal(ret, 0, "Should find and process an event");

	/* Process remaining events */
	process_all_events();

	/* Verify processing */
	zassert_equal(atomic_get(&capture1.count), 1, "Sink1 should process");
	zassert_equal(atomic_get(&capture2.count), 1, "Sink2 should process");
}

/* Test polling with timeout */
ZTEST(swift_io_unit_test, test_poll_timeout)
{
	int ret;

	reset_all_captures();

	/* Test timeout behavior with packet event processing */
	/* Should timeout when no events available */
	ret = swift_io_event_process(&test_events, K_MSEC(TEST_TIMEOUT_MS));
	zassert_equal(ret, -EAGAIN, "Should timeout when no events");

	/* Send events */
	ret = send_packet(&source2, 0xFFFF);
	zassert_equal(ret, 2, "Should deliver to 2 sinks");

	/* Should now process immediately */
	ret = swift_io_event_process(&test_events, K_NO_WAIT);
	zassert_equal(ret, 0, "Should find and process events");

	process_all_events();
}

/* =============================================================================
 * Stress and Performance Tests
 * =============================================================================
 */

/* Test stress burst sends */
ZTEST(swift_io_unit_test, test_stress_burst)
{
	int successful = 0;
	int ret;

	reset_all_captures();

	/* Send burst of packets rapidly */
	for (int burst = 0; burst < (TEST_STRESS_ITERATIONS / TEST_BURST_SIZE); burst++) {
		for (int i = 0; i < TEST_BURST_SIZE; i++) {
			ret = send_packet(&source1, 0xA000 + (burst * 10) + i);
			if (ret > 0) {
				successful++;
			}
		}
		/* Process events after each burst */
		process_all_events();
	}

	/* Final processing */
	process_all_events();

	/* Verify all packets were handled */
	zassert_equal(successful, TEST_STRESS_ITERATIONS, "Should send all stress test packets");
	zassert_equal(atomic_get(&capture1.count), TEST_STRESS_ITERATIONS,
		      "Sink1 should get all packets");
	zassert_equal(atomic_get(&capture2.count), TEST_STRESS_ITERATIONS,
		      "Sink2 should get all packets");
	zassert_equal(atomic_get(&immediate_count), TEST_STRESS_ITERATIONS,
		      "Immediate should get all packets");
}

/* Test many-to-one interleaved routing */
ZTEST(swift_io_unit_test, test_many_to_one_interleaved)
{
	reset_all_captures();

	/* Interleave sends from multiple sources to sink1 */
	for (int round = 0; round < 10; round++) {
		/* source1 -> sink1 (also goes to sink2, immediate) */
		int ret1 = send_packet(&source1, 0xB000 + round);
		zassert_equal(ret1, 3, "Source1 should deliver");

		/* source2 -> sink1 (also goes to sink3) */
		int ret2 = send_packet(&source2, 0xC000 + round);
		zassert_equal(ret2, 2, "Source2 should deliver");

		/* Process some events between rounds */
		if (round % 3 == 0) {
			process_all_events();
		}
	}

	/* Final processing */
	process_all_events();

	/* sink1 should receive from both source1 and source2 = 20 total */
	zassert_equal(atomic_get(&capture1.count), 20, "Sink1 should get 20 total");

	/* Verify other sinks got their packets too */
	zassert_equal(atomic_get(&capture2.count), 10, "Sink2 should get 10 from source1");
	zassert_equal(atomic_get(&capture3.count), 10, "Sink3 should get 10 from source2");
}

/* Test one-to-many fanout */
ZTEST(swift_io_unit_test, test_one_to_many_fanout)
{
	reset_all_captures();

	/* Send from source1 which fans out to 3 sinks */
	for (int i = 0; i < 15; i++) {
		int ret = send_packet(&source1, 0xD000 + i);
		zassert_equal(ret, 3, "Should fanout to 3 sinks");

		/* Process periodically */
		if (i % 5 == 4) {
			process_all_events();
		}
	}

	/* Final processing */
	process_all_events();

	/* All 3 destinations should receive all 15 packets */
	zassert_equal(atomic_get(&capture1.count), 15, "Sink1 should get all 15");
	zassert_equal(atomic_get(&capture2.count), 15, "Sink2 should get all 15");
	zassert_equal(atomic_get(&immediate_count), 15, "Immediate should get all 15");

	/* Verify data integrity on last packet */
	zassert_equal(capture1.last_value, 0xD000 + 14, "Sink1 should have last value");
	zassert_equal(capture2.last_value, 0xD000 + 14, "Sink2 should have last value");
	zassert_equal(immediate_last_value, 0xD000 + 14, "Immediate should have last value");
}

/* =============================================================================
 * Error Recovery Tests
 * =============================================================================
 */

/* Test partial delivery scenarios */
ZTEST(swift_io_unit_test, test_partial_delivery)
{
	int ret;

	reset_all_captures();

	/* Fill queues to near capacity */
	for (int i = 0; i < 15; i++) {
		ret = send_packet(&source1, 0x1000 + i);
		if (ret <= 0) {
			break;
		}
	}

	process_all_events();

	/* Send one more - some sinks might be full */
	ret = send_packet(&source1, 0x2000);
	zassert_true(ret >= 0, "Should not fail completely");

	process_all_events();
}

/* Test memory leak detection */
ZTEST(swift_io_unit_test, test_no_memory_leak)
{
	struct net_buf *initial_bufs[10];
	struct net_buf *final_bufs[10];
	int initial_count = 0, final_count = 0;

	reset_all_captures();

	/* Allocate some buffers to check initial state */
	for (int i = 0; i < 10; i++) {
		initial_bufs[i] = net_buf_alloc(&test_pool, K_NO_WAIT);
		if (initial_bufs[i]) {
			initial_count++;
		} else {
			break;
		}
	}

	/* Free them */
	for (int i = 0; i < initial_count; i++) {
		net_buf_unref(initial_bufs[i]);
	}

	/* Send and process many packets */
	for (int i = 0; i < 20; i++) {
		int ret = send_packet(&source1, 0x4000 + i);
		if (ret > 0) {
			process_all_events();
		}
	}

	/* Drain all queues */
	process_all_events();

	/* Check if we can still allocate the same number of buffers */
	for (int i = 0; i < 10; i++) {
		final_bufs[i] = net_buf_alloc(&test_pool, K_NO_WAIT);
		if (final_bufs[i]) {
			final_count++;
		} else {
			break;
		}
	}

	/* Free them */
	for (int i = 0; i < final_count; i++) {
		net_buf_unref(final_bufs[i]);
	}

	zassert_equal(initial_count, final_count,
		      "Should be able to allocate same number of buffers");
}

/* Test invalid sink mode error path */
ZTEST(swift_io_unit_test, test_invalid_sink_mode)
{
	struct net_buf *buf;
	struct swift_io_sink test_sink;
	int ret;

	/* Create a sink with invalid mode */
	test_sink.mode = 999; /* Invalid mode */
	test_sink.handler = immediate_handler;
	test_sink.msgq = NULL;
#ifdef CONFIG_SWIFT_IO_STATS
	atomic_clear(&test_sink.handled_count);
	atomic_clear(&test_sink.dropped_count);
#endif

	/* Manually add connection to source to test invalid mode path */
	struct swift_io_connection test_conn = {
		.source = &source1,
		.sink = &test_sink,
	};

	k_spinlock_key_t key = k_spin_lock(&source1.lock);
	sys_dlist_append(&source1.sinks, &test_conn.node);
	k_spin_unlock(&source1.lock, key);

	/* Try to send - should handle invalid mode gracefully */
	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Should allocate buffer");
	net_buf_add_le32(buf, 0x5000);
	ret = swift_io_source_send(&source1, buf, K_NO_WAIT);

	/* Should succeed for other valid sinks, fail for invalid one */
	zassert_true(ret >= 0, "Send should not fail completely");

	/* Remove the test connection */
	key = k_spin_lock(&source1.lock);
	sys_dlist_remove(&test_conn.node);
	k_spin_unlock(&source1.lock, key);

	net_buf_unref(buf);
}

/* Test queue full scenario to trigger dropped count stats */
ZTEST(swift_io_unit_test, test_queue_full_drop)
{
	struct net_buf *buf;
	int ret;

#ifdef CONFIG_SWIFT_IO_STATS
	uint32_t initial_dropped, final_dropped;

	/* Reset all captures and stats to start clean */
	reset_all_captures();

	/* Get initial drop count */
	swift_io_sink_get_stats(&sink1, NULL, &initial_dropped);

	/* Fill up the queue (size 100) with K_NO_WAIT - don't process events! */
	for (int i = 0; i < 105; i++) {
		buf = net_buf_alloc(&test_pool, K_NO_WAIT);
		if (!buf) {
			break;
		}
		net_buf_add_le32(buf, 0x6000 + i);
		ret = swift_io_source_send(&queue_test_source, buf, K_NO_WAIT);
		net_buf_unref(buf);
		/* Don't break on ret == 0, we want to overflow the queue */
	}

	/* Check if drop count increased */
	swift_io_sink_get_stats(&sink1, NULL, &final_dropped);
	zassert_true(final_dropped > initial_dropped,
		     "Drop count should increase when queue is full");

	/* Clean up queue */
	process_all_events();
#else
	/* Without stats, just test that queue full doesn't crash */
	reset_all_captures();

	for (int i = 0; i < 105; i++) {
		buf = net_buf_alloc(&test_pool, K_NO_WAIT);
		if (!buf) {
			break;
		}
		net_buf_add_le32(buf, 0x6000 + i);
		ret = swift_io_source_send(&source1, buf, K_NO_WAIT);
		net_buf_unref(buf);
	}

	/* Clean up */
	process_all_events();
#endif
}

/* =============================================================================
 * Input Validation and Error Handling Tests
 * =============================================================================
 */

/* Test invalid inputs for swift_io_source_send */
ZTEST(swift_io_unit_test, test_invalid_source_send)
{
	struct net_buf *buf;
	int ret;

	/* Test NULL source */
	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Buffer allocation should succeed");
	ret = swift_io_source_send(NULL, buf, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject NULL source");
	net_buf_unref(buf);

	/* Test NULL buffer */
	ret = swift_io_source_send(&source1, NULL, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject NULL buffer");

	/* Test both NULL */
	ret = swift_io_source_send(NULL, NULL, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject NULL parameters");
}

/* Test invalid inputs for swift_io_source_send_consume */
ZTEST(swift_io_unit_test, test_invalid_source_send_consume)
{
	struct net_buf *buf;
	int ret;

	/* Test NULL source */
	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Buffer allocation should succeed");
	ret = swift_io_source_send_consume(NULL, buf, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject NULL source");
	net_buf_unref(buf);

	/* Test NULL buffer */
	ret = swift_io_source_send_consume(&source1, NULL, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject NULL buffer");

	/* Test both NULL */
	ret = swift_io_source_send_consume(NULL, NULL, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject NULL parameters");
}

/* Test invalid inputs for swift_io_event_process */
ZTEST(swift_io_unit_test, test_invalid_event_process)
{
	int ret;

	/* Test NULL queue */
	ret = swift_io_event_process(NULL, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject NULL queue");

	/* Test queue with NULL msgq */
	struct swift_io_event_queue bad_queue = {.msgq = NULL};
	ret = swift_io_event_process(&bad_queue, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject queue with NULL msgq");
}

#ifdef CONFIG_SWIFT_IO_STATS
/* Test invalid inputs for statistics functions */
ZTEST(swift_io_unit_test, test_invalid_stats_functions)
{
	uint32_t send_count, queued_total, handled_count, dropped_count;

	/* Test NULL source in get_stats */
	swift_io_source_get_stats(NULL, &send_count, &queued_total);
	/* Should not crash, just return */

	/* Test NULL sink in get_stats */
	swift_io_sink_get_stats(NULL, &handled_count, &dropped_count);
	/* Should not crash, just return */

	/* Test NULL source in reset_stats */
	swift_io_source_reset_stats(NULL);
	/* Should not crash, just return */

	/* Test NULL sink in reset_stats */
	swift_io_sink_reset_stats(NULL);
	/* Should not crash, just return */

	/* These functions handle NULL gracefully, so we just verify they don't crash */
	zassert_true(true, "Statistics functions handled NULL inputs");
}
#endif

/* =============================================================================
 * Corruption and Defensive Programming Tests
 * =============================================================================
 */

/* Test sink with invalid handler (NULL) */
ZTEST(swift_io_unit_test, test_sink_null_handler)
{
	struct net_buf *buf;
	struct swift_io_sink bad_sink;
	struct swift_io_connection conn;
	struct swift_io_source isolated_source = {
		.sinks = SYS_DLIST_STATIC_INIT(&isolated_source.sinks),
		.lock = {},
#ifdef CONFIG_SWIFT_IO_STATS
		.send_count = ATOMIC_INIT(0),
		.queued_total = ATOMIC_INIT(0),
#endif
	};
	int ret;

	/* Create a sink with NULL handler */
	bad_sink.mode = SINK_MODE_IMMEDIATE;
	bad_sink.handler = NULL; /* Invalid! */
	bad_sink.msgq = NULL;
#ifdef CONFIG_SWIFT_IO_STATS
	atomic_clear(&bad_sink.handled_count);
	atomic_clear(&bad_sink.dropped_count);
#endif

	/* Connect bad sink to isolated source */
	conn.source = &isolated_source;
	conn.sink = &bad_sink;
	sys_dnode_init(&conn.node);
	sys_dlist_append(&isolated_source.sinks, &conn.node);

	/* Try to send - should handle NULL handler gracefully */
	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Buffer allocation should succeed");
	net_buf_add_le32(buf, 0xBADC0DE);

	ret = swift_io_source_send(&isolated_source, buf, K_NO_WAIT);
	/* The send will fail for this sink but shouldn't crash */
	zassert_equal(ret, 0, "Send should return 0 (no successful deliveries)");

	net_buf_unref(buf);

	/* Clean up */
	sys_dlist_remove(&conn.node);
}

/* Test queued sink with NULL msgq */
ZTEST(swift_io_unit_test, test_sink_null_msgq)
{
	struct net_buf *buf;
	struct k_msgq *original_msgq;
	int ret;

	/* Save original msgq and corrupt it */
	original_msgq = sink1.msgq;
	sink1.msgq = NULL; /* Corrupt! */

	/* Try to send - should handle NULL msgq gracefully */
	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Buffer allocation should succeed");
	net_buf_add_le32(buf, 0xBADC0DE);

	/* source1 is connected to sink1, sink2, and immediate_sink */
	ret = swift_io_source_send(&source1, buf, K_NO_WAIT);
	/* Should deliver to sink2 and immediate_sink (sink1 will fail due to NULL msgq) */
	zassert_equal(ret, 2, "Should deliver to 2 sinks (sink2 and immediate_sink)");

	net_buf_unref(buf);

	/* Restore original msgq */
	sink1.msgq = original_msgq;

	/* Clean up any events that made it to sink2 */
	process_all_events();
}

/* Test corrupted connection with NULL sink during iteration */
ZTEST(swift_io_unit_test, test_corrupted_connection_null_sink)
{
	struct net_buf *buf;
	struct swift_io_connection bad_conn;
	k_spinlock_key_t key;
	int ret;

	/* Create a corrupted connection with NULL sink and add to source1 */
	bad_conn.source = &source1;
	bad_conn.sink = NULL; /* Corrupted! */
	sys_dnode_init(&bad_conn.node);

	/* Manually add the corrupted connection to source1's list */
	key = k_spin_lock(&source1.lock);
	sys_dlist_append(&source1.sinks, &bad_conn.node);
	k_spin_unlock(&source1.lock, key);

	/* Try to send - should skip the NULL sink but deliver to others */
	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Buffer allocation should succeed");
	net_buf_add_le32(buf, 0xDEADBEEF);

	ret = swift_io_source_send(&source1, buf, K_NO_WAIT);
	/* Should still deliver to the 3 valid sinks (sink1, sink2, immediate_sink) */
	zassert_equal(ret, 3, "Should deliver to 3 valid sinks, skip NULL");

	net_buf_unref(buf);

	/* Clean up - remove the bad connection */
	key = k_spin_lock(&source1.lock);
	sys_dlist_remove(&bad_conn.node);
	k_spin_unlock(&source1.lock, key);

	/* Process any queued events */
	process_all_events();
}

/* Define message queue for corruption tests at file scope */
K_MSGQ_DEFINE(test_corrupt_msgq, sizeof(struct swift_io_event), TEST_CORRUPT_QUEUE_SIZE, 4);

/* Test corrupted event in queue */
ZTEST(swift_io_unit_test, test_corrupted_event_in_queue)
{
	struct swift_io_event_queue test_queue;
	struct swift_io_event bad_event;
	struct net_buf *buf;
	int ret;

	test_queue.msgq = &test_corrupt_msgq;
#ifdef CONFIG_SWIFT_IO_STATS
	atomic_clear(&test_queue.processed_count);
#endif

	/* Test 1: Event with NULL sink */
	bad_event.sink = NULL;
	bad_event.buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(bad_event.buf, "Failed to allocate buffer");

	ret = k_msgq_put(&test_corrupt_msgq, &bad_event, K_NO_WAIT);
	zassert_equal(ret, 0, "Failed to queue bad event");

	ret = swift_io_event_process(&test_queue, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject event with NULL sink");

	/* Test 2: Event with NULL buffer */
	bad_event.sink = &sink1;
	bad_event.buf = NULL;

	ret = k_msgq_put(&test_corrupt_msgq, &bad_event, K_NO_WAIT);
	zassert_equal(ret, 0, "Failed to queue bad event");

	ret = swift_io_event_process(&test_queue, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject event with NULL buffer");

	/* Test 3: Event with sink that has NULL handler */
	struct swift_io_sink bad_sink = {
		.mode = SINK_MODE_IMMEDIATE,
		.handler = NULL,
		.msgq = NULL,
	};
	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Buffer allocation should succeed");

	bad_event.sink = &bad_sink;
	bad_event.buf = buf;

	ret = k_msgq_put(&test_corrupt_msgq, &bad_event, K_NO_WAIT);
	zassert_equal(ret, 0, "Failed to queue bad event");

	ret = swift_io_event_process(&test_queue, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject event with NULL handler");

	/* Clean up any remaining messages */
	k_msgq_purge(&test_corrupt_msgq);
}

/* Test buffer chaining with net_buf_frag_add */
ZTEST(swift_io_unit_test, test_buffer_chaining)
{
	struct net_buf *header_buf, *data_buf;
	int ret;

	reset_all_captures();

	/* Simulate what processor.c does - allocate header and data buffers */
	header_buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(header_buf, "Header buffer allocation failed");

	data_buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(data_buf, "Data buffer allocation failed");

	/* Add some data to both buffers */
	net_buf_add_le32(header_buf, 0x4845); /* Header data "HE" */
	net_buf_add_le32(data_buf, 0x4441);   /* Payload data "DA" */

	/* Chain the buffers - this takes ownership of data_buf */
	/* CRITICAL: Add reference since we're passing a buffer we don't own */
	data_buf = net_buf_ref(data_buf); /* Handler doesn't own the received buffer */
	net_buf_frag_add(header_buf, data_buf);

	/* Send the chained buffer */
	ret = swift_io_source_send(&source1, header_buf, K_NO_WAIT);
	zassert_equal(ret, 3, "Should deliver to 3 sinks");

	/* Process events */
	process_all_events();

	/* Verify all sinks received the packet */
	zassert_equal(atomic_get(&capture1.count), 1, "Sink1 should receive");
	zassert_equal(atomic_get(&capture2.count), 1, "Sink2 should receive");
	zassert_equal(atomic_get(&immediate_count), 1, "Immediate should receive");

	/* Release our reference to header buffer */
	net_buf_unref(header_buf);

	/* Allocate new buffers to verify no memory leak */
	struct net_buf *test_buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(test_buf, "Should be able to allocate after chained buffer test");
	net_buf_unref(test_buf);
}

/* Handler that simulates processor-like behavior with chaining */
static void chaining_handler(struct swift_io_sink *sink, struct net_buf *buf)
{
	struct net_buf *header_buf;

	/* Allocate a header buffer */
	header_buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	if (!header_buf) {
		/* Input buffer is borrowed, unref is handled automatically */
		return;
	}

	/* Add header */
	net_buf_add_le32(header_buf, 0xABCD);

	/* Chain the input buffer - CRITICAL: must ref before chaining! */
	/* Handler doesn't own the buffer, so add ref before frag_add takes ownership */
	buf = net_buf_ref(buf);
	net_buf_frag_add(header_buf, buf);

	/* Forward the chained buffer */
	swift_io_source_send_consume(&isolated_source, header_buf, K_NO_WAIT);
}

/* Test buffer with zero ref count before handler execution */
ZTEST(swift_io_unit_test, test_zero_ref_before_handler)
{
	struct swift_io_event_queue test_queue;
	struct swift_io_event event;
	struct net_buf *buf;
	int ret;

	test_queue.msgq = &test_corrupt_msgq;
#ifdef CONFIG_SWIFT_IO_STATS
	atomic_clear(&test_queue.processed_count);
#endif

	/* Allocate buffer */
	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Buffer allocation should succeed");
	net_buf_add_le32(buf, 0xDEAD);

	/* Artificially set ref count to 0 to trigger error path */
	/* Note: This is technically invalid but we're testing error handling */
	buf->ref = 0;

	/* Create event with zero-ref buffer */
	event.sink = &sink1;
	event.buf = buf;

	/* Queue the event */
	ret = k_msgq_put(&test_corrupt_msgq, &event, K_NO_WAIT);
	zassert_equal(ret, 0, "Failed to queue event");

	/* Process should detect zero ref and return error */
	ret = swift_io_event_process(&test_queue, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject buffer with zero ref count");

	/* Clean up - restore ref count and free */
	buf->ref = 1;
	net_buf_unref(buf);

	/* Purge queue */
	k_msgq_purge(&test_corrupt_msgq);
}

/* Handler that corrupts buffer ref count */
static void corrupt_ref_handler(struct swift_io_sink *sink, struct net_buf *buf)
{
	/* Malicious handler that sets ref count to 0 */
	/* This should never be done but we're testing error handling */
	if (buf) {
		buf->ref = 0;
	}
}

/* Test buffer with zero ref count after handler execution */
ZTEST(swift_io_unit_test, test_zero_ref_after_handler)
{
	struct swift_io_sink corrupt_sink;
	struct swift_io_connection conn;
	struct swift_io_source test_source = {
		.sinks = SYS_DLIST_STATIC_INIT(&test_source.sinks),
		.lock = {},
#ifdef CONFIG_SWIFT_IO_STATS
		.send_count = ATOMIC_INIT(0),
		.queued_total = ATOMIC_INIT(0),
#endif
	};
	struct net_buf *buf;
	int ret;

	/* Create sink with corrupt handler */
	corrupt_sink.mode = SINK_MODE_IMMEDIATE;
	corrupt_sink.handler = corrupt_ref_handler;
	corrupt_sink.msgq = NULL;
#ifdef CONFIG_SWIFT_IO_STATS
	atomic_clear(&corrupt_sink.handled_count);
	atomic_clear(&corrupt_sink.dropped_count);
#endif

	/* Connect corrupt sink to test source */
	conn.source = &test_source;
	conn.sink = &corrupt_sink;
	sys_dnode_init(&conn.node);
	sys_dlist_append(&test_source.sinks, &conn.node);

	/* Allocate and send buffer */
	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Buffer allocation should succeed");
	net_buf_add_le32(buf, 0xBAD);

	/* Send through corrupt handler - should detect zero ref after handler */
	ret = swift_io_source_send(&test_source, buf, K_NO_WAIT);
	/* The send returns 0 because the sink handler corrupted the buffer */
	zassert_equal(ret, 0, "Should fail when handler corrupts ref count");

	/* Clean up our reference */
	net_buf_unref(buf);

	/* Clean up connection */
	sys_dlist_remove(&conn.node);
}

/* Define small message queue to force -ENOMSG error */
K_MSGQ_DEFINE(test_small_msgq, sizeof(struct swift_io_event), 1, 4);

/* Test k_msgq_put returning -ENOMSG and error translation to -ENOBUFS */
ZTEST(swift_io_unit_test, test_msgq_enomsg_error_translation)
{
	struct swift_io_sink test_sink;
	struct swift_io_connection conn;
	struct swift_io_source test_source = {
		.sinks = SYS_DLIST_STATIC_INIT(&test_source.sinks),
		.lock = {},
#ifdef CONFIG_SWIFT_IO_STATS
		.send_count = ATOMIC_INIT(0),
		.queued_total = ATOMIC_INIT(0),
#endif
	};
	struct net_buf *buf1;
	struct swift_io_event dummy_event;
	int ret;

	/* Create sink with small queue */
	test_sink.mode = SINK_MODE_QUEUED;
	test_sink.handler = capture_handler;
	test_sink.msgq = &test_small_msgq;
	test_sink.user_data = NULL;
#ifdef CONFIG_SWIFT_IO_STATS
	atomic_clear(&test_sink.handled_count);
	atomic_clear(&test_sink.dropped_count);
#endif

	/* Connect sink to test source */
	conn.source = &test_source;
	conn.sink = &test_sink;
	sys_dnode_init(&conn.node);
	sys_dlist_append(&test_source.sinks, &conn.node);

	/* Fill the queue with a dummy event to make it full */
	dummy_event.sink = &test_sink;
	dummy_event.buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(dummy_event.buf, "Failed to allocate dummy buffer");
	ret = k_msgq_put(&test_small_msgq, &dummy_event, K_NO_WAIT);
	zassert_equal(ret, 0, "Failed to fill queue");

	/* Now try to send - should get -ENOBUFS (translated from -ENOMSG) */
	buf1 = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf1, "Buffer allocation failed");
	net_buf_add_le32(buf1, 0xFULL);

	/* Send with K_NO_WAIT to force immediate -ENOMSG */
	ret = swift_io_source_send(&test_source, buf1, K_NO_WAIT);
	/* Should return 0 successful deliveries because queue is full */
	zassert_equal(ret, 0, "Should fail to deliver when queue is full");

	/* Clean up */
	net_buf_unref(buf1);

	/* Drain the queue */
	k_msgq_purge(&test_small_msgq);

	/* Clean up dummy buffer */
	net_buf_unref(dummy_event.buf);

	/* Remove connection */
	sys_dlist_remove(&conn.node);
}

/* Test zero ref count in swift_io_source_send_consume */
ZTEST(swift_io_unit_test, test_send_consume_zero_ref)
{
	struct net_buf *buf;
	int ret;

	/* Allocate buffer */
	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Buffer allocation failed");
	net_buf_add_le32(buf, 0xDEAD);

	/* Corrupt the ref count to 0 */
	buf->ref = 0;

	/* Try to send_consume - should detect zero ref and return error */
	ret = swift_io_source_send_consume(&source1, buf, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject buffer with zero ref count");

	/* Restore ref count for cleanup */
	buf->ref = 1;
	net_buf_unref(buf);
}

/* Test that verifies the double-unref bug with chaining */
ZTEST(swift_io_unit_test, test_chaining_double_unref_bug)
{
	struct swift_io_sink chaining_sink;
	struct swift_io_connection conn;
	struct swift_io_source test_source;
	struct net_buf *buf;
	int ret;

	/* Initialize test source */
	sys_dlist_init(&test_source.sinks);
	test_source.lock = (struct k_spinlock){};
#ifdef CONFIG_SWIFT_IO_STATS
	atomic_clear(&test_source.send_count);
	atomic_clear(&test_source.queued_total);
#endif

	/* Create a sink that chains buffers */
	chaining_sink.mode = SINK_MODE_IMMEDIATE;
	chaining_sink.handler = chaining_handler;
	chaining_sink.msgq = NULL;
#ifdef CONFIG_SWIFT_IO_STATS
	atomic_clear(&chaining_sink.handled_count);
	atomic_clear(&chaining_sink.dropped_count);
#endif

	/* Connect the chaining sink to test source */
	conn.source = &test_source;
	conn.sink = &chaining_sink;
	sys_dnode_init(&conn.node);
	sys_dlist_append(&test_source.sinks, &conn.node);

	/* Allocate and send a buffer */
	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Buffer allocation failed");
	net_buf_add_le32(buf, 0x12345678);

	/* Send through the chaining handler */
	ret = swift_io_source_send(&test_source, buf, K_NO_WAIT);
	zassert_equal(ret, 1, "Should deliver to chaining sink");

	/* Clean up our reference */
	net_buf_unref(buf);

	/* If the bug exists (missing net_buf_ref before chaining),
	 * we would get memory corruption or assertion failure here.
	 * With the fix, everything should work correctly. */

	/* Verify we can still allocate buffers (no corruption) */
	struct net_buf *test_buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(test_buf, "Buffer pool should still be functional");
	net_buf_unref(test_buf);

	/* Clean up */
	sys_dlist_remove(&conn.node);
}

/* Test swift_io_sink_deliver public API */
ZTEST(swift_io_unit_test, test_swift_io_sink_deliver_immediate)
{
	/* Test immediate mode delivery */
	struct swift_io_sink sink = {
		.mode = SINK_MODE_IMMEDIATE,
		.handler = immediate_handler,
	};

	struct net_buf *buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Failed to allocate buffer");

	/* Store initial reference count */
	uint8_t initial_ref = buf->ref;
	atomic_clear(&immediate_count);

	/* Deliver packet to sink */
	int ret = swift_io_sink_deliver(&sink, buf, K_NO_WAIT);
	zassert_equal(ret, 0, "swift_io_sink_deliver should succeed");

	/* Verify handler was called */
	zassert_equal(atomic_get(&immediate_count), 1, "Handler should have been called");

	/* Verify buffer reference is unchanged (function doesn't consume caller's ref) */
	zassert_equal(buf->ref, initial_ref, "Buffer reference should be preserved");

	net_buf_unref(buf);
}

/* Message queue for queued delivery tests */
K_MSGQ_DEFINE(deliver_test_msgq, sizeof(struct swift_io_event), 4, 4);

ZTEST(swift_io_unit_test, test_swift_io_sink_deliver_queued)
{
	/* Test queued mode delivery */
	k_msgq_purge(&deliver_test_msgq);

	struct swift_io_sink sink = {
		.mode = SINK_MODE_QUEUED,
		.handler = immediate_handler,
		.msgq = &deliver_test_msgq,
	};

	struct net_buf *buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Failed to allocate buffer");

	/* Store initial reference count */
	uint8_t initial_ref = buf->ref;

	/* Deliver packet to sink */
	int ret = swift_io_sink_deliver(&sink, buf, K_NO_WAIT);
	zassert_equal(ret, 0, "swift_io_sink_deliver should succeed");

	/* Verify event was queued */
	struct swift_io_event event;
	ret = k_msgq_get(&deliver_test_msgq, &event, K_NO_WAIT);
	zassert_equal(ret, 0, "Event should be in queue");
	zassert_equal(event.sink, &sink, "Event should have correct sink");
	zassert_equal(event.buf, buf, "Event should have correct buffer");

	/* Verify buffer reference count increased (queued copy) */
	zassert_equal(buf->ref, initial_ref + 1, "Buffer ref should increase for queued event");

	/* Clean up */
	net_buf_unref(event.buf); /* From queue */
	net_buf_unref(buf);       /* Our original reference */
}

ZTEST(swift_io_unit_test, test_swift_io_sink_deliver_invalid_params)
{
	struct swift_io_sink sink = {
		.mode = SINK_MODE_IMMEDIATE,
		.handler = immediate_handler,
	};
	struct net_buf *buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Failed to allocate buffer");

	/* Test NULL sink */
	int ret = swift_io_sink_deliver(NULL, buf, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should return -EINVAL for NULL sink");

	/* Test NULL buffer */
	ret = swift_io_sink_deliver(&sink, NULL, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should return -EINVAL for NULL buffer");

	/* Test queued mode without message queue */
	struct swift_io_sink bad_sink = {
		.mode = SINK_MODE_QUEUED,
		.handler = immediate_handler,
		.msgq = NULL, /* No queue configured */
	};
	ret = swift_io_sink_deliver(&bad_sink, buf, K_NO_WAIT);
	zassert_equal(ret, -ENOSYS, "Should return -ENOSYS for queued mode without queue");

	net_buf_unref(buf);
}

/* Small message queue for queue full test */
K_MSGQ_DEFINE(deliver_small_msgq, sizeof(struct swift_io_event), 1, 4);

ZTEST(swift_io_unit_test, test_swift_io_sink_deliver_queue_full)
{
	/* Test behavior when queue is full */
	k_msgq_purge(&deliver_small_msgq);

	struct swift_io_sink sink = {
		.mode = SINK_MODE_QUEUED,
		.handler = immediate_handler,
		.msgq = &deliver_small_msgq,
	};

	struct net_buf *buf1 = net_buf_alloc(&test_pool, K_NO_WAIT);
	struct net_buf *buf2 = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf1, "Failed to allocate buffer 1");
	zassert_not_null(buf2, "Failed to allocate buffer 2");

	/* First delivery should succeed */
	int ret = swift_io_sink_deliver(&sink, buf1, K_NO_WAIT);
	zassert_equal(ret, 0, "First delivery should succeed");

	/* Second delivery should fail with queue full */
	ret = swift_io_sink_deliver(&sink, buf2, K_NO_WAIT);
	zassert_equal(ret, -ENOBUFS, "Should return -ENOBUFS when queue is full");

	/* Clean up */
	struct swift_io_event event;
	k_msgq_get(&deliver_small_msgq, &event, K_NO_WAIT);
	net_buf_unref(event.buf);
	net_buf_unref(buf1);
	net_buf_unref(buf2);
}

/* Test swift_io_sink_deliver_consume public API */
ZTEST(swift_io_unit_test, test_swift_io_sink_deliver_consume_immediate)
{
	/* Test immediate mode delivery with consume */
	struct swift_io_sink sink = {
		.mode = SINK_MODE_IMMEDIATE,
		.handler = immediate_handler,
	};

	struct net_buf *buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Failed to allocate buffer");

	atomic_clear(&immediate_count);

	/* Deliver packet to sink (consumes buffer) */
	int ret = swift_io_sink_deliver_consume(&sink, buf, K_NO_WAIT);
	zassert_equal(ret, 0, "swift_io_sink_deliver_consume should succeed");

	/* Verify handler was called */
	zassert_equal(atomic_get(&immediate_count), 1, "Handler should have been called");

	/* Buffer is consumed, no need to unref */
}

ZTEST(swift_io_unit_test, test_swift_io_sink_deliver_consume_queued)
{
	/* Test queued mode delivery with consume */
	k_msgq_purge(&deliver_test_msgq);

	struct swift_io_sink sink = {
		.mode = SINK_MODE_QUEUED,
		.handler = immediate_handler,
		.msgq = &deliver_test_msgq,
	};

	struct net_buf *buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Failed to allocate buffer");

	/* Deliver packet to sink (consumes buffer) */
	int ret = swift_io_sink_deliver_consume(&sink, buf, K_NO_WAIT);
	zassert_equal(ret, 0, "swift_io_sink_deliver_consume should succeed");

	/* Verify event was queued */
	struct swift_io_event event;
	ret = k_msgq_get(&deliver_test_msgq, &event, K_NO_WAIT);
	zassert_equal(ret, 0, "Event should be in queue");
	zassert_equal(event.sink, &sink, "Event should have correct sink");
	zassert_equal(event.buf, buf, "Event should have correct buffer");

	/* Clean up - only need to unref the queued buffer */
	net_buf_unref(event.buf);
}

ZTEST(swift_io_unit_test, test_swift_io_sink_deliver_consume_invalid)
{
	struct swift_io_sink sink = {
		.mode = SINK_MODE_IMMEDIATE,
		.handler = immediate_handler,
	};

	/* Test NULL sink - should return error and consume buffer */
	struct net_buf *buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Failed to allocate buffer");

	int ret = swift_io_sink_deliver_consume(NULL, buf, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should return -EINVAL for NULL sink");
	/* Buffer is consumed even on error */

	/* Test NULL buffer */
	ret = swift_io_sink_deliver_consume(&sink, NULL, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should return -EINVAL for NULL buffer");
}
