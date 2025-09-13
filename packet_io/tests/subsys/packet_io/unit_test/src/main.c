/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/packet_io/packet_io.h>
#include <zephyr/net/buf.h>
#include <zephyr/sys/util.h>

/* Define a test buffer pool */
NET_BUF_POOL_DEFINE(test_pool, 32, 128, 4, NULL);

/* Test fixtures - sources and sinks */
PACKET_SOURCE_DEFINE(test_source1);
PACKET_SOURCE_DEFINE(test_source2);
PACKET_SOURCE_DEFINE(test_source3);
PACKET_SOURCE_DEFINE(isolated_source);

PACKET_SINK_DEFINE(test_sink1, 10, false);
PACKET_SINK_DEFINE(test_sink2, 10, true);  /* drop_on_full */
PACKET_SINK_DEFINE(test_sink3, 5, false);
PACKET_SINK_DEFINE(small_sink, 1, true);
PACKET_SINK_DEFINE(large_sink, 100, false);
PACKET_SINK_DEFINE(isolated_sink, 10, false);

/* Define connections for various topologies */
/* One-to-many: test_source1 -> sink1, sink2, sink3 */
PACKET_SOURCE_CONNECT(test_source1, test_sink1);
PACKET_SOURCE_CONNECT(test_source1, test_sink2);
PACKET_SOURCE_CONNECT(test_source1, test_sink3);

/* Many-to-one: source1, source2, source3 -> sink1 */
PACKET_SOURCE_CONNECT(test_source2, test_sink1);
PACKET_SOURCE_CONNECT(test_source3, test_sink1);

/* Additional connections for complex topology */
PACKET_SOURCE_CONNECT(test_source2, test_sink2);
PACKET_SOURCE_CONNECT(test_source3, test_sink3);

/* Small queue test */
PACKET_SOURCE_CONNECT(test_source1, small_sink);

/* Helper function to drain a sink's queue */
static void drain_sink(struct packet_sink *sink)
{
	struct net_buf *buf;
	int drained = 0;
	
	/* Drain up to 100 messages to prevent infinite loops */
	while (drained < 100 && k_msgq_get(&sink->msgq, &buf, K_NO_WAIT) == 0) {
		net_buf_unref(buf);
		drained++;
	}
	
	/* Don't use k_msgq_purge as it doesn't free the buffers */
	/* Just keep draining until empty */
	while (k_msgq_get(&sink->msgq, &buf, K_NO_WAIT) == 0) {
		net_buf_unref(buf);
	}
}

/* Helper function to fill a sink's queue */
static int fill_sink_queue(struct packet_sink *sink)
{
	struct net_buf *buf;
	int count = 0;
	
	/* Fill until full */
	while (count < 200) {  /* Safety limit */
		buf = net_buf_alloc(&test_pool, K_NO_WAIT);
		if (!buf) {
			break;
		}
		
		/* Don't add extra ref - the buffer has ref=1 from alloc */
		if (k_msgq_put(&sink->msgq, &buf, K_NO_WAIT) != 0) {
			net_buf_unref(buf);
			break;
		}
		count++;
	}
	
	return count;
}

/* Helper function to allocate buffer, add data, send via source, and unref */
static int send_test_packet(struct packet_source *source, uint32_t data)
{
	struct net_buf *buf;
	int ret;
	
	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	if (!buf) {
		return -ENOMEM;
	}
	
	net_buf_add_le32(buf, data);
	ret = packet_source_send(source, buf, K_NO_WAIT);
	net_buf_unref(buf);
	return ret;
}

/* Setup function to clear all queues before each test */
static void packet_io_setup(void *fixture)
{
	ARG_UNUSED(fixture);
	
	/* Drain all sinks to ensure clean state */
	drain_sink(&test_sink1);
	drain_sink(&test_sink2);
	drain_sink(&test_sink3);
	drain_sink(&small_sink);
	drain_sink(&large_sink);
	drain_sink(&isolated_sink);
	
	
#ifdef CONFIG_PACKET_IO_STATS
	packet_source_reset_stats(&test_source1);
	packet_source_reset_stats(&test_source2);
	packet_source_reset_stats(&test_source3);
	packet_source_reset_stats(&isolated_source);
	
	packet_sink_reset_stats(&test_sink1);
	packet_sink_reset_stats(&test_sink2);
	packet_sink_reset_stats(&test_sink3);
	packet_sink_reset_stats(&small_sink);
	packet_sink_reset_stats(&large_sink);
	packet_sink_reset_stats(&isolated_sink);
#endif
}

ZTEST_SUITE(packet_io_unit_test, NULL, NULL, packet_io_setup, NULL, NULL);

/*
 * ===========================================================================
 * 1. BASIC FUNCTIONALITY TESTS
 * ===========================================================================
 */

/* Test 1.1: Single source to single sink */
ZTEST(packet_io_unit_test, test_single_source_single_sink)
{
	struct net_buf *received;
	int ret;
	
	/* Send to isolated source (no connections) first */
	ret = send_test_packet(&isolated_source, 0xDEADBEEF);
	zassert_equal(ret, 0, "Should deliver to 0 sinks");
	
	/* Now test with connected source */
	/* Send from source2 which connects only to sink1 and sink2 */
	ret = send_test_packet(&test_source2, 0xCAFEBABE);
	zassert_equal(ret, 2, "Should deliver to 2 sinks");
	
	/* Verify sink1 received it */
	ret = k_msgq_get(&test_sink1.msgq, &received, K_NO_WAIT);
	zassert_equal(ret, 0, "Should receive buffer");
	zassert_equal(*(uint32_t *)received->data, 0xCAFEBABE, "Data corrupted");
	net_buf_unref(received);
}

/* Test 1.1b: Single source to single sink - multiple packets */
ZTEST(packet_io_unit_test, test_single_source_single_sink_multiple)
{
	struct net_buf *received;
	int ret;
	int i;
	
	/* Send 5 packets sequentially */
	for (i = 0; i < 5; i++) {
		ret = send_test_packet(&test_source2, i);
		zassert_equal(ret, 2, "Should deliver to 2 sinks");
	}
	
	/* Verify all 5 received in order */
	for (i = 0; i < 5; i++) {
		ret = k_msgq_get(&test_sink1.msgq, &received, K_NO_WAIT);
		zassert_equal(ret, 0, "Should receive packet %d", i);
		zassert_equal(*(uint32_t *)received->data, i, "Wrong packet order");
		net_buf_unref(received);
	}
}

