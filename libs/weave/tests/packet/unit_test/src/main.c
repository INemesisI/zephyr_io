/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/net/buf.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>
#include <weave/packet.h>

/* Test configuration constants */
#define TEST_POOL_SIZE    16
#define TEST_BUF_SIZE     64
#define TEST_QUEUE_SIZE   16
#define TEST_MAX_MESSAGES 50

/* Test packet IDs */
#define TEST_ID_SENSOR  0x10
#define TEST_ID_CONTROL 0x20
#define TEST_ID_STATUS  0x30

/* =============================================================================
 * Test Infrastructure - Buffer Pool and Captures
 * =============================================================================
 */

/* Define test buffer pool */
WEAVE_PACKET_POOL_DEFINE(test_pool, TEST_POOL_SIZE, TEST_BUF_SIZE, NULL);

/* Message queue for queued delivery */
WEAVE_MSGQ_DEFINE(test_queue, TEST_QUEUE_SIZE);

/* Capture context for tracking handler invocations */
struct packet_capture {
	atomic_t count;
	struct net_buf *last_buf;
	uint8_t last_packet_id;
	uint16_t last_counter;
};

static struct packet_capture captures[5] = {0};

static void reset_all_captures(void)
{
	ARRAY_FOR_EACH_PTR(captures, capture) {
		atomic_clear(&capture->count);
		capture->last_buf = NULL;
		capture->last_packet_id = 0;
		capture->last_counter = 0;
	}
}

/* Packet handler - captures delivery info */
static inline void packet_capture_handler(struct net_buf *buf, void *user_data)
{
	struct packet_capture *capture = user_data;

	zassert_not_null(capture, "Capture context should not be NULL");
	zassert_not_null(buf, "Buffer should not be NULL");

	atomic_inc(&capture->count);
	capture->last_buf = buf;

	/* Extract metadata */
	uint8_t packet_id;
	uint16_t counter;

	if (weave_packet_get_id(buf, &packet_id) == 0) {
		capture->last_packet_id = packet_id;
	}
	if (weave_packet_get_counter(buf, &counter) == 0) {
		capture->last_counter = counter;
	}
}

/* =============================================================================
 * Test Sources and Sinks
 * =============================================================================
 */

/* Basic packet source */
WEAVE_PACKET_SOURCE_DEFINE(basic_source);

/* Basic sinks */
WEAVE_PACKET_SINK_DEFINE(sink_immediate, packet_capture_handler, WV_IMMEDIATE, WV_NO_FILTER,
			 &captures[4]);
WEAVE_PACKET_SINK_DEFINE(sink_queued, packet_capture_handler, &test_queue, WV_NO_FILTER,
			 &captures[4]);

/* Connect basic source to sinks */
WEAVE_CONNECT(&basic_source, &sink_immediate);
WEAVE_CONNECT(&basic_source, &sink_queued);

/* =============================================================================
 * Filtered Sinks Setup - For ID filtering tests
 * =============================================================================
 */

/* Source for filtered tests */
WEAVE_PACKET_SOURCE_DEFINE(filtered_source);

/* Sinks with different ID filters */
WEAVE_PACKET_SINK_DEFINE(sink_filter_any, packet_capture_handler, WV_IMMEDIATE, WV_NO_FILTER,
			 &captures[0]);
WEAVE_PACKET_SINK_DEFINE(sink_filter_sensor, packet_capture_handler, WV_IMMEDIATE, TEST_ID_SENSOR,
			 &captures[1]);
WEAVE_PACKET_SINK_DEFINE(sink_filter_control, packet_capture_handler, WV_IMMEDIATE, TEST_ID_CONTROL,
			 &captures[2]);
WEAVE_PACKET_SINK_DEFINE(sink_filter_status, packet_capture_handler, WV_IMMEDIATE, TEST_ID_STATUS,
			 &captures[3]);

/* Connect filtered source to all filtered sinks */
WEAVE_CONNECT(&filtered_source, &sink_filter_any);
WEAVE_CONNECT(&filtered_source, &sink_filter_sensor);
WEAVE_CONNECT(&filtered_source, &sink_filter_control);
WEAVE_CONNECT(&filtered_source, &sink_filter_status);

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

static size_t pool_num_free(struct net_buf_pool *pool)
{
	return sys_sflist_len(&pool->free._queue.data_q) + pool->uninit_count;
}

/* =============================================================================
 * Test Setup/Teardown
 * =============================================================================
 */

static void test_setup(void *fixture)
{
	ARG_UNUSED(fixture);

	reset_all_captures();
	k_msgq_purge(&test_queue);
}

