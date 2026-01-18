/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "zephyr/sys/atomic.h"
#include "zephyr/sys/sflist.h"
#include "zephyr/sys/slist.h"
#include "zephyr/ztest_assert.h"
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/buf.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>
#include <zephyr_io/flow/flow.h>

LOG_MODULE_REGISTER(flow_test, LOG_LEVEL_INF);

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
FLOW_BUF_POOL_DEFINE(test_pool, TEST_BUFFER_POOL_SIZE, TEST_BUFFER_SIZE, NULL);

/* =============================================================================
 * Handler Buffer Management Rules:
 * - ALL handlers: MUST NOT call net_buf_unref() - framework handles it
 * - The flow framework automatically manages buffer references
 * =============================================================================
 */

/* Test context for counting packets */
struct test_capture {
	atomic_t count;
	bool has_value;
	uint32_t last_value;
};

/* Test capture contexts */
static struct test_capture captures[10] = {0};

size_t pool_num_free(struct net_buf_pool *pool);
static int get_sink_idx(struct flow_sink *sink);
/* QUEUED handler - counts packets and stores last value (no unref needed) */
static void capture_handler(struct flow_sink *sink, struct net_buf *buf_ref)
{
	zassert(sink != NULL, "Sink is NULL");
	zassert(buf_ref != NULL, "Buffer is NULL");
	zassert(sink->user_data != NULL, "Sink user_data is NULL");
	zassert(buf_ref->ref > 0, "Buffer ref is zero");

	struct test_capture *capture = (struct test_capture *)sink->user_data;

	atomic_inc(&capture->count);
	/* Store the last value seen */
	if (buf_ref && buf_ref->data && buf_ref->len >= 4) {
		capture->has_value = true;
		capture->last_value = sys_le32_to_cpu(*(uint32_t *)buf_ref->data);
	}
}

/* Define event queue for queued processing */
FLOW_EVENT_QUEUE_DEFINE(event_queue, TEST_EVENT_QUEUE_SIZE);

/* Define packet sources */
#define ISOLATED 3 /* Index of isolated source with no connections */
static struct flow_source sources[4] = {
	FLOW_SOURCE_INITIALIZER(source1, FLOW_PACKET_ID_ANY),
	FLOW_SOURCE_INITIALIZER(source2, FLOW_PACKET_ID_ANY),
	FLOW_SOURCE_INITIALIZER(source3, FLOW_PACKET_ID_ANY),
	FLOW_SOURCE_INITIALIZER(isolated, FLOW_PACKET_ID_ANY),
};

/* Define packet sinks */
static struct flow_sink sinks[3] = {
	FLOW_SINK_INITIALIZER(queued_1, SINK_MODE_QUEUED, capture_handler, &event_queue,
			      &captures[0], FLOW_PACKET_ID_ANY),
	FLOW_SINK_INITIALIZER(queued_2, SINK_MODE_QUEUED, capture_handler, &event_queue,
			      &captures[1], FLOW_PACKET_ID_ANY),
	FLOW_SINK_INITIALIZER(immediat_1, SINK_MODE_IMMEDIATE, capture_handler, NULL, &captures[2],
			      FLOW_PACKET_ID_ANY),
};

/* Define connections */
/* sources[0] -> sinks[0], sinks[1], sinks[2] */
FLOW_CONNECT(&sources[0], &sinks[0]);
FLOW_CONNECT(&sources[0], &sinks[1]);
FLOW_CONNECT(&sources[0], &sinks[2]);

/* sources[1] -> sinks[0], sinks[2] */
FLOW_CONNECT(&sources[1], &sinks[0]);
FLOW_CONNECT(&sources[1], &sinks[2]);

/* sources[2] -> sinks[1], sinks[2] */
FLOW_CONNECT(&sources[2], &sinks[1]);
FLOW_CONNECT(&sources[2], &sinks[2]);

bool connected[ARRAY_SIZE(sources)][ARRAY_SIZE(sinks)] = {
	{true, true, true},   /* sources[0] */
	{true, false, true},  /* sources[1] */
	{false, true, true},  /* sources[2] */
	{false, false, false} /* sources[ISOLATED] */
};

size_t num_connected[ARRAY_SIZE(sources)] = {
	3, /* sources[0] */
	2, /* sources[1] */
	2, /* sources[2] */
	0  /* sources[ISOLATED] */
};