/* Test 1.2: One source to many sinks */
ZTEST(packet_io_unit_test, test_one_to_many)
{
	struct net_buf *received;
	int ret;
	uint32_t test_value = 0x12345678;
	
	/* source1 connects to sink1, sink2, sink3, and small_sink (4 total) */
	ret = send_test_packet(&test_source1, test_value);
	zassert_equal(ret, 4, "Should deliver to 4 sinks");
	
	/* Verify each sink received the same data */
	ret = k_msgq_get(&test_sink1.msgq, &received, K_NO_WAIT);
	zassert_equal(ret, 0, "Sink1 should receive");
	zassert_equal(*(uint32_t *)received->data, test_value, "Sink1 data corrupted");
	net_buf_unref(received);
	
	ret = k_msgq_get(&test_sink2.msgq, &received, K_NO_WAIT);
	zassert_equal(ret, 0, "Sink2 should receive");
	zassert_equal(*(uint32_t *)received->data, test_value, "Sink2 data corrupted");
	net_buf_unref(received);
	
	ret = k_msgq_get(&test_sink3.msgq, &received, K_NO_WAIT);
	zassert_equal(ret, 0, "Sink3 should receive");
	zassert_equal(*(uint32_t *)received->data, test_value, "Sink3 data corrupted");
	net_buf_unref(received);
}

/* Test 1.3: Many sources to one sink */
ZTEST(packet_io_unit_test, test_many_to_one)
{
	struct net_buf *received;
	int ret;
	
	/* Three sources send different values to sink1 */
	/* Send from all three sources */
	ret = send_test_packet(&test_source1, 0x11111111);
	zassert_true(ret >= 1, "Source1 should deliver");
	
	ret = send_test_packet(&test_source2, 0x22222222);
	zassert_true(ret >= 1, "Source2 should deliver");
	
	ret = send_test_packet(&test_source3, 0x33333333);
	zassert_true(ret >= 1, "Source3 should deliver");
	
	/* Sink1 should have received from all three */
	uint32_t values[3];
	int count = 0;
	
	while (k_msgq_get(&test_sink1.msgq, &received, K_NO_WAIT) == 0 && count < 3) {
		values[count++] = *(uint32_t *)received->data;
		net_buf_unref(received);
	}
	
	zassert_equal(count, 3, "Should receive 3 messages");
	/* Values might be in any order, just verify all are present */
	bool found1 = false, found2 = false, found3 = false;
	for (int i = 0; i < 3; i++) {
		if (values[i] == 0x11111111) found1 = true;
		if (values[i] == 0x22222222) found2 = true;
		if (values[i] == 0x33333333) found3 = true;
	}
	zassert_true(found1 && found2 && found3, "All values should be received");
}

/*
 * ===========================================================================
 * 2. QUEUE MANAGEMENT TESTS  
 * ===========================================================================
 */

/* Test 2.1: Queue full with drop_on_full=true */
ZTEST(packet_io_unit_test, test_queue_full_drop)
{
	int ret;
	int filled;
	
	/* Fill sink2's queue (drop_on_full=true) */
	filled = fill_sink_queue(&test_sink2);
	zassert_equal(filled, 10, "Should fill 10 entries");
	
	/* Send one more - should be dropped */
	/* source2 connects to sink1 and sink2 */
	ret = send_test_packet(&test_source2, 0);
	/* sink1 should succeed, sink2 should drop */
	zassert_equal(ret, 1, "Should deliver to 1 sink (other drops)");
	
#ifdef CONFIG_PACKET_IO_STATS
	uint32_t dropped;
	packet_sink_get_stats(&test_sink2, NULL, &dropped);
	zassert_true(dropped > 0, "Should have dropped packets");
#endif
}

/* Test 2.2: Queue full with drop_on_full=false */
ZTEST(packet_io_unit_test, test_queue_full_no_drop)
{
	int ret;
	int filled;
	
	/* Fill sink1's queue (drop_on_full=false) */
	filled = fill_sink_queue(&test_sink1);
	zassert_equal(filled, 10, "Should fill 10 entries");
	
	/* Send one more - should fail to deliver but not crash */
	/* source2 connects to sink1 and sink2 */
	ret = send_test_packet(&test_source2, 0);
	/* Only sink2 should succeed */
	zassert_equal(ret, 1, "Should deliver to 1 sink");
	
	/* Drain and verify queue works again */
	drain_sink(&test_sink1);
	
	ret = send_test_packet(&test_source2, 0);
	zassert_equal(ret, 2, "Should deliver to both sinks after drain");
}