static void test_teardown(void *fixture)
{
	ARG_UNUSED(fixture);

	process_all_messages();

	/* Verify queue is empty */
	zassert_equal(k_msgq_num_used_get(&test_queue), 0, "Queue should be empty");

	/* Verify no buffer leaks */
	zassert_equal(pool_num_free(test_pool.pool), TEST_POOL_SIZE, "All buffers should be freed");
}

ZTEST_SUITE(weave_packet_unit_test, NULL, NULL, test_setup, test_teardown, NULL);

/* =============================================================================
 * Buffer Allocation Tests
 * =============================================================================
 */

ZTEST(weave_packet_unit_test, test_packet_alloc_basic)
{
	struct net_buf *buf;
	uint8_t packet_id;

	buf = weave_packet_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Allocation should succeed");
	zassert_equal(buf->ref, 1, "Initial ref should be 1");

	/* Default packet ID should be ANY */
	int ret = weave_packet_get_id(buf, &packet_id);
	zassert_equal(ret, 0, "Get ID should succeed");
	zassert_equal(packet_id, WEAVE_PACKET_ID_ANY, "Default ID should be ANY");

	net_buf_unref(buf);
}

ZTEST(weave_packet_unit_test, test_packet_alloc_with_id)
{
	struct net_buf *buf;
	uint8_t packet_id;

	buf = weave_packet_alloc_with_id(&test_pool, TEST_ID_SENSOR, K_NO_WAIT);
	zassert_not_null(buf, "Allocation should succeed");

	int ret = weave_packet_get_id(buf, &packet_id);
	zassert_equal(ret, 0, "Get ID should succeed");
	zassert_equal(packet_id, TEST_ID_SENSOR, "ID should be TEST_ID_SENSOR");

	net_buf_unref(buf);
}

ZTEST(weave_packet_unit_test, test_packet_alloc_null_pool)
{
	struct net_buf *buf;

	/* Test NULL pool */
	buf = weave_packet_alloc(NULL, K_NO_WAIT);
	zassert_is_null(buf, "Should return NULL for NULL pool");

	buf = weave_packet_alloc_with_id(NULL, TEST_ID_SENSOR, K_NO_WAIT);
	zassert_is_null(buf, "Should return NULL for NULL pool");
}

ZTEST(weave_packet_unit_test, test_packet_alloc_null_pool_ptr)
{
	struct net_buf *buf;

	/* Create pool with NULL pool->pool */
	struct weave_packet_pool bad_pool = {
		.pool = NULL,
		.counter = ATOMIC_INIT(0),
	};

	buf = weave_packet_alloc(&bad_pool, K_NO_WAIT);
	zassert_is_null(buf, "Should return NULL for pool with NULL pool->pool");

	buf = weave_packet_alloc_with_id(&bad_pool, TEST_ID_SENSOR, K_NO_WAIT);
	zassert_is_null(buf, "Should return NULL for pool with NULL pool->pool");
}

ZTEST(weave_packet_unit_test, test_packet_counter_increments)
{
	struct net_buf *buf1, *buf2, *buf3;
	uint16_t counter1, counter2, counter3;

	buf1 = weave_packet_alloc(&test_pool, K_NO_WAIT);
	buf2 = weave_packet_alloc(&test_pool, K_NO_WAIT);
	buf3 = weave_packet_alloc(&test_pool, K_NO_WAIT);

	zassert_not_null(buf1, "Alloc 1 should succeed");
	zassert_not_null(buf2, "Alloc 2 should succeed");
	zassert_not_null(buf3, "Alloc 3 should succeed");

	weave_packet_get_counter(buf1, &counter1);
	weave_packet_get_counter(buf2, &counter2);
	weave_packet_get_counter(buf3, &counter3);

	/* Counters should be sequential */
	zassert_equal(counter2, counter1 + 1, "Counter should increment");
	zassert_equal(counter3, counter2 + 1, "Counter should increment");

	net_buf_unref(buf1);
	net_buf_unref(buf2);
	net_buf_unref(buf3);
}

/* =============================================================================
 * Metadata API Tests
 * =============================================================================
 */

ZTEST(weave_packet_unit_test, test_packet_get_set_id)
{
	struct net_buf *buf;
	uint8_t packet_id;
	int ret;

	buf = weave_packet_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Alloc should succeed");

	/* Set ID */
	ret = weave_packet_set_id(buf, 0x42);
	zassert_equal(ret, 0, "Set ID should succeed");

	/* Get ID */
	ret = weave_packet_get_id(buf, &packet_id);
	zassert_equal(ret, 0, "Get ID should succeed");
	zassert_equal(packet_id, 0x42, "ID should be 0x42");

	net_buf_unref(buf);
}