size_t num_immediate[ARRAY_SIZE(sources)] = {
	1, /* sources[0] */
	1, /* sources[1] */
	1, /* sources[2] */
	0  /* sources[ISOLATED] */
};

/* =============================================================================
 * Helper Functions
 * =============================================================================
 */

/* Process all pending events */
static void process_all_events(void)
{
	int count = 0;
	while (flow_event_process(&event_queue, K_NO_WAIT) == 0 && count < TEST_MAX_EVENTS) {
		count++;
	}

	zassert_equal(k_msgq_num_used_get(&event_queue), 0, "Event queue should be empty");
}

static void reset_all_captures(void)
{
	ARRAY_FOR_EACH_PTR(captures, capture) {
		atomic_clear(&capture->count);
		capture->has_value = false;
		capture->last_value = 0;
	}
}

struct net_buf *alloc_net_buf(uint32_t value)
{
	struct net_buf *buf = flow_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Should allocate");
	zassert_equal(buf->ref, 1, "Initial ref = 1");

	net_buf_add_le32(buf, value);
	return buf;
}

/* Send test packet with data */
static int send_packet(int source_idx, uint32_t data, k_timeout_t timeout, bool check_ret)
{
	struct net_buf *buf;
	int ret;

	buf = alloc_net_buf(data);

	/* Use ref version to check reference counts */
	ret = flow_source_send_ref(&sources[source_idx], buf, timeout);
	if (check_ret) {
		zassert_equal(ret, num_connected[source_idx], "Send from sources[%d]", source_idx);
		zassert_equal(buf->ref, 1 + num_connected[source_idx] - num_immediate[source_idx],
			      "Ref after send from sources[%d]", source_idx);
	}

	net_buf_unref(buf);
	return ret;
}

static int get_sink_idx(struct flow_sink *sink)
{
	ARRAY_FOR_EACH_PTR(sinks, s) {
		if (s == sink) {
			return s - sinks;
		}
	}
	return -1;
}

static void verify_deliveries(size_t source_idx, int expected_count, int last_vaule,
			      bool immediate_only)
{
	ARRAY_FOR_EACH(sinks, sink_idx) {
		struct test_capture *capture = &captures[sink_idx];
		if (connected[source_idx][sink_idx] &&
		    (!immediate_only || sinks[sink_idx].mode == SINK_MODE_IMMEDIATE)) {
			zassert_equal(atomic_get(&capture->count), expected_count,
				      "Should have received expected packets");
			zassert_true(capture->has_value, "Should have value");
			zassert_equal(capture->last_value, last_vaule, "Should have correct value");
		} else {
			zassert_equal(atomic_get(&capture->count), 0, "Should be empty");
			zassert_false(capture->has_value, "Should not have value");
			zassert_equal(capture->last_value, 0, "Last value should be 0");
		}
	}
}

size_t pool_num_free(struct net_buf_pool *pool)
{
	return sys_sflist_len(&pool->free._queue.data_q) + pool->uninit_count;
}

/* Test setup - called before each test */
static void test_setup(void *fixture)
{
	ARG_UNUSED(fixture);

	reset_all_captures();
	k_msgq_purge(&event_queue);

#ifdef CONFIG_FLOW_STATS
	/* Reset all statistics */
	flow_source_reset_stats(&sources[0]);
	flow_source_reset_stats(&sources[1]);
	flow_source_reset_stats(&sources[2]);
	flow_source_reset_stats(&sources[ISOLATED]);

	flow_sink_reset_stats(&sinks[0]);
	flow_sink_reset_stats(&sinks[1]);
	flow_sink_reset_stats(&sinks[2]);
#endif
}