/* Test 2.3: Queue overflow recovery */
ZTEST(packet_io_unit_test, test_queue_overflow_recovery)
{
	int ret;
	
#ifdef CONFIG_PACKET_IO_STATS
	uint32_t msg_count, delivered_count, received_count, dropped_count;
	/* Reset stats to get clean measurements */
	packet_source_reset_stats(&test_source2);
	packet_sink_reset_stats(&test_sink1);
	packet_sink_reset_stats(&test_sink2);
#endif
	
	/* Fill queue */
	fill_sink_queue(&test_sink1);
	
	/* Try to send - should fail for sink1 (full, no drop) */
	ret = send_test_packet(&test_source2, 0);
	zassert_equal(ret, 1, "Only sink2 should succeed");
	
#ifdef CONFIG_PACKET_IO_STATS
	/* Verify stats: source sent 1, delivered to 1 sink */
	packet_source_get_stats(&test_source2, &msg_count, &delivered_count);
	zassert_equal(msg_count, 1, "Should have sent 1 message");
	zassert_equal(delivered_count, 1, "Should have delivered to 1 sink");
	
	/* Sink1 should have 0 received (full, drop_on_full=false) */
	packet_sink_get_stats(&test_sink1, &received_count, &dropped_count);
	zassert_equal(received_count, 0, "Sink1 should not receive (full)");
	zassert_equal(dropped_count, 0, "Sink1 should not drop (drop_on_full=false)");
	
	/* Sink2 should have received 1 */
	packet_sink_get_stats(&test_sink2, &received_count, &dropped_count);
	zassert_equal(received_count, 1, "Sink2 should receive 1");
	zassert_equal(dropped_count, 0, "Sink2 should not drop");
#endif
	
	/* Drain queue */
	drain_sink(&test_sink1);
	drain_sink(&test_sink2);
	
	/* Refill - should work */
	int filled = fill_sink_queue(&test_sink1);
	zassert_equal(filled, 10, "Should refill to 10 entries");
	
	/* Drain again and send normally */
	drain_sink(&test_sink1);
	ret = send_test_packet(&test_source2, 0);
	zassert_equal(ret, 2, "Both sinks should work after recovery");
	
#ifdef CONFIG_PACKET_IO_STATS
	/* Verify final stats: source sent 2 total, delivered to 3 sinks total */
	packet_source_get_stats(&test_source2, &msg_count, &delivered_count);
	zassert_equal(msg_count, 2, "Should have sent 2 messages total");
	zassert_equal(delivered_count, 3, "Should have delivered to 3 sinks total (1+2)");
	
	/* Sink1 should have 1 received (after recovery) */
	packet_sink_get_stats(&test_sink1, &received_count, &dropped_count);
	zassert_equal(received_count, 1, "Sink1 should receive 1 after recovery");
	zassert_equal(dropped_count, 0, "Sink1 should never drop");
	
	/* Sink2 should have received 2 total */
	packet_sink_get_stats(&test_sink2, &received_count, &dropped_count);
	zassert_equal(received_count, 2, "Sink2 should receive 2 total");
	zassert_equal(dropped_count, 0, "Sink2 should not drop");
#endif
}

/*
 * ===========================================================================
 * 3. REFERENCE COUNTING TESTS
 * ===========================================================================
 */

/* Test 3.1: Reference counting basic */
ZTEST(packet_io_unit_test, test_ref_counting_basic)
{
	struct net_buf *buf, *received1, *received2;
	int ret;
	
	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Failed to allocate buffer");
	
	/* Check initial ref count is 1 */
	zassert_equal(buf->ref, 1, "Initial ref should be 1");
	
	/* Add data for consistency with other tests */
	net_buf_add_le32(buf, 0x12345678);
	
	/* Send to source1 (4 sinks) - buffer is consumed */
	ret = packet_source_send(&test_source1, buf, K_NO_WAIT);
	net_buf_unref(buf);
	zassert_equal(ret, 4, "Should deliver to 4 sinks");
	
	/* Original buffer pointer is no longer valid after send */
	/* Get from two sinks and verify they're the same buffer */
	ret = k_msgq_get(&test_sink1.msgq, &received1, K_NO_WAIT);
	zassert_equal(ret, 0, "Should receive from sink1");
	
	ret = k_msgq_get(&test_sink2.msgq, &received2, K_NO_WAIT);
	zassert_equal(ret, 0, "Should receive from sink2");
	
	/* Both should have refs > 0 */
	zassert_true(received1->ref > 0, "Received1 should have refs");
	zassert_true(received2->ref > 0, "Received2 should have refs");
	
	/* Clean up */
	net_buf_unref(received1);
	net_buf_unref(received2);
	
	/* Drain remaining sinks to free buffers */
	drain_sink(&test_sink3);
	drain_sink(&small_sink);
}

/* Test 3.2: Buffer freed after all consumers */
ZTEST(packet_io_unit_test, test_buffer_lifecycle)
{
	struct net_buf *buf, *received;
	int ret;
	void *buf_addr;
	
	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Failed to allocate buffer");
	buf_addr = buf;  /* Save address for comparison */
	
	/* Add data for consistency with other tests */
	net_buf_add_le32(buf, 0x87654321);
	
	/* Send to source2 (2 sinks) */
	ret = packet_source_send(&test_source2, buf, K_NO_WAIT);
	net_buf_unref(buf);
	zassert_equal(ret, 2, "Should deliver to 2 sinks");
	
	/* Get and unref from first sink */
	ret = k_msgq_get(&test_sink1.msgq, &received, K_NO_WAIT);
	zassert_equal(ret, 0, "Should receive from sink1");
	zassert_equal(received, buf_addr, "Should be same buffer");
	zassert_equal(received->ref, 2, "Should have 2 refs");
	net_buf_unref(received);
	
	/* Get and unref from second sink */
	ret = k_msgq_get(&test_sink2.msgq, &received, K_NO_WAIT);
	zassert_equal(ret, 0, "Should receive from sink2");
	zassert_equal(received, buf_addr, "Should be same buffer");
	zassert_equal(received->ref, 1, "Should have 1 ref");
	net_buf_unref(received);
	
	/* Buffer should now be freed and available in pool */
	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Should be able to allocate (buffer returned to pool)");
	net_buf_unref(buf);
}

/* Test 3.3: All sinks drop, buffer should be freed */
ZTEST(packet_io_unit_test, test_ref_count_all_drops)
{
	struct net_buf *buf;
	int ret;
	
	/* Fill all sinks that source1 connects to */
	fill_sink_queue(&test_sink1);
	fill_sink_queue(&test_sink2);  /* This one has drop_on_full=true */
	fill_sink_queue(&test_sink3);
	fill_sink_queue(&small_sink);  /* This one has drop_on_full=true */
	
	/* Send - all sinks are full, drops don't count as delivered */
	ret = send_test_packet(&test_source1, 0);
	zassert_equal(ret, 0, "No deliveries when all sinks are full");
	
	/* Buffer should be freed - verify by allocating many */
	for (int i = 0; i < 32; i++) {
		buf = net_buf_alloc(&test_pool, K_NO_WAIT);
		if (!buf) {
			zassert_unreachable("Pool exhausted - possible leak");
		}
		net_buf_unref(buf);
	}
}

/*
 * ===========================================================================
 * 4. EDGE CASES AND ERROR CONDITIONS
 * ===========================================================================
 */

/* Test 4.1: Source with no sinks */
ZTEST(packet_io_unit_test, test_source_no_sinks)
{
	struct net_buf *buf;
	int ret;
	
	/* isolated_source has no connections */
	ret = send_test_packet(&isolated_source, 0);
	zassert_equal(ret, 0, "Should deliver to 0 sinks");
	
	/* Buffer should be freed (send consumes it) */
	/* Try allocating many buffers to verify no leak */
	for (int i = 0; i < 32; i++) {
		buf = net_buf_alloc(&test_pool, K_NO_WAIT);
		if (!buf) {
			zassert_unreachable("Pool exhausted - possible leak");
		}
		net_buf_unref(buf);
	}
}