ZTEST(weave_packet_unit_test, test_packet_get_set_client_id)
{
	struct net_buf *buf;
	uint8_t client_id;
	int ret;

	buf = weave_packet_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Alloc should succeed");

	/* Initial client_id should be 0 */
	ret = weave_packet_get_client_id(buf, &client_id);
	zassert_equal(ret, 0, "Get client_id should succeed");
	zassert_equal(client_id, 0, "Initial client_id should be 0");

	/* Set client_id */
	ret = weave_packet_set_client_id(buf, 0xAB);
	zassert_equal(ret, 0, "Set client_id should succeed");

	ret = weave_packet_get_client_id(buf, &client_id);
	zassert_equal(ret, 0, "Get client_id should succeed");
	zassert_equal(client_id, 0xAB, "client_id should be 0xAB");

	net_buf_unref(buf);
}

ZTEST(weave_packet_unit_test, test_packet_get_set_counter)
{
	struct net_buf *buf;
	uint16_t counter;
	int ret;

	buf = weave_packet_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Alloc should succeed");

	/* Get initial counter (set during alloc) */
	ret = weave_packet_get_counter(buf, &counter);
	zassert_equal(ret, 0, "Get counter should succeed");

	/* Set counter to specific value */
	ret = weave_packet_set_counter(buf, 0x1234);
	zassert_equal(ret, 0, "Set counter should succeed");

	ret = weave_packet_get_counter(buf, &counter);
	zassert_equal(ret, 0, "Get counter should succeed");
	zassert_equal(counter, 0x1234, "Counter should be 0x1234");

	net_buf_unref(buf);
}

ZTEST(weave_packet_unit_test, test_packet_timestamp)
{
	struct net_buf *buf;
	uint32_t ticks1, ticks2;
	int ret;

	buf = weave_packet_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Alloc should succeed");

	/* Get initial timestamp - may be 0 at boot, just verify API works */
	ret = weave_packet_get_timestamp_ticks(buf, &ticks1);
	zassert_equal(ret, 0, "Get timestamp should succeed");

	/* Wait a bit and update */
	k_msleep(1);
	ret = weave_packet_update_timestamp(buf);
	zassert_equal(ret, 0, "Update timestamp should succeed");

	ret = weave_packet_get_timestamp_ticks(buf, &ticks2);
	zassert_equal(ret, 0, "Get timestamp should succeed");
	zassert_true(ticks2 >= ticks1, "Updated timestamp should be >= original");

	/* Test set timestamp */
	ret = weave_packet_set_timestamp_ticks(buf, 0x12345678);
	zassert_equal(ret, 0, "Set timestamp should succeed");

	ret = weave_packet_get_timestamp_ticks(buf, &ticks1);
	zassert_equal(ret, 0, "Get timestamp should succeed");
	zassert_equal(ticks1, 0x12345678, "Timestamp should match set value");

	net_buf_unref(buf);
}

ZTEST(weave_packet_unit_test, test_packet_metadata_null_buf)
{
	uint8_t u8_val;
	uint16_t u16_val;
	uint32_t u32_val;
	int ret;

	/* All getters should fail with NULL buffer */
	ret = weave_packet_get_id(NULL, &u8_val);
	zassert_equal(ret, -EINVAL, "Get ID with NULL buf should fail");

	ret = weave_packet_get_client_id(NULL, &u8_val);
	zassert_equal(ret, -EINVAL, "Get client_id with NULL buf should fail");

	ret = weave_packet_get_counter(NULL, &u16_val);
	zassert_equal(ret, -EINVAL, "Get counter with NULL buf should fail");

	ret = weave_packet_get_timestamp_ticks(NULL, &u32_val);
	zassert_equal(ret, -EINVAL, "Get timestamp with NULL buf should fail");

	/* All setters should fail with NULL buffer */
	ret = weave_packet_set_id(NULL, 0x42);
	zassert_equal(ret, -EINVAL, "Set ID with NULL buf should fail");

	ret = weave_packet_set_client_id(NULL, 0x42);
	zassert_equal(ret, -EINVAL, "Set client_id with NULL buf should fail");

	ret = weave_packet_set_counter(NULL, 0x1234);
	zassert_equal(ret, -EINVAL, "Set counter with NULL buf should fail");

	ret = weave_packet_set_timestamp_ticks(NULL, 0x12345678);
	zassert_equal(ret, -EINVAL, "Set timestamp with NULL buf should fail");

	ret = weave_packet_update_timestamp(NULL);
	zassert_equal(ret, -EINVAL, "Update timestamp with NULL buf should fail");
}