/* Test teardown - called after each test */
static void test_teardown(void *fixture)
{
	ARG_UNUSED(fixture);
	process_all_events();

	/* Verify connection integrity */
	ARRAY_FOR_EACH(sources, source_idx) {
		struct flow_source *source = &sources[source_idx];
		struct flow_connection *conn;

		bool connected_found[ARRAY_SIZE(sinks)] = {false};
		SYS_SLIST_FOR_EACH_CONTAINER(&source->connections, conn, node) {
			zassert_equal(conn->source, &sources[source_idx],
				      "sources[0] connection source mismatch");
			int sink_idx = get_sink_idx(conn->sink);
			zassert_true(sink_idx >= 0 && sink_idx < ARRAY_SIZE(sinks),
				     "sources[0] connection to unknown sink");
			zassert_true(connected[source_idx][sink_idx],
				     "sources[0] unexpected connection");
			zassert_false(connected_found[sink_idx], "sources[0] duplicate connection");
			connected_found[sink_idx] = true;
		}

		ARRAY_FOR_EACH(sinks, j) {
			zassert_equal(connected_found[j], connected[source_idx][j],
				      "sources[%d] connection to sinks[%d] mismatch", source_idx,
				      j);
		}
	}

	/* Verify valid sources */
	ARRAY_FOR_EACH(sources, i) {
		struct flow_source *source = &sources[i];
		zassert_equal(source->packet_id, FLOW_PACKET_ID_ANY,
			      "sources[%d] packet_id mismatch", i);
	}

	/* Verify valid sinks */
	ARRAY_FOR_EACH(sinks, i) {
		struct flow_sink *sink = &sinks[i];
		zassert_not_null(sink->handler, "sinks[%d] handler is NULL", i);
		if (i == 2) {
			zassert_equal(sink->mode, SINK_MODE_IMMEDIATE,
				      "sinks[%d] mode should be immediate", i);
			zassert_is_null(sink->msgq, "sinks[%d] msgq should be NULL", i);
		} else {
			zassert_equal(sink->mode, SINK_MODE_QUEUED,
				      "sinks[%d] mode should be queued", i);
			zassert_equal(sink->msgq, &event_queue, "sinks[%d] msgq pointer mismatch",
				      i);
		}
		zassert_equal(sink->accept_id, FLOW_PACKET_ID_ANY, "sinks[%d] packet_id mismatch",
			      i);
	}

	/* Verify no pending events */
	zassert_equal(k_msgq_num_used_get(&event_queue), 0, "Event queue not empty");
	zassert_equal(k_msgq_num_free_get(&event_queue), TEST_EVENT_QUEUE_SIZE,
		      "Event queue size mismatch");

	/* Verify no pending buffers */
	zassert_equal(pool_num_free(test_pool.pool), TEST_BUFFER_POOL_SIZE,
		      "Buffer pool size mismatch");
}

ZTEST_SUITE(flow_unit_test, NULL, NULL, test_setup, test_teardown, NULL);

/* =============================================================================
 * Basic Functionality Tests
 * =============================================================================
 */

/* Add test for flow_event_process timeout (tests line 178-179) */
ZTEST(flow_unit_test, test_event_process_empty_queue)
{
	int ret;

	/* Try to process from empty queue with no wait - should return -EAGAIN */
	ret = flow_event_process(&event_queue, K_NO_WAIT);
	zassert_equal(ret, -EAGAIN, "Should return -EAGAIN when queue is empty");

	/* Try with timeout */
	ret = flow_event_process(&event_queue, K_MSEC(TEST_TIMEOUT_MS));
	zassert_equal(ret, -EAGAIN, "Should return -EAGAIN after timeout");
}

/* =============================================================================
 * Connectivity and Routing Tests
 * =============================================================================
 */

ZTEST(flow_unit_test, test_basic_send)
{
	ARRAY_FOR_EACH(sources, source_idx) {
		send_packet(source_idx, source_idx, K_NO_WAIT, true);
		verify_deliveries(source_idx, 1, source_idx, true);
		process_all_events();
		verify_deliveries(source_idx, 1, source_idx, false);
		reset_all_captures();
	}
}

/* =============================================================================
 * Event Processing Tests
 * =============================================================================
 */

ZTEST(flow_unit_test, test_event_queue_processing)
{
	const int num_packets = 5;
	for (int source_idx = 0; source_idx < ARRAY_SIZE(sources); source_idx++) {
		/* Send multiple packets */
		for (int i = 0; i < num_packets; i++) {
			send_packet(source_idx, 0x1000 + i, K_NO_WAIT, true);
		}

		verify_deliveries(source_idx, num_packets, 0x1000 + num_packets - 1, true);

		/* Process events one by one */
		int processed = 0;
		int ret;
		do {
			ret = flow_event_process(&event_queue, K_NO_WAIT);
			if (ret == 0) {
				processed++;
				zassert_true(processed <= TEST_MAX_EVENTS,
					     "Too many events processed");
			}
		} while (ret == 0);

		/* Should have processed 10 events (5 packets * 2 sinks) */
		zassert_equal(processed,
			      num_packets * (num_connected[source_idx] - num_immediate[source_idx]),
			      "Should process 10 events");

		verify_deliveries(source_idx, num_packets, 0x1000 + num_packets - 1, false);
		reset_all_captures();
	}
}