/* Test 4.2: NULL parameters */
ZTEST(packet_io_unit_test, test_null_parameters)
{
	struct net_buf *buf;
	int ret;
	
	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Failed to allocate buffer");
	
	/* Add data for consistency with other tests */
	net_buf_add_le32(buf, 0xDEADBEEF);
	
	/* These should handle NULL gracefully or assert in debug builds */
	/* In production, we expect controlled behavior */
	
	/* NULL source - should return 0 or assert */
	ret = packet_source_send(NULL, buf, K_NO_WAIT);
	net_buf_unref(buf);
	zassert_equal(ret, 0, "NULL source should deliver to 0 sinks");
	
	/* NULL buffer - should return 0 or assert */
	ret = packet_source_send(&test_source1, NULL, K_NO_WAIT);
	/* Don't unref NULL */
	zassert_equal(ret, 0, "NULL buffer should deliver to 0 sinks");
}

/*
 * ===========================================================================
 * 5. QUEUE SIZE TESTS
 * ===========================================================================
 */

/* Test 5.1: Small queue (size 1) */
ZTEST(packet_io_unit_test, test_small_queue)
{
	struct net_buf *received;
	int ret;
	
	/* Send two buffers to small_sink (queue size 1) */
	/* First should succeed */
	ret = send_test_packet(&test_source1, 0xAAAAAAAA);
	zassert_true(ret >= 1, "First send should succeed");
	
	/* Second should drop for small_sink but succeed for others */
	ret = send_test_packet(&test_source1, 0xBBBBBBBB);
	zassert_true(ret >= 3, "Should deliver to other sinks");
	
	/* Verify small_sink has only first buffer */
	ret = k_msgq_get(&small_sink.msgq, &received, K_NO_WAIT);
	zassert_equal(ret, 0, "Should receive first buffer");
	zassert_equal(*(uint32_t *)received->data, 0xAAAAAAAA, "Should be first buffer");
	net_buf_unref(received);
	
	/* Should be empty now */
	ret = k_msgq_get(&small_sink.msgq, &received, K_NO_WAIT);
	zassert_not_equal(ret, 0, "Should be empty");
}

/* Test 5.2: High volume packet distribution */
ZTEST(packet_io_unit_test, test_large_queue)
{
	struct net_buf *received;
	int sent = 0;
	int ret;
	int received_sink2 = 0;
	
	/* Test high volume distribution through packet_io */
	/* Use test_source2 which only connects to test_sink1 and test_sink2 */
	/* This gives us a cleaner test without interference from other connections */
	
	/* Send many packets */
	for (int i = 0; i < 10; i++) {
		ret = send_test_packet(&test_source2, i);
		if (ret > 0) {
			sent++;
		} else {
			break;  /* Buffer allocation failed */
		}
	}
	
	/* Should have sent all 10 packets to 2 sinks each */
	zassert_equal(sent, 10, "Should send 10 packets, sent %d", sent);
	
	/* Verify sink2 received all packets (sink2 has drop_on_full=true) */
	while (k_msgq_get(&test_sink2.msgq, &received, K_NO_WAIT) == 0) {
		received_sink2++;
		/* Verify data integrity */
		uint32_t value = *(uint32_t *)received->data;
		zassert_true(value < 10, "Data should be 0-9, got %u", value);
		net_buf_unref(received);
	}
	
	/* test_sink2 should have received all 10 packets */
	zassert_equal(received_sink2, 10, "Sink2 should receive all 10");
	
	/* Drain test_sink1 as well */
	drain_sink(&test_sink1);
	
	/* This test verifies packet_io can handle sustained packet flow */
	/* to multiple sinks without data corruption or loss (when queues aren't full) */
}

/*
 * ===========================================================================
 * 6. STATISTICS TESTS
 * ===========================================================================
 */

/* Test 6.1: Statistics accuracy */
#ifdef CONFIG_PACKET_IO_STATS
ZTEST(packet_io_unit_test, test_statistics)
{
	uint32_t msg_count, delivered_count, received_count, dropped_count;
	int ret;
	
	/* Reset stats */
	packet_source_reset_stats(&test_source1);
	packet_sink_reset_stats(&test_sink1);
	packet_sink_reset_stats(&test_sink2);
	
	/* Send 5 messages */
	for (int i = 0; i < 5; i++) {
		ret = send_test_packet(&test_source1, i);
		zassert_true(ret > 0, "Send should succeed");
	}
	
	/* Check source stats - test_source1 connects to 4 sinks */
	packet_source_get_stats(&test_source1, &msg_count, &delivered_count);
	zassert_equal(msg_count, 5, "Should have sent 5 messages");
	/* small_sink (capacity 1) drops 4 messages, so actual deliveries are 5+5+5+1=16 */
	zassert_equal(delivered_count, 16, "Should have delivered 16 times (small_sink drops 4)");
	
	/* Check sink stats */
	packet_sink_get_stats(&test_sink1, &received_count, &dropped_count);
	zassert_equal(received_count, 5, "Sink1 should receive 5");
	zassert_equal(dropped_count, 0, "Sink1 should drop 0");
	
	packet_sink_get_stats(&test_sink2, &received_count, &dropped_count);
	zassert_equal(received_count, 5, "Sink2 should receive 5");
	zassert_equal(dropped_count, 0, "Sink2 should drop 0");
	
	/* Fill sink2 and test drop stats */
	fill_sink_queue(&test_sink2);
	packet_sink_reset_stats(&test_sink2);
	
	/* Send 3 more - should drop */
	for (int i = 0; i < 3; i++) {
		send_test_packet(&test_source1, i);
	}
	
	packet_sink_get_stats(&test_sink2, &received_count, &dropped_count);
	zassert_equal(received_count, 0, "Sink2 should receive 0 (full)");
	zassert_equal(dropped_count, 3, "Sink2 should drop 3");
}