ZTEST(weave_packet_unit_test, test_packet_metadata_null_output)
{
	struct net_buf *buf;
	int ret;

	buf = weave_packet_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Alloc should succeed");

	/* Getters with NULL output pointer should fail */
	ret = weave_packet_get_id(buf, NULL);
	zassert_equal(ret, -EINVAL, "Get ID with NULL output should fail");

	ret = weave_packet_get_client_id(buf, NULL);
	zassert_equal(ret, -EINVAL, "Get client_id with NULL output should fail");

	ret = weave_packet_get_counter(buf, NULL);
	zassert_equal(ret, -EINVAL, "Get counter with NULL output should fail");

	ret = weave_packet_get_timestamp_ticks(buf, NULL);
	zassert_equal(ret, -EINVAL, "Get timestamp with NULL output should fail");

	net_buf_unref(buf);
}

/* =============================================================================
 * ID Filtering Tests
 * =============================================================================
 */

ZTEST(weave_packet_unit_test, test_filter_sink_any_accepts_all)
{
	struct net_buf *buf;
	int ret;

	/* Create source with only sink_filter_any connected */
	struct weave_source source = WEAVE_SOURCE_INITIALIZER(test, &weave_packet_ops);
	static struct weave_connection conn;

	conn.source = &source;
	conn.sink = &sink_filter_any;
	sys_slist_append(&source.sinks, &conn.node);

	/* Send packet with specific ID */
	buf = weave_packet_alloc_with_id(&test_pool, TEST_ID_SENSOR, K_NO_WAIT);
	zassert_not_null(buf, "Alloc should succeed");

	ret = weave_packet_send(&source, buf, K_NO_WAIT);
	zassert_equal(ret, 1, "Should deliver to 1 sink");
	zassert_equal(atomic_get(&captures[0].count), 1, "sink_any should receive");

	reset_all_captures();

	/* Send packet with ANY ID */
	buf = weave_packet_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Alloc should succeed");

	ret = weave_packet_send(&source, buf, K_NO_WAIT);
	zassert_equal(ret, 1, "Should deliver to 1 sink");
	zassert_equal(atomic_get(&captures[0].count), 1, "sink_any should receive");
}

ZTEST(weave_packet_unit_test, test_filter_sink_accepts_match)
{
	struct net_buf *buf;
	int ret;

	/* Send packet with TEST_ID_SENSOR to filtered_source */
	buf = weave_packet_alloc_with_id(&test_pool, TEST_ID_SENSOR, K_NO_WAIT);
	zassert_not_null(buf, "Alloc should succeed");

	ret = weave_packet_send(&filtered_source, buf, K_NO_WAIT);

	/* Should reach: sink_filter_any (accepts all) + sink_filter_sensor (matching ID) */
	zassert_equal(ret, 2, "Should deliver to 2 sinks (any + matching)");
	zassert_equal(atomic_get(&captures[0].count), 1, "sink_any should receive");
	zassert_equal(atomic_get(&captures[1].count), 1, "sink_sensor should receive");
	zassert_equal(atomic_get(&captures[2].count), 0, "sink_control should NOT receive");
	zassert_equal(atomic_get(&captures[3].count), 0, "sink_status should NOT receive");
}

ZTEST(weave_packet_unit_test, test_filter_sink_rejects_mismatch)
{
	struct net_buf *buf;
	int ret;

	/* Send packet with TEST_ID_CONTROL */
	buf = weave_packet_alloc_with_id(&test_pool, TEST_ID_CONTROL, K_NO_WAIT);
	zassert_not_null(buf, "Alloc should succeed");

	ret = weave_packet_send(&filtered_source, buf, K_NO_WAIT);

	/* Should reach: sink_filter_any + sink_filter_control */
	zassert_equal(ret, 2, "Should deliver to 2 sinks");
	zassert_equal(atomic_get(&captures[0].count), 1, "sink_any should receive");
	zassert_equal(atomic_get(&captures[1].count), 0, "sink_sensor should NOT receive");
	zassert_equal(atomic_get(&captures[2].count), 1, "sink_control should receive");
	zassert_equal(atomic_get(&captures[3].count), 0, "sink_status should NOT receive");
}