ZTEST(flow_unit_test, test_event_timeout)
{
	int ret;
	uint64_t now;

	/* Try to process from empty queue */
	now = k_uptime_get();
	ret = flow_event_process(&event_queue, K_NO_WAIT);
	zassert_equal(ret, -EAGAIN, "Should return EAGAIN when empty");
	zassert_true(k_uptime_get() - now < TEST_TIMEOUT_MS, "Should not wait");

	/* Try with timeout */
	now = k_uptime_get();
	ret = flow_event_process(&event_queue, K_MSEC(TEST_TIMEOUT_MS));
	zassert_equal(ret, -EAGAIN, "Should timeout");
	zassert_true(k_uptime_get() - now >= TEST_TIMEOUT_MS, "Should have waited");
}

/* =============================================================================
 * Reference Counting Tests
 * =============================================================================
 */

ZTEST(flow_unit_test, test_send_consume)
{
	struct net_buf *buf;
	int ret;
	ARRAY_FOR_EACH(sources, source_idx) {
		buf = alloc_net_buf(0xFEED);
		ret = flow_source_send(&sources[source_idx], buf, K_NO_WAIT);
		zassert_equal(ret, num_connected[source_idx], "Send from sources[%d]", source_idx);
		zassert_equal(buf->ref, num_connected[source_idx] - num_immediate[source_idx],
			      "Ref after send from sources[%d]", source_idx);

		verify_deliveries(source_idx, 1, 0xFEED, true);
		process_all_events();
		verify_deliveries(source_idx, 1, 0xFEED, false);
		zassert_equal(pool_num_free(test_pool.pool), TEST_BUFFER_POOL_SIZE,
			      "Buffer pool count after send_consume");
		reset_all_captures();
	}
}

/* =============================================================================
 * Statistics Tests
 * =============================================================================
 */

#ifdef CONFIG_FLOW_STATS
ZTEST(flow_unit_test, test_statistics)
{
	uint32_t send_count, delivery_count;
	uint32_t handled_count, dropped_count;
	int ret;
	const int num_packets = 5;

	ARRAY_FOR_EACH(sources, source_idx) {
		flow_source_get_stats(&sources[source_idx], &send_count, &delivery_count);
		zassert_equal(send_count, 0, "Initial send_count should be 0");
		zassert_equal(delivery_count, 0, "Initial delivery_count should be 0");

		ARRAY_FOR_EACH(sinks, sink_idx) {
			flow_sink_get_stats(&sinks[sink_idx], &handled_count, &dropped_count);
			zassert_equal(handled_count, 0, "Initial handled_count should be 0");
			zassert_equal(dropped_count, 0, "Initial dropped_count should be 0");
		}

		/* Send packets */
		for (int i = 0; i < num_packets; i++) {
			ret = send_packet(source_idx, 0x2000 + i, K_NO_WAIT, true);
			/* Check source stats */
			flow_source_get_stats(&sources[source_idx], &send_count, &delivery_count);
			zassert_equal(send_count, i + 1, "Total sent");
			zassert_equal(delivery_count, num_connected[source_idx] * (i + 1),
				      "Total delivered");
			ARRAY_FOR_EACH(sinks, sink_idx) {
				flow_sink_get_stats(&sinks[sink_idx], &handled_count,
						    &dropped_count);
				if (!connected[source_idx][sink_idx]) {
					zassert_equal(handled_count, 0,
						      "Unconnected handled count");
					zassert_equal(dropped_count, 0,
						      "Unconnected dropped count");
					continue;
				}
				/* Immediate sinks handle right away */
				if (sinks[sink_idx].mode == SINK_MODE_IMMEDIATE) {
					zassert_equal(handled_count, i + 1,
						      "Immediate handled count");
					zassert_equal(dropped_count, 0, "Immediate dropped count");
				} else {
					zassert_equal(handled_count, 0, "Queued handled count");
					zassert_equal(dropped_count, 0, "Queued dropped count");
				}
			}
		}

		/* Process events */
		process_all_events();

		/* Final stats check */
		flow_source_get_stats(&sources[source_idx], &send_count, &delivery_count);
		zassert_equal(send_count, num_packets, "Final send count");
		zassert_equal(delivery_count, num_connected[source_idx] * num_packets,
			      "Final delivery count");
		ARRAY_FOR_EACH(sinks, sink_idx) {
			flow_sink_get_stats(&sinks[sink_idx], &handled_count, &dropped_count);
			if (connected[source_idx][sink_idx]) {
				zassert_equal(handled_count, num_packets,
					      "Final handled count for connected sink");
				zassert_equal(dropped_count, 0,
					      "Final dropped count for connected sink");
			} else {
				zassert_equal(handled_count, 0,
					      "Final handled count for unconnected sink");
				zassert_equal(dropped_count, 0,
					      "Final dropped count for unconnected sink");
			}
		}

		/* Test reset */
		flow_source_reset_stats(&sources[0]);
		flow_source_get_stats(&sources[0], &send_count, &delivery_count);
		zassert_equal(send_count, 0, "Reset to 0");
		zassert_equal(delivery_count, 0, "Reset to 0");

		ARRAY_FOR_EACH(sinks, sink_idx) {
			flow_sink_reset_stats(&sinks[sink_idx]);
			flow_sink_get_stats(&sinks[sink_idx], &handled_count, &dropped_count);
			zassert_equal(handled_count, 0, "Reset to 0");
			zassert_equal(dropped_count, 0, "Reset to 0");
		}
	}
}
#endif