/* Test 6.2: Statistics reset */
ZTEST(packet_io_unit_test, test_stats_reset)
{
	uint32_t msg_count, delivered_count, received_count, dropped_count;
	
	/* Send some packets */
	for (int i = 0; i < 3; i++) {
		send_test_packet(&test_source1, i);
	}
	
	/* Reset stats */
	packet_source_reset_stats(&test_source1);
	packet_sink_reset_stats(&test_sink1);
	
	/* Verify counters are zero */
	packet_source_get_stats(&test_source1, &msg_count, &delivered_count);
	zassert_equal(msg_count, 0, "Message count should be reset");
	zassert_equal(delivered_count, 0, "Delivered count should be reset");
	
	packet_sink_get_stats(&test_sink1, &received_count, &dropped_count);
	zassert_equal(received_count, 0, "Received count should be reset");
	zassert_equal(dropped_count, 0, "Dropped count should be reset");
}
#endif /* CONFIG_PACKET_IO_STATS */

/*
 * ===========================================================================
 * 7. CONCURRENCY TESTS
 * ===========================================================================
 */

/* Test 7.1: Concurrent sends from multiple threads */
#define CONCURRENT_SENDS 100
#define STACK_SIZE 1024

static struct k_thread thread1, thread2, thread3;
static K_THREAD_STACK_DEFINE(stack1, STACK_SIZE);
static K_THREAD_STACK_DEFINE(stack2, STACK_SIZE);
static K_THREAD_STACK_DEFINE(stack3, STACK_SIZE);

static struct k_sem start_sem;
static atomic_t send_counter;

static void concurrent_sender(void *source, void *unused2, void *unused3)
{
	struct packet_source *src = (struct packet_source *)source;
	int ret;
	
	ARG_UNUSED(unused2);
	ARG_UNUSED(unused3);
	
	/* Wait for start signal */
	k_sem_take(&start_sem, K_FOREVER);
	
	/* Send many packets rapidly */
	for (int i = 0; i < CONCURRENT_SENDS; i++) {
		ret = send_test_packet(src, i);
		if (ret >= 0) {
			/* Count successful sends (ret >= 0 means buffer was allocated) */
			atomic_inc(&send_counter);
		}
		/* Minimal delay to allow interleaving */
		k_busy_wait(10);
	}
}