ZTEST(weave_packet_unit_test, test_filter_packet_any_passes_all)
{
	struct net_buf *buf;
	int ret;

	/* Send packet with ANY ID - should pass ALL filters */
	buf = weave_packet_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Alloc should succeed");

	/* Verify it has ANY ID */
	uint8_t id;
	weave_packet_get_id(buf, &id);
	zassert_equal(id, WEAVE_PACKET_ID_ANY, "Should have ANY ID");

	ret = weave_packet_send(&filtered_source, buf, K_NO_WAIT);

	/* Packet with ANY ID should reach ALL sinks */
	zassert_equal(ret, 4, "Should deliver to all 4 sinks");
	zassert_equal(atomic_get(&captures[0].count), 1, "sink_any should receive");
	zassert_equal(atomic_get(&captures[1].count), 1, "sink_sensor should receive");
	zassert_equal(atomic_get(&captures[2].count), 1, "sink_control should receive");
	zassert_equal(atomic_get(&captures[3].count), 1, "sink_status should receive");
}

ZTEST(weave_packet_unit_test, test_filter_multiple_packets)
{
	struct net_buf *buf;

	/* Send sensor packet */
	buf = weave_packet_alloc_with_id(&test_pool, TEST_ID_SENSOR, K_NO_WAIT);
	weave_packet_send(&filtered_source, buf, K_NO_WAIT);

	/* Send control packet */
	buf = weave_packet_alloc_with_id(&test_pool, TEST_ID_CONTROL, K_NO_WAIT);
	weave_packet_send(&filtered_source, buf, K_NO_WAIT);

	/* Send status packet */
	buf = weave_packet_alloc_with_id(&test_pool, TEST_ID_STATUS, K_NO_WAIT);
	weave_packet_send(&filtered_source, buf, K_NO_WAIT);

	/* sink_any should have received all 3 */
	zassert_equal(atomic_get(&captures[0].count), 3, "sink_any should receive all");

	/* Each filtered sink should have received exactly 1 */
	zassert_equal(atomic_get(&captures[1].count), 1, "sink_sensor should receive 1");
	zassert_equal(atomic_get(&captures[2].count), 1, "sink_control should receive 1");
	zassert_equal(atomic_get(&captures[3].count), 1, "sink_status should receive 1");
}

/* =============================================================================
 * Send Function Tests
 * =============================================================================
 */

ZTEST(weave_packet_unit_test, test_packet_send_consumes_ref)
{
	struct net_buf *buf;
	int ret;

	buf = weave_packet_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Alloc should succeed");
	zassert_equal(buf->ref, 1, "Initial ref should be 1");

	/* weave_packet_send should consume caller's reference */
	ret = weave_packet_send(&basic_source, buf, K_NO_WAIT);
	zassert_equal(ret, 2, "Should deliver to 2 sinks");

	/* After send + processing, buffer should be freed (ref was consumed) */
	process_all_messages();

	/* Pool should be back to full */
	zassert_equal(pool_num_free(test_pool.pool), TEST_POOL_SIZE, "Buffer should be freed");
}

ZTEST(weave_packet_unit_test, test_packet_send_ref_preserves)
{
	struct net_buf *buf;
	int ret;

	buf = weave_packet_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Alloc should succeed");
	zassert_equal(buf->ref, 1, "Initial ref should be 1");

	/* weave_packet_send_ref should preserve caller's reference */
	ret = weave_packet_send_ref(&basic_source, buf, K_NO_WAIT);
	zassert_equal(ret, 2, "Should deliver to 2 sinks");

	/* Caller still has reference */
	zassert_true(buf->ref >= 1, "Caller should still have reference");

	process_all_messages();

	/* Caller still has their reference */
	zassert_equal(buf->ref, 1, "Caller should have exactly 1 reference");

	/* Now release it */
	net_buf_unref(buf);
}

ZTEST(weave_packet_unit_test, test_packet_send_to_multiple_sinks)
{
	struct net_buf *buf;
	int ret;

	buf = weave_packet_alloc(&test_pool, K_NO_WAIT);
	zassert_not_null(buf, "Alloc should succeed");

	/* basic_source has 2 sinks: 1 immediate + 1 queued */
	ret = weave_packet_send_ref(&basic_source, buf, K_NO_WAIT);
	zassert_equal(ret, 2, "Should deliver to 2 sinks");

	/* After immediate delivery, ref should be: 1 (caller) + 1 (queued) = 2 */
	/* Actually: ref was incremented for each sink, then unref for immediate */
	/* So: 1 (caller) + 2 (sinks) - 1 (immediate unref) = 2 */
	zassert_equal(buf->ref, 2, "Ref should be 2 (caller + queued)");

	/* Immediate sink should have received */
	zassert_equal(atomic_get(&captures[4].count), 1, "Immediate sink should receive");

	/* Process queued */
	process_all_messages();

	/* Now only caller has reference */
	zassert_equal(buf->ref, 1, "Only caller reference remains");
	zassert_equal(atomic_get(&captures[4].count), 2, "Both sinks should have received");

	net_buf_unref(buf);
}