/* =============================================================================
 * Stress Tests
 * =============================================================================
 */

ZTEST(flow_unit_test, test_many_packets)
{
	int ret;
	int sent = 0;
	size_t source_idx = 2;

	/* Send many packets */
	for (int i = 0; i < TEST_MANY_PACKETS; i++) {
		ret = send_packet(source_idx, i, K_NO_WAIT, true);
		if (ret > 0) {
			sent++;
		}
	}

	zassert_equal(sent, TEST_MANY_PACKETS, "Sent all packets");

	verify_deliveries(source_idx, TEST_MANY_PACKETS, TEST_MANY_PACKETS - 1, true);
	process_all_events();
	verify_deliveries(source_idx, TEST_MANY_PACKETS, TEST_MANY_PACKETS - 1, false);
}

ZTEST(flow_unit_test, test_queue_overflow)
{
	int ret;
	int source_idx = 2;
	size_t num_sent = 0;

	/* Send many packets to test handler resilience */
	while (k_msgq_num_free_get(&event_queue) > 0) {
		send_packet(source_idx, 0xBEEF, K_NO_WAIT, false);
		num_sent++;
	}

	verify_deliveries(source_idx, num_sent, 0xBEEF, true);

	/* Sending more packets should fail */
	ret = send_packet(source_idx, 0xDEAD, K_NO_WAIT, false);
	zassert_equal(ret, num_immediate[source_idx], "Should only send to immediate sinks");

	process_all_events();
}

/* =============================================================================
 * Concurrency Tests
 * =============================================================================
 */

/* Test concurrent event processing */
ZTEST(flow_unit_test, test_concurrent_receive)
{
	int num_packets = 5;

	ARRAY_FOR_EACH(sources, source_idx) {
		for (int i = 0; i < num_packets; i++) {
			send_packet(source_idx, 0x4000 + source_idx, K_NO_WAIT, true);
		}
	}

	process_all_events();

	ARRAY_FOR_EACH(sinks, sink_idx) {
		size_t connected_sources = 0;
		ARRAY_FOR_EACH(sources, source_idx) {
			if (connected[source_idx][sink_idx]) {
				connected_sources++;
			}
		}
		zassert_equal(atomic_get(&captures[sink_idx].count),
			      connected_sources * num_packets,
			      "sinks[%d] should process correct number of packets", sink_idx);
	}
}

/* =============================================================================
 * Input Validation and Error Handling Tests
 * =============================================================================
 */