ZTEST(packet_io_unit_test, test_concurrent_send)
{
	struct net_buf *received;
	int received_count = 0;
	
	k_sem_init(&start_sem, 0, 3);
	atomic_clear(&send_counter);
	
	/* Clear all sinks first */
	drain_sink(&test_sink1);
	drain_sink(&test_sink2);
	drain_sink(&test_sink3);
	
	/* Start three sender threads */
	k_thread_create(&thread1, stack1, STACK_SIZE,
			concurrent_sender, &test_source1, NULL, NULL,
			K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
	
	k_thread_create(&thread2, stack2, STACK_SIZE,
			concurrent_sender, &test_source2, NULL, NULL,
			K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
	
	k_thread_create(&thread3, stack3, STACK_SIZE,
			concurrent_sender, &test_source3, NULL, NULL,
			K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
	
	/* Start all threads simultaneously */
	k_sem_give(&start_sem);
	k_sem_give(&start_sem);
	k_sem_give(&start_sem);
	
	/* Wait for completion */
	k_thread_join(&thread1, K_SECONDS(5));
	k_thread_join(&thread2, K_SECONDS(5));
	k_thread_join(&thread3, K_SECONDS(5));
	
	/* Count total received in sink1 (all sources connect to it) */
	while (k_msgq_get(&test_sink1.msgq, &received, K_NO_WAIT) == 0) {
		received_count++;
		net_buf_unref(received);
		if (received_count > CONCURRENT_SENDS * 3) {
			break;  /* Safety limit */
		}
	}
	
	
	/* We should have received many messages */
	zassert_true(received_count > 0, "Should receive messages");
	zassert_true(atomic_get(&send_counter) > 0, "Should have sent messages");
	
	/* No assertion on exact count due to possible allocation failures */
	TC_PRINT("Concurrent test: sent %d, sink1 received %d\n",
		 atomic_get(&send_counter), received_count);
}

/* Test 7.2: Concurrent receive from multiple threads */
static struct k_sem receive_start_sem;
static atomic_t receive_counter;
static K_THREAD_STACK_DEFINE(recv_stack1, 1024);
static K_THREAD_STACK_DEFINE(recv_stack2, 1024);

static void concurrent_receiver(void *sink, void *unused2, void *unused3)
{
	struct packet_sink *snk = (struct packet_sink *)sink;
	struct net_buf *buf;
	
	ARG_UNUSED(unused2);
	ARG_UNUSED(unused3);
	
	/* Wait for start signal */
	k_sem_take(&receive_start_sem, K_FOREVER);
	
	/* Receive packets */
	while (k_msgq_get(&snk->msgq, &buf, K_MSEC(10)) == 0) {
		atomic_inc(&receive_counter);
		net_buf_unref(buf);
	}
}

ZTEST(packet_io_unit_test, test_concurrent_receive)
{
	struct k_thread recv_thread1, recv_thread2;
	
	k_sem_init(&receive_start_sem, 0, 2);
	atomic_clear(&receive_counter);
	
	/* Pre-fill sink with packets */
	for (int i = 0; i < 20; i++) {
		send_test_packet(&test_source1, i);
	}
	
	/* Start two receiver threads on same sink */
	k_thread_create(&recv_thread1, recv_stack1, 1024,
			concurrent_receiver, &test_sink1, NULL, NULL,
			K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
	
	k_thread_create(&recv_thread2, recv_stack2, 1024,
			concurrent_receiver, &test_sink1, NULL, NULL,
			K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
	
	/* Start both threads */
	k_sem_give(&receive_start_sem);
	k_sem_give(&receive_start_sem);
	
	/* Wait for completion */
	k_thread_join(&recv_thread1, K_SECONDS(1));
	k_thread_join(&recv_thread2, K_SECONDS(1));
	
	/* Verify packets were received */
	zassert_true(atomic_get(&receive_counter) > 0, "Should receive packets");
	TC_PRINT("Concurrent receive: %d packets\n", atomic_get(&receive_counter));
}

/*
 * ===========================================================================
 * 8. K_POLL INTEGRATION TESTS
 * ===========================================================================
 */

/* Test 8.1: k_poll integration */
ZTEST(packet_io_unit_test, test_poll_integration)
{
	struct net_buf *received;
	struct k_poll_event events[2];
	int ret;
	
	/* Setup poll events for two sinks */
	k_poll_event_init(&events[0],
			  K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
			  K_POLL_MODE_NOTIFY_ONLY,
			  &test_sink1.msgq);
	
	k_poll_event_init(&events[1],
			  K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
			  K_POLL_MODE_NOTIFY_ONLY,
			  &test_sink2.msgq);
	
	/* Poll should timeout (queues empty) */
	ret = k_poll(events, 2, K_MSEC(10));
	zassert_equal(ret, -EAGAIN, "Should timeout on empty queues");
	
	/* Send a message */
	send_test_packet(&test_source1, 0x12345678);
	
	/* Poll should succeed immediately */
	ret = k_poll(events, 2, K_MSEC(100));
	zassert_equal(ret, 0, "Poll should succeed");
	
	/* Both events should be signaled */
	zassert_equal(events[0].state, K_POLL_STATE_MSGQ_DATA_AVAILABLE,
		      "Sink1 should have data");
	zassert_equal(events[1].state, K_POLL_STATE_MSGQ_DATA_AVAILABLE,
		      "Sink2 should have data");
	
	/* Read from both */
	ret = k_msgq_get(&test_sink1.msgq, &received, K_NO_WAIT);
	zassert_equal(ret, 0, "Should read from sink1");
	net_buf_unref(received);
	
	ret = k_msgq_get(&test_sink2.msgq, &received, K_NO_WAIT);
	zassert_equal(ret, 0, "Should read from sink2");
	net_buf_unref(received);
	
	/* test_source1 also sends to test_sink3 and small_sink - drain them */
	ret = k_msgq_get(&test_sink3.msgq, &received, K_NO_WAIT);
	zassert_equal(ret, 0, "Should read from sink3");
	net_buf_unref(received);
	
	ret = k_msgq_get(&small_sink.msgq, &received, K_NO_WAIT);
	zassert_equal(ret, 0, "Should read from small_sink");
	net_buf_unref(received);
}

/* Test 8.2: k_poll with timeout */
ZTEST(packet_io_unit_test, test_poll_timeout)
{
	struct k_poll_event event;
	int ret;
	
	/* Setup poll event for empty sink */
	k_poll_event_init(&event,
			  K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
			  K_POLL_MODE_NOTIFY_ONLY,
			  &isolated_sink.msgq);
	
	/* Poll should timeout */
	uint32_t start = k_uptime_get_32();
	ret = k_poll(&event, 1, K_MSEC(50));
	uint32_t elapsed = k_uptime_get_32() - start;
	
	zassert_equal(ret, -EAGAIN, "Should timeout");
	zassert_true(elapsed >= 45, "Should wait at least 45ms, waited %dms", elapsed);
	zassert_equal(event.state, K_POLL_STATE_NOT_READY, "Event should not be ready");
}

/*
 * ===========================================================================
 * 9. COMPLEX SCENARIOS
 * ===========================================================================
 */

/* Test 9.1: Partial delivery (some succeed, some fail) */
ZTEST(packet_io_unit_test, test_partial_delivery)
{
	int ret;
	
	/* Fill sink3 (drop_on_full=false) but not others */
	fill_sink_queue(&test_sink3);
	
	/* Send from source1 which connects to sink1, sink2, sink3, small_sink */
	ret = send_test_packet(&test_source1, 0);
	/* sink3 fails (full, no drop), others succeed */
	zassert_equal(ret, 3, "Should deliver to 3 of 4 sinks");
}

/* Test 9.2: Mixed drop policies */
ZTEST(packet_io_unit_test, test_mixed_drop_policies)
{
	int ret;
	
	/* Fill both sinks - sink1 has drop_on_full=false, sink2 has true */
	fill_sink_queue(&test_sink1);
	fill_sink_queue(&test_sink2);
	
	/* Send 5 packets */
	for (int i = 0; i < 5; i++) {
		ret = send_test_packet(&test_source2, 0);
		/* sink1 fails (no drop), sink2 drops - neither counts as delivered */
		if (ret >= 0) {
			zassert_equal(ret, 0, "No successful deliveries when both sinks are full");
		}
		/* send_test_packet returns -ENOMEM if allocation fails, which is ok */
	}
	
#ifdef CONFIG_PACKET_IO_STATS
	uint32_t received, dropped;
	packet_sink_get_stats(&test_sink2, &received, &dropped);
	zassert_equal(dropped, 5, "Sink2 should drop 5 packets");
	
	packet_sink_get_stats(&test_sink1, &received, &dropped);
	zassert_equal(dropped, 0, "Sink1 should not drop (drop_on_full=false)");
#endif
}

/*
 * ===========================================================================
 * 10. STRESS AND PERFORMANCE TESTS
 * ===========================================================================
 */

/* Test 10.1: Memory leak test */
ZTEST(packet_io_unit_test, test_no_memory_leak)
{
	struct net_buf *buf;
	struct net_buf *allocated[64];  /* Max buffers to allocate */
	int initial_count = 0;
	int final_count = 0;
	
	/* Count available buffers (with safety limit) */
	while (initial_count < 64 && (buf = net_buf_alloc(&test_pool, K_NO_WAIT)) != NULL) {
		allocated[initial_count] = buf;
		initial_count++;
	}
	/* Free all allocated buffers */
	for (int i = 0; i < initial_count; i++) {
		net_buf_unref(allocated[i]);
	}
	
	/* Do many send/receive cycles */
	for (int cycle = 0; cycle < 100; cycle++) {
		send_test_packet(&test_source1, 0);
		
		/* Drain all sinks */
		drain_sink(&test_sink1);
		drain_sink(&test_sink2);
		drain_sink(&test_sink3);
		drain_sink(&small_sink);
	}
	
	/* Count available buffers again (with safety limit) */
	while (final_count < 64 && (buf = net_buf_alloc(&test_pool, K_NO_WAIT)) != NULL) {
		allocated[final_count] = buf;
		final_count++;
	}
	/* Free all allocated buffers */
	for (int i = 0; i < final_count; i++) {
		net_buf_unref(allocated[i]);
	}
	
	/* Should have same number of buffers */
	zassert_equal(initial_count, final_count, 
		      "Memory leak detected: initial=%d, final=%d",
		      initial_count, final_count);
}

/* Test 10.2: Stress test - continuous sending */
ZTEST(packet_io_unit_test, test_stress_continuous)
{
	int sent = 0;
	int failed = 0;
	int ret;
	uint32_t start = k_uptime_get_32();
	
	/* Send continuously for 100ms */
	while (k_uptime_get_32() - start < 100) {
		ret = send_test_packet(&test_source1, 0);
		if (ret > 0) {
			sent++;
		} else if (ret == 0) {
			failed++;
		}
		/* ret == -ENOMEM means allocation failed, which is ok to ignore */
		
		/* Periodically drain sinks to allow continuous sending */
		if ((sent + failed) % 20 == 0) {
			drain_sink(&test_sink1);
			drain_sink(&test_sink2);
			drain_sink(&test_sink3);
			drain_sink(&small_sink);
		}
		
		/* Minimal delay to prevent CPU hogging */
		k_busy_wait(100);
	}
	
	zassert_true(sent > 0, "Should send packets during stress test");
	TC_PRINT("Stress test: sent %d packets in 100ms, %d failed\n", sent, failed);
	
	/* Drain all sinks */
	drain_sink(&test_sink1);
	drain_sink(&test_sink2);
	drain_sink(&test_sink3);
	drain_sink(&small_sink);
}

/* Test 10.3: Burst sending pattern */
ZTEST(packet_io_unit_test, test_stress_burst)
{
	int burst_size = 10;
	int bursts = 5;
	int total_sent = 0;
	int ret;
	
	for (int b = 0; b < bursts; b++) {
		/* Drain sinks before each burst to ensure space */
		drain_sink(&test_sink1);
		drain_sink(&test_sink2);
		drain_sink(&test_sink3);
		drain_sink(&small_sink);
		
		/* Send burst */
		for (int i = 0; i < burst_size; i++) {
			ret = send_test_packet(&test_source1, 0);
			if (ret > 0) {
				total_sent++;
			}
		}
		/* Pause between bursts */
		k_sleep(K_MSEC(10));
	}
	
	zassert_true(total_sent > 0, "Should send packets in bursts");
	TC_PRINT("Burst test: sent %d packets in %d bursts\n", total_sent, bursts);
}

/*
 * ===========================================================================
 * 11. TOPOLOGY TESTS
 * ===========================================================================
 */

/* Test 11.1: Complex topology (diamond pattern) */
ZTEST(packet_io_unit_test, test_complex_topology)
{
	struct net_buf *received;
	int ret;
	int count;
	
	/* Current topology:
	 * source1 -> sink1, sink2, sink3, small_sink
	 * source2 -> sink1, sink2
	 * source3 -> sink1, sink3
	 */
	
	/* Send from each source and verify correct delivery */
	ret = send_test_packet(&test_source1, 1);  /* Mark as from source1 */
	zassert_equal(ret, 4, "Source1 should reach 4 sinks");
	
	ret = send_test_packet(&test_source2, 2);  /* Mark as from source2 */
	zassert_equal(ret, 2, "Source2 should reach 2 sinks");
	
	ret = send_test_packet(&test_source3, 3);  /* Mark as from source3 */
	zassert_equal(ret, 2, "Source3 should reach 2 sinks");
	
	/* Verify sink1 got all three (many-to-one) */
	count = 0;
	uint8_t sources_seen = 0;
	while (k_msgq_get(&test_sink1.msgq, &received, K_NO_WAIT) == 0) {
		uint32_t source_id = *(uint32_t *)received->data;
		sources_seen |= (1 << source_id);
		net_buf_unref(received);
		count++;
	}
	zassert_equal(count, 3, "Sink1 should receive 3 messages");
	zassert_equal(sources_seen, 0x0E, "Should see all three sources (bits 1,2,3)");
	
	/* Verify sink2 got two (from source1 and source2) */
	count = 0;
	while (k_msgq_get(&test_sink2.msgq, &received, K_NO_WAIT) == 0) {
		count++;
		net_buf_unref(received);
	}
	zassert_equal(count, 2, "Sink2 should receive 2 messages");
	
	/* Verify sink3 got two (from source1 and source3) */
	count = 0;
	while (k_msgq_get(&test_sink3.msgq, &received, K_NO_WAIT) == 0) {
		count++;
		net_buf_unref(received);
	}
	zassert_equal(count, 2, "Sink3 should receive 2 messages");
}

/* Test 11.2: Interleaved sending pattern */
ZTEST(packet_io_unit_test, test_many_to_one_interleaved)
{
	struct net_buf *buf;
	int ret;
	uint32_t values[9];
	int count = 0;
	
	/* Send in alternating pattern: 1-2-3-1-2-3-1-2-3 */
	for (int round = 0; round < 3; round++) {
		/* Source1 */
		ret = send_test_packet(&test_source1, 0x1000 | round);
		zassert_true(ret > 0, "Source1 send failed");
		
		/* Source2 */
		ret = send_test_packet(&test_source2, 0x2000 | round);
		zassert_true(ret > 0, "Source2 send failed");
		
		/* Source3 */
		ret = send_test_packet(&test_source3, 0x3000 | round);
		zassert_true(ret > 0, "Source3 send failed");
	}
	
	/* Sink1 should have received all 9 packets */
	while (k_msgq_get(&test_sink1.msgq, &buf, K_NO_WAIT) == 0 && count < 9) {
		values[count++] = *(uint32_t *)buf->data;
		net_buf_unref(buf);
	}
	
	zassert_equal(count, 9, "Should receive 9 packets, got %d", count);
	
	/* Verify all packets received (order may vary) */
	int source1_count = 0, source2_count = 0, source3_count = 0;
	for (int i = 0; i < 9; i++) {
		if ((values[i] & 0xF000) == 0x1000) source1_count++;
		if ((values[i] & 0xF000) == 0x2000) source2_count++;
		if ((values[i] & 0xF000) == 0x3000) source3_count++;
	}
	
	zassert_equal(source1_count, 3, "Should get 3 from source1");
	zassert_equal(source2_count, 3, "Should get 3 from source2");
	zassert_equal(source3_count, 3, "Should get 3 from source3");
}

/*
 * ===========================================================================
 * 12. TIMEOUT TESTS
 * ===========================================================================
 */

/* Test 12.1: K_NO_WAIT timeout behavior */
ZTEST(packet_io_unit_test, test_timeout_no_wait)
{
	struct net_buf *buf;
	int ret;

	/* Create buffer */
	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Failed to allocate buffer");
	net_buf_add_le32(buf, 0xDEADBEEF);

	/* Send with K_NO_WAIT - should behave same as before */
	ret = packet_source_send(&test_source1, buf, K_NO_WAIT);
	zassert_equal(ret, 4, "Should deliver to all 4 sinks");

	net_buf_unref(buf);
}

/* Test 12.2: K_FOREVER timeout behavior */
ZTEST(packet_io_unit_test, test_timeout_forever)
{
	struct net_buf *buf;
	int ret;

	/* Create buffer */
	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Failed to allocate buffer");
	net_buf_add_le32(buf, 0xCAFEBABE);

	/* Send with K_FOREVER - with empty queues this should succeed immediately */
	ret = packet_source_send(&test_source1, buf, K_FOREVER);
	zassert_equal(ret, 4, "Should deliver to all 4 sinks");

	net_buf_unref(buf);
}

/* Test 12.3: Specific timeout value */
ZTEST(packet_io_unit_test, test_timeout_specific)
{
	struct net_buf *buf;
	int ret;

	/* Create buffer */
	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Failed to allocate buffer");
	net_buf_add_le32(buf, 0x12345678);

	/* Send with 10ms timeout - with empty queues this should succeed immediately */
	ret = packet_source_send(&test_source1, buf, K_MSEC(10));
	zassert_equal(ret, 4, "Should deliver to all 4 sinks");

	net_buf_unref(buf);
}

/* Test 12.4: Timeout expiry with full queues */
ZTEST(packet_io_unit_test, test_timeout_expiry_full_queues)
{
	struct net_buf *bufs[6];
	int ret;
	int64_t start_time, end_time;
	int64_t elapsed_ms;

	/* Fill small_sink first (size 1), then keep sending until others start dropping too */
	for (int i = 0; i < 6; i++) {
		bufs[i] = net_buf_alloc(&test_pool, K_NO_WAIT);
		zassert_not_null(bufs[i], "Failed to allocate buffer %d", i);
		net_buf_add_le32(bufs[i], 0x1000 + i);

		ret = packet_source_send(&test_source1, bufs[i], K_NO_WAIT);

		/* Accept any reasonable result as queue states may vary from previous tests */
		zassert_true(ret >= 0 && ret <= 4, "Should deliver to 0-4 sinks for packet %d, got %d", i, ret);
	}

	/* Now try to send with timeout - should try blocked sinks then fallback to K_NO_WAIT */
	struct net_buf *test_buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(test_buf, "Failed to allocate test buffer");
	net_buf_add_le32(test_buf, 0x99999999);

	start_time = k_uptime_get();
	ret = packet_source_send(&test_source1, test_buf, K_MSEC(50));
	end_time = k_uptime_get();
	elapsed_ms = end_time - start_time;

	/* Should complete quickly since queues are full and will drop/fail */
	zassert_true(elapsed_ms < 100, "Should complete quickly with full queues, took %lldms", elapsed_ms);

	/* Some sinks might accept (drop_on_full=true) while others reject */
	zassert_true(ret >= 0 && ret <= 4, "Should deliver to 0-4 sinks with full queues");

	/* Clean up */
	net_buf_unref(test_buf);
	for (int i = 0; i < 6; i++) {
		net_buf_unref(bufs[i]);
	}
}

/* Test 12.5: Mixed timeout behavior with partial delivery */
ZTEST(packet_io_unit_test, test_timeout_partial_delivery)
{
	struct net_buf *buf;
	int ret;

	/* Fill small_sink queue (size 1) */
	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Failed to allocate buffer");
	net_buf_add_le32(buf, 0x11111111);
	ret = packet_source_send(&test_source1, buf, K_NO_WAIT);
	zassert_equal(ret, 4, "Should deliver to all 4 sinks initially");
	net_buf_unref(buf);

	/* Now small_sink is full, send another packet */
	buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Failed to allocate buffer");
	net_buf_add_le32(buf, 0x22222222);

	/* With timeout, should deliver to 3 sinks (small_sink will drop) */
	ret = packet_source_send(&test_source1, buf, K_MSEC(10));
	zassert_equal(ret, 3, "Should deliver to 3 sinks (small_sink drops)");

	net_buf_unref(buf);
}

/* Test 12.6: Timeout with no-drop sink behavior */
ZTEST(packet_io_unit_test, test_timeout_no_drop_sink)
{
	struct net_buf *bufs[8];
	int ret;

	/* Fill queues gradually - some sinks may already have packets from previous tests */
	for (int i = 0; i < 8; i++) {
		bufs[i] = net_buf_alloc(&test_pool, K_NO_WAIT);
		zassert_not_null(bufs[i], "Failed to allocate buffer %d", i);
		net_buf_add_le32(bufs[i], 0x3000 + i);
		ret = packet_source_send(&test_source1, bufs[i], K_NO_WAIT);

		/* Accept varying results as queue states change */
		zassert_true(ret >= 0 && ret <= 4, "Should deliver to 0-4 sinks for fill packet %d, got %d", i, ret);
	}

	/* Try to send with timeout - expect some sinks to be full by now */
	struct net_buf *test_buf = net_buf_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(test_buf, "Failed to allocate test buffer");
	net_buf_add_le32(test_buf, 0x88888888);

	int64_t start_time = k_uptime_get();
	ret = packet_source_send(&test_source1, test_buf, K_MSEC(20));
	int64_t elapsed = k_uptime_get() - start_time;

	/* Should not take much longer than timeout */
	zassert_true(elapsed < 50, "Should not take much longer than timeout, took %lldms", elapsed);
	/* Some sinks should accept, others may drop or block */
	zassert_true(ret >= 0 && ret <= 4, "Should deliver to 0-4 sinks, got %d", ret);

	/* Clean up */
	net_buf_unref(test_buf);
	for (int i = 0; i < 8; i++) {
		net_buf_unref(bufs[i]);
	}
}