/* =============================================================================
 * Integration Tests
 * =============================================================================
 */

ZTEST(weave_packet_unit_test, test_packet_full_lifecycle)
{
	struct net_buf *buf;
	uint16_t counter;
	int ret;

	/* Allocate with specific ID */
	buf = weave_packet_alloc_with_id(&test_pool, TEST_ID_SENSOR, K_NO_WAIT);
	zassert_not_null(buf, "Alloc should succeed");

	/* Set metadata */
	weave_packet_set_client_id(buf, 0x55);
	weave_packet_get_counter(buf, &counter);

	/* Add some data */
	net_buf_add_mem(buf, "Hello", 5);

	/* Send to filtered source */
	ret = weave_packet_send(&filtered_source, buf, K_NO_WAIT);
	zassert_equal(ret, 2, "Should deliver to sink_any + sink_sensor");

	/* Verify captures */
	zassert_equal(atomic_get(&captures[0].count), 1, "sink_any received");
	zassert_equal(atomic_get(&captures[1].count), 1, "sink_sensor received");
	zassert_equal(captures[1].last_packet_id, TEST_ID_SENSOR, "Correct packet ID");
	zassert_equal(captures[1].last_counter, counter, "Correct counter");
}

ZTEST(weave_packet_unit_test, test_packet_many_sends)
{
	const int num_packets = 10;
	struct net_buf *buf;
	int ret;

	for (int i = 0; i < num_packets; i++) {
		buf = weave_packet_alloc(&test_pool, K_NO_WAIT);
		zassert_not_null(buf, "Alloc %d should succeed", i);

		ret = weave_packet_send(&basic_source, buf, K_NO_WAIT);
		zassert_equal(ret, 2, "Send %d should succeed", i);
	}

	process_all_messages();

	/* Both sinks should have received all packets */
	zassert_equal(atomic_get(&captures[4].count), num_packets * 2,
		      "Should receive all packets on both sinks");

	/* All buffers should be freed */
	zassert_equal(pool_num_free(test_pool.pool), TEST_POOL_SIZE, "All buffers freed");
}

ZTEST(weave_packet_unit_test, test_packet_no_buffer_leaks)
{
	const int iterations = TEST_POOL_SIZE * 2;
	struct net_buf *buf;

	for (int i = 0; i < iterations; i++) {
		buf = weave_packet_alloc(&test_pool, K_NO_WAIT);
		zassert_not_null(buf, "Alloc %d should succeed", i);

		weave_packet_send(&basic_source, buf, K_NO_WAIT);
		process_all_messages();
	}

	/* All buffers should be back in pool */
	zassert_equal(pool_num_free(test_pool.pool), TEST_POOL_SIZE,
		      "No buffer leaks after %d iterations", iterations);
}

/* =============================================================================
 * Pool Exhaustion Tests
 * =============================================================================
 */

ZTEST(weave_packet_unit_test, test_packet_alloc_pool_exhaustion)
{
	struct net_buf *bufs[TEST_POOL_SIZE];
	struct net_buf *extra;

	/* Allocate all buffers from pool */
	for (int i = 0; i < TEST_POOL_SIZE; i++) {
		bufs[i] = weave_packet_alloc(&test_pool, K_NO_WAIT);
		zassert_not_null(bufs[i], "Alloc %d should succeed", i);
	}

	/* Pool should be exhausted */
	zassert_equal(pool_num_free(test_pool.pool), 0, "Pool should be empty");

	/* Next allocation should fail (return NULL) */
	extra = weave_packet_alloc(&test_pool, K_NO_WAIT);
	zassert_is_null(extra, "Alloc should fail when pool exhausted");

	/* Also test alloc_with_id variant */
	extra = weave_packet_alloc_with_id(&test_pool, TEST_ID_SENSOR, K_NO_WAIT);
	zassert_is_null(extra, "Alloc with ID should fail when pool exhausted");

	/* Free all buffers */
	for (int i = 0; i < TEST_POOL_SIZE; i++) {
		net_buf_unref(bufs[i]);
	}
}

/* =============================================================================
 * Edge Case Tests - Raw Sink Without Context
 * =============================================================================
 */

/* Handler for raw sink test */
static atomic_t raw_sink_count = ATOMIC_INIT(0);