/* Test invalid inputs for flow_source_send */
ZTEST(flow_unit_test, test_invalid_source_send)
{
	struct net_buf *buf;
	int ret;

	/* Test NULL source */
	buf = alloc_net_buf(0xFEED);
	ret = flow_source_send(NULL, buf, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject NULL source");
	net_buf_unref(buf);

	/* Test NULL buffer */
	ret = flow_source_send(&sources[0], NULL, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject NULL buffer");

	/* Test both NULL */
	ret = flow_source_send(NULL, NULL, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject NULL parameters");
}

/* Test invalid inputs for flow_source_send */
ZTEST(flow_unit_test, test_invalid_source_send_consume)
{
	struct net_buf *buf;
	int ret;

	/* Test NULL source */
	buf = alloc_net_buf(0xFEED);
	ret = flow_source_send(NULL, buf, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject NULL source");
	net_buf_unref(buf);

	/* Test NULL buffer */
	ret = flow_source_send(&sources[0], NULL, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject NULL buffer");

	/* Test both NULL */
	ret = flow_source_send(NULL, NULL, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject NULL parameters");
}

/* Test invalid inputs for flow_event_process */
ZTEST(flow_unit_test, test_invalid_event_process)
{
	int ret;

	/* Test NULL queue */
	ret = flow_event_process(NULL, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject NULL queue");
}

#ifdef CONFIG_FLOW_STATS
/* Test invalid inputs for statistics functions */
ZTEST(flow_unit_test, test_invalid_stats_functions)
{
	uint32_t value;

	/* Test flow_source_get_stats with NULL parameters */
	flow_source_get_stats(NULL, NULL, NULL);          /* all NULL */
	flow_source_get_stats(&sources[0], NULL, NULL);   /* Both NULL */
	flow_source_get_stats(&sources[0], &value, NULL); /* delivery_count NULL */
	flow_source_get_stats(&sources[0], NULL, &value); /* send_count NULL */

	/* Test flow_sink_get_stats with NULL parameters */
	flow_sink_get_stats(NULL, NULL, NULL);        /* all NULL */
	flow_sink_get_stats(&sinks[0], NULL, NULL);   /* Both NULL */
	flow_sink_get_stats(&sinks[0], &value, NULL); /* dropped_count NULL */
	flow_sink_get_stats(&sinks[0], NULL, &value); /* handled_count NULL */

	/* Test NULL source in reset_stats */
	flow_source_reset_stats(NULL);
	/* Should not crash, just return */

	/* Test NULL sink in reset_stats */
	flow_sink_reset_stats(NULL);
	/* Should not crash, just return */

	/* These functions handle NULL gracefully, so we just verify they don't crash
	 */
	zassert_true(true, "Statistics functions handled NULL inputs");
}
#endif

/* =============================================================================
 * Corruption and Defensive Programming Tests
 * =============================================================================
 */

/* Test sink with invalid handler (NULL) */
ZTEST(flow_unit_test, test_sink_null_handler)
{
	size_t source_idx = 2;
	sinks[1].handler = NULL; /* Corrupt! */

	int ret = send_packet(source_idx, 0xABCD, K_NO_WAIT, false);
	zassert_equal(ret, num_connected[source_idx] - 1,
		      "Should deliver to all but the sink with NULL handler");

	sinks[2].handler = NULL; /* Corrupt! */
	ret = send_packet(source_idx, 0xBADC0DE, K_NO_WAIT, false);
	zassert_equal(ret, num_connected[source_idx] - 2,
		      "Should deliver to all but the sinks with NULL handler");

	/* Restore original handler */
	sinks[1].handler = capture_handler;
	sinks[2].handler = capture_handler;
}

/* Test queued sink with NULL msgq */
ZTEST(flow_unit_test, test_sink_null_msgq)
{
	size_t source_idx = 0;

	sinks[0].msgq = NULL; /* Corrupt! */
	int ret = send_packet(source_idx, 0xABCD, K_NO_WAIT, false);
	zassert_equal(ret, num_connected[source_idx] - 1,
		      "Should deliver to all but the sink with NULL msgq");

	/* Restore original msgq */
	sinks[0].msgq = &event_queue;
}

/* Test corrupted connection with NULL sink during iteration */
ZTEST(flow_unit_test, test_corrupted_connection_null_sink)
{
	int source_idx = 0;

	/* get first connection */
	struct flow_connection *connection = NULL;
	connection = SYS_SLIST_PEEK_HEAD_CONTAINER(&sources[0].connections, connection, node);
	zassert_not_null(connection, "sources[0] should have connections");

	struct flow_sink *original_sink = connection->sink;
	connection->sink = NULL; /* Corrupt! */

	int ret = send_packet(source_idx, 0x1234, K_NO_WAIT, false);
	process_all_events();
	zassert_equal(ret, num_connected[source_idx] - 1,
		      "Should deliver to all but the connection with NULL sink");

	/* Restore original sink */
	connection->sink = original_sink;
}

/* Define message queue for corruption tests at file scope */
K_MSGQ_DEFINE(test_corrupt_msgq, sizeof(struct flow_event), TEST_CORRUPT_QUEUE_SIZE, 4);

/* Test corrupted event in queue */
ZTEST(flow_unit_test, test_corrupted_event_in_queue)
{
	struct flow_event bad_event;
	struct net_buf *buf;
	int ret;

	buf = alloc_net_buf(0xFEED);

	/* Test 1: Event with NULL sink */
	bad_event.sink = NULL;
	bad_event.buf = buf;

	ret = k_msgq_put(&test_corrupt_msgq, &bad_event, K_NO_WAIT);
	zassert_equal(ret, 0, "Failed to queue bad event");

	ret = flow_event_process(&test_corrupt_msgq, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject event with NULL sink");

	/* Test 2: Event with NULL buffer */
	bad_event.sink = &sinks[0];
	bad_event.buf = NULL;

	ret = k_msgq_put(&test_corrupt_msgq, &bad_event, K_NO_WAIT);
	zassert_equal(ret, 0, "Failed to queue bad event");

	ret = flow_event_process(&test_corrupt_msgq, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject event with NULL buffer");

	/* Test 3: Event with sink that has NULL handler */
	FLOW_SINK_DEFINE_IMMEDIATE(bad_sink, NULL);

	buf = alloc_net_buf(0xFEED);

	bad_event.sink = &bad_sink;
	bad_event.buf = buf;

	ret = k_msgq_put(&test_corrupt_msgq, &bad_event, K_NO_WAIT);
	zassert_equal(ret, 0, "Failed to queue bad event");

	ret = flow_event_process(&test_corrupt_msgq, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should reject event with NULL handler");

	/* Clean up any remaining messages */
	k_msgq_purge(&test_corrupt_msgq);
}

/* Test flow_sink_deliver_ref public API */
ZTEST(flow_unit_test, test_flow_sink_deliver_ref_invalid_params)
{
	struct flow_sink sink = {
		.mode = SINK_MODE_IMMEDIATE,
		.handler = capture_handler,
	};

	struct net_buf *buf = alloc_net_buf(0xFEED);

	/* Test NULL sink */
	int ret = flow_sink_deliver_ref(NULL, buf, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should return -EINVAL for NULL sink");

	/* Test NULL buffer */
	ret = flow_sink_deliver_ref(&sink, NULL, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should return -EINVAL for NULL buffer");

	/* Test queued mode without message queue */
	FLOW_SINK_DEFINE_QUEUED(bad_sink, capture_handler, NULL, NULL);
	ret = flow_sink_deliver_ref(&bad_sink, buf, K_NO_WAIT);
	zassert_equal(ret, -ENOSYS, "Should return -ENOSYS for queued mode without queue");

	net_buf_unref(buf);
}

ZTEST(flow_unit_test, test_flow_sink_deliver_ref)
{
	struct net_buf *buf;
	ARRAY_FOR_EACH(sinks, sink_idx) {

		buf = alloc_net_buf(0xFEED);

		/* Deliver packet to sink */
		int ret = flow_sink_deliver_ref(&sinks[sink_idx], buf, K_NO_WAIT);
		zassert_equal(ret, 0, "flow_sink_deliver_ref should succeed");

		if (sinks[sink_idx].mode == SINK_MODE_QUEUED) {
			zassert_equal(buf->ref, 2, "Buffer ref should increase for delivery");
			process_all_events();
		}

		/* Verify handler was called */
		zassert_equal(atomic_get(&captures[sink_idx].count), 1,
			      "Handler should have been called");
		zassert_true(captures[sink_idx].has_value, "Should have value");
		zassert_equal(captures[sink_idx].last_value, 0xFEED, "Should have correct value");

		/* Buffer reference should be preserved after handling */
		zassert_equal(buf->ref, 1, "Buffer reference should be preserved");
		net_buf_unref(buf);
	}
}

/* Test flow_sink_deliver public API (consuming version) */
ZTEST(flow_unit_test, test_flow_sink_deliver)
{
	struct net_buf *buf;
	ARRAY_FOR_EACH(sinks, sink_idx) {

		buf = alloc_net_buf(0xFEED);

		/* Deliver packet to sink */
		int ret = flow_sink_deliver(&sinks[sink_idx], buf, K_NO_WAIT);
		zassert_equal(ret, 0, "flow_sink_deliver should succeed");

		if (sinks[sink_idx].mode == SINK_MODE_QUEUED) {
			zassert_equal(buf->ref, 1, "Buffer ref should increase for delivery");
			process_all_events();
		}

		/* Verify handler was called */
		zassert_equal(atomic_get(&captures[sink_idx].count), 1,
			      "Handler should have been called");
		zassert_true(captures[sink_idx].has_value, "Should have value");
		zassert_equal(captures[sink_idx].last_value, 0xFEED, "Should have correct value");

		/* Buffer should be consumed */
		zassert_equal(pool_num_free(test_pool.pool), TEST_BUFFER_POOL_SIZE,
			      "Buffer should be consumed");
	}
}

ZTEST(flow_unit_test, test_flow_sink_deliver_invalid)
{
	struct flow_sink sink = {
		.mode = SINK_MODE_IMMEDIATE,
		.handler = capture_handler,
	};

	/* Test NULL sink - should return error and consume buffer */
	struct net_buf *buf = flow_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Failed to allocate buffer");

	int ret = flow_sink_deliver(NULL, buf, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should return -EINVAL for NULL sink");
	/* Buffer is consumed even on error */

	/* Test NULL buffer */
	ret = flow_sink_deliver(&sink, NULL, K_NO_WAIT);
	zassert_equal(ret, -EINVAL, "Should return -EINVAL for NULL buffer");
}

/* =============================================================================
 * Packet ID Filtering Tests
 * =============================================================================
 */

/* Define test packet IDs */
#define TEST_PACKET_ID_1 0x12
#define TEST_PACKET_ID_2 0x56
#define TEST_PACKET_ID_3 0x9A

FLOW_SOURCE_DEFINE_ROUTED(source_routed, TEST_PACKET_ID_1);
FLOW_SOURCE_DEFINE(source_any);

FLOW_SINK_DEFINE_IMMEDIATE(sink_any, capture_handler, &captures[4]);
FLOW_SINK_DEFINE_ROUTED_IMMEDIATE(sink_routed_same, capture_handler, TEST_PACKET_ID_1,
				  &captures[5]);
FLOW_SINK_DEFINE_ROUTED_IMMEDIATE(sink_routed_diff, capture_handler, TEST_PACKET_ID_2,
				  &captures[6]);
FLOW_SINK_DEFINE_ROUTED_IMMEDIATE(sink_routed_none, capture_handler, TEST_PACKET_ID_3,
				  &captures[7]);

FLOW_CONNECT(&source_routed, &sink_any);
FLOW_CONNECT(&source_routed, &sink_routed_same);
FLOW_CONNECT(&source_routed, &sink_routed_diff);

FLOW_CONNECT(&source_any, &sink_any);
FLOW_CONNECT(&source_any, &sink_routed_same);
FLOW_CONNECT(&source_any, &sink_routed_diff);

/* Test basic packet ID stamping */
ZTEST(flow_unit_test, test_packet_id_filtering)
{
	struct net_buf *buf;
	int ret;

	/* Allocate buffer with user data space */
	buf = alloc_net_buf(0xBEEF);
	zassert_not_null(buf, "Buffer allocation failed");

	/* Test sending from source with specific packet ID */
	ret = flow_source_send(&source_routed, buf, K_NO_WAIT);
	zassert_equal(ret, 2, "Should deliver to 2 sinks (matching + any)");
	zassert_equal(atomic_get(&((struct test_capture *)sink_any.user_data)->count), 1,
		      "sink_any should receive");
	zassert_equal(atomic_get(&((struct test_capture *)sink_routed_same.user_data)->count), 1,
		      "sink_routed_same should receive");
	zassert_equal(atomic_get(&((struct test_capture *)sink_routed_diff.user_data)->count), 0,
		      "sink_routed_diff should not receive");
	zassert_equal(atomic_get(&((struct test_capture *)sink_routed_none.user_data)->count), 0,
		      "sink_routed_none should not receive");

	reset_all_captures();

	buf = alloc_net_buf(0xDEAD);
	zassert_not_null(buf, "Buffer allocation failed");
	/* Test sending from source with any packet ID */
	ret = flow_source_send(&source_any, buf, K_NO_WAIT);
	zassert_equal(ret, 1, "Should deliver to all sinks");
	zassert_equal(atomic_get(&((struct test_capture *)sink_any.user_data)->count), 1,
		      "sink_any should receive");
	zassert_equal(atomic_get(&((struct test_capture *)sink_routed_same.user_data)->count), 0,
		      "sink_routed_same should receive");
	zassert_equal(atomic_get(&((struct test_capture *)sink_routed_diff.user_data)->count), 0,
		      "sink_routed_diff should receive");
	zassert_equal(atomic_get(&((struct test_capture *)sink_routed_none.user_data)->count), 0,
		      "sink_routed_none should not receive");
}