static void raw_sink_handler(void *ptr, void *user_data)
{
	/* user_data should be NULL */
	zassert_is_null(user_data, "Raw sink should have NULL user_data");
	zassert_not_null(ptr, "Buffer should not be NULL");

	atomic_inc(&raw_sink_count);
	net_buf_unref((struct net_buf *)ptr);
}

ZTEST(weave_packet_unit_test, test_packet_sink_null_context)
{
	struct net_buf *buf;
	int ret;

	/* Create a raw sink with NULL user_data but using weave_packet_ops */
	/* This tests the ctx == NULL branch in packet_buf_ref */
	struct weave_sink raw_sink =
		WEAVE_SINK_INITIALIZER(raw_sink_handler, WV_IMMEDIATE, NULL, &weave_packet_ops);

	/* Create source and connect */
	struct weave_source source = WEAVE_SOURCE_INITIALIZER(raw_test, &weave_packet_ops);
	static struct weave_connection conn;

	conn.source = &source;
	conn.sink = &raw_sink;
	sys_slist_append(&source.sinks, &conn.node);

	atomic_clear(&raw_sink_count);

	/* Send packet - should work even with NULL ctx (no filtering) */
	buf = weave_packet_alloc_with_id(&test_pool, TEST_ID_SENSOR, K_NO_WAIT);
	zassert_not_null(buf, "Alloc should succeed");

	ret = weave_source_emit(&source, buf, K_NO_WAIT);
	zassert_equal(ret, 1, "Should deliver to 1 sink");
	zassert_equal(atomic_get(&raw_sink_count), 1, "Raw sink should receive packet");

	/* Caller's reference */
	net_buf_unref(buf);
}

/* =============================================================================
 * Wrong Usage Tests - Buffers Without Proper Metadata
 * =============================================================================
 */

/*
 * Define a net_buf pool with NO user_data space.
 * This simulates "wrong usage" where someone creates a pool without
 * weave_packet metadata space.
 */
NET_BUF_POOL_DEFINE(no_metadata_pool, 4, TEST_BUF_SIZE, 0, NULL);

/* Handler for no-metadata buffer test */
static atomic_t no_meta_sink_count = ATOMIC_INIT(0);

static void no_meta_sink_handler(void *ptr, void *user_data)
{
	ARG_UNUSED(user_data);
	zassert_not_null(ptr, "Buffer should not be NULL");
	atomic_inc(&no_meta_sink_count);
	net_buf_unref((struct net_buf *)ptr);
}

ZTEST(weave_packet_unit_test, test_buffer_without_metadata_space)
{
	struct net_buf *buf;
	int ret;

	/* Test that buffers without metadata space are handled gracefully */

	/* Create a filtered sink */
	static struct weave_packet_sink_ctx ctx = {
		.filter = TEST_ID_SENSOR,
		.user_data = NULL,
	};
	struct weave_sink filtered_sink =
		WEAVE_SINK_INITIALIZER(no_meta_sink_handler, WV_IMMEDIATE, &ctx, &weave_packet_ops);

	/* Create source and connect */
	struct weave_source source = WEAVE_SOURCE_INITIALIZER(no_meta_test, &weave_packet_ops);
	static struct weave_connection conn;

	conn.source = &source;
	conn.sink = &filtered_sink;
	sys_slist_append(&source.sinks, &conn.node);

	atomic_clear(&no_meta_sink_count);

	/* Allocate buffer from pool WITHOUT metadata space */
	buf = net_buf_alloc(&no_metadata_pool, K_NO_WAIT);
	zassert_not_null(buf, "Alloc should succeed");
	zassert_equal(buf->user_data_size, 0, "Should have no user_data space");

	/* weave_packet_get_meta should return NULL for this buffer */
	struct weave_packet_metadata *meta = weave_packet_get_meta(buf);
	zassert_is_null(meta, "Meta should be NULL for buffer without metadata space");

	/* Try to emit through weave - should still work (filter skipped) */
	ret = weave_source_emit(&source, buf, K_NO_WAIT);
	zassert_equal(ret, 1, "Should deliver to 1 sink (filter skipped when meta is NULL)");
	zassert_equal(atomic_get(&no_meta_sink_count), 1, "Sink should receive packet");

	/* Caller's reference */
	net_buf_unref(buf);
}

ZTEST(weave_packet_unit_test, test_direct_net_buf_alloc_uninitialized)
{
	struct net_buf *buf;
	uint8_t packet_id;
	int ret;

	/*
	 * Test buffers allocated via net_buf_alloc() instead of weave_packet_alloc().
	 * These have metadata space but uninitialized (all-zeros) metadata.
	 */

	buf = net_buf_alloc(test_pool.pool, K_NO_WAIT);
	zassert_not_null(buf, "Direct alloc should succeed");
	zassert_true(buf->user_data_size >= WEAVE_PACKET_METADATA_SIZE,
		     "Buffer has metadata space");

	/* Uninitialized metadata (all zeros) should be detected as invalid */
	struct weave_packet_metadata *meta = weave_packet_get_meta(buf);
	zassert_is_null(meta, "Meta should be NULL for uninitialized buffer");

	/* Metadata accessors should fail */
	ret = weave_packet_get_id(buf, &packet_id);
	zassert_equal(ret, -EINVAL, "Get ID should fail for uninitialized buffer");

	/* Verify the raw bytes are zeroed (Zephyr behavior) */
	uint8_t *raw_bytes = (uint8_t *)net_buf_user_data(buf);
	bool all_zero = true;
	for (size_t i = 0; i < sizeof(struct weave_packet_metadata); i++) {
		if (raw_bytes[i] != 0) {
			all_zero = false;
			break;
		}
	}
	zassert_true(all_zero, "user_data should be zeroed by Zephyr");

	/* Clean up */
	net_buf_unref(buf);
}

ZTEST(weave_packet_unit_test, test_metadata_api_with_no_metadata_buffer)
{
	struct net_buf *buf;
	uint8_t u8_val;
	uint16_t u16_val;
	uint32_t u32_val;
	int ret;

	/*
	 * Test that all metadata accessor functions handle buffers
	 * without proper metadata space gracefully.
	 */

	buf = net_buf_alloc(&no_metadata_pool, K_NO_WAIT);
	zassert_not_null(buf, "Alloc should succeed");
	zassert_equal(buf->user_data_size, 0, "No metadata space");

	/* All getters should return -EINVAL */
	ret = weave_packet_get_id(buf, &u8_val);
	zassert_equal(ret, -EINVAL, "Get ID should fail with no metadata");

	ret = weave_packet_get_client_id(buf, &u8_val);
	zassert_equal(ret, -EINVAL, "Get client_id should fail with no metadata");

	ret = weave_packet_get_counter(buf, &u16_val);
	zassert_equal(ret, -EINVAL, "Get counter should fail with no metadata");

	ret = weave_packet_get_timestamp_ticks(buf, &u32_val);
	zassert_equal(ret, -EINVAL, "Get timestamp should fail with no metadata");

	/* All setters should return -EINVAL */
	ret = weave_packet_set_id(buf, 0x42);
	zassert_equal(ret, -EINVAL, "Set ID should fail with no metadata");

	ret = weave_packet_set_client_id(buf, 0x42);
	zassert_equal(ret, -EINVAL, "Set client_id should fail with no metadata");

	ret = weave_packet_set_counter(buf, 0x1234);
	zassert_equal(ret, -EINVAL, "Set counter should fail with no metadata");

	ret = weave_packet_set_timestamp_ticks(buf, 0x12345678);
	zassert_equal(ret, -EINVAL, "Set timestamp should fail with no metadata");

	ret = weave_packet_update_timestamp(buf);
	zassert_equal(ret, -EINVAL, "Update timestamp should fail with no metadata");

	net_buf_unref(buf);
}

ZTEST(weave_packet_unit_test, test_alloc_from_pool_without_metadata_space)
{
	struct net_buf *buf;

	/* Test weave_packet_alloc with a pool that has no metadata space */
	struct weave_packet_pool bad_pool = {
		.pool = &no_metadata_pool,
		.counter = ATOMIC_INIT(0),
	};

	/* Allocate - should succeed but skip metadata initialization */
	buf = weave_packet_alloc_with_id(&bad_pool, TEST_ID_SENSOR, K_NO_WAIT);
	zassert_not_null(buf, "Alloc should succeed");

	/* Buffer has no metadata space */
	zassert_equal(buf->user_data_size, 0, "Should have no user_data space");

	/* Meta should be NULL */
	struct weave_packet_metadata *meta = weave_packet_get_meta(buf);
	zassert_is_null(meta, "Meta should be NULL");

	/* Also test the basic alloc variant */
	net_buf_unref(buf);

	buf = weave_packet_alloc(&bad_pool, K_NO_WAIT);
	zassert_not_null(buf, "Basic alloc should succeed");
	zassert_is_null(weave_packet_get_meta(buf), "Meta should be NULL");

	net_buf_unref(buf);
}
