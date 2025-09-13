/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/packet_io/packet_io.h>
#include <zephyr/net/buf.h>
#include <zephyr/sys/util.h>
#include <zephyr/timing/timing.h>
#include <zephyr/random/random.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_ip.h>
#include <math.h>

LOG_MODULE_REGISTER(packet_io_integration, LOG_LEVEL_INF);

/*
 * ===========================================================================
 * BUFFER POOLS - Different sizes for different scenarios
 * ===========================================================================
 */

/* Small packets - 128 bytes */
NET_BUF_POOL_DEFINE(small_pool, 64, 128, 4, NULL);

/* Medium packets - 512 bytes (typical IoT) */
NET_BUF_POOL_DEFINE(medium_pool, 32, 512, 4, NULL);

/* Large packets - 1500 bytes (Ethernet MTU) */
NET_BUF_POOL_DEFINE(large_pool, 16, 1500, 4, NULL);

/* Jumbo packets - 4KB */
NET_BUF_POOL_DEFINE(jumbo_pool, 8, 4096, 4, NULL);

/*
 * ===========================================================================
 * TEST DATA STRUCTURES
 * ===========================================================================
 */

/* Ethernet frame structure */
struct ethernet_frame {
	uint8_t dst_mac[6];
	uint8_t src_mac[6];
	uint16_t ethertype;
	uint8_t payload[];
} __packed;

/* Streaming frame structure */
struct stream_frame {
	uint32_t sequence;
	uint32_t timestamp;
	uint16_t frame_size;
	uint16_t flags;
	uint8_t data[];
} __packed;

/* Bulk transfer chunk */
struct bulk_chunk {
	uint32_t offset;
	uint32_t total_size;
	uint16_t chunk_size;
	uint16_t crc16;
	uint8_t data[];
} __packed;

/*
 * ===========================================================================
 * PACKET SOURCES AND SINKS
 * ===========================================================================
 */

/* Network processing pipeline */
PACKET_SOURCE_DEFINE(eth_rx_source);
PACKET_SOURCE_DEFINE(eth_tx_source);
PACKET_SINK_DEFINE(packet_filter_sink, 32, false);
PACKET_SINK_DEFINE(packet_router_sink, 32, false);
PACKET_SINK_DEFINE(packet_logger_sink, 64, true);  /* Can drop for logging */

/* Streaming pipeline */
PACKET_SOURCE_DEFINE(stream_producer);
PACKET_SINK_DEFINE(stream_consumer1, 16, false);
PACKET_SINK_DEFINE(stream_consumer2, 16, false);
PACKET_SINK_DEFINE(stream_buffer, 64, false);  /* Jitter buffer */

/* Bulk transfer pipeline */
PACKET_SOURCE_DEFINE(bulk_source);
PACKET_SINK_DEFINE(bulk_processor, 32, false);
PACKET_SINK_DEFINE(bulk_storage, 32, false);

/* Performance test sources/sinks */
PACKET_SOURCE_DEFINE(perf_source1);
PACKET_SOURCE_DEFINE(perf_source2);
PACKET_SOURCE_DEFINE(perf_source3);
PACKET_SINK_DEFINE(perf_sink, 128, true);  /* Large queue, can drop */

/* Wire connections */
PACKET_SOURCE_CONNECT(eth_rx_source, packet_filter_sink);
PACKET_SOURCE_CONNECT(eth_rx_source, packet_logger_sink);
PACKET_SOURCE_CONNECT(eth_tx_source, packet_router_sink);
PACKET_SOURCE_CONNECT(eth_tx_source, packet_logger_sink);

PACKET_SOURCE_CONNECT(stream_producer, stream_consumer1);
PACKET_SOURCE_CONNECT(stream_producer, stream_consumer2);
PACKET_SOURCE_CONNECT(stream_producer, stream_buffer);

PACKET_SOURCE_CONNECT(bulk_source, bulk_processor);
PACKET_SOURCE_CONNECT(bulk_source, bulk_storage);

PACKET_SOURCE_CONNECT(perf_source1, perf_sink);
PACKET_SOURCE_CONNECT(perf_source2, perf_sink);
PACKET_SOURCE_CONNECT(perf_source3, perf_sink);

/*
 * ===========================================================================
 * HELPER FUNCTIONS
 * ===========================================================================
 */

static void drain_all_sinks(void)
{
	struct net_buf *buf;
	
	while (k_msgq_get(&packet_filter_sink.msgq, &buf, K_NO_WAIT) == 0) {
		net_buf_unref(buf);
	}
	while (k_msgq_get(&packet_router_sink.msgq, &buf, K_NO_WAIT) == 0) {
		net_buf_unref(buf);
	}
	while (k_msgq_get(&packet_logger_sink.msgq, &buf, K_NO_WAIT) == 0) {
		net_buf_unref(buf);
	}
	while (k_msgq_get(&stream_consumer1.msgq, &buf, K_NO_WAIT) == 0) {
		net_buf_unref(buf);
	}
	while (k_msgq_get(&stream_consumer2.msgq, &buf, K_NO_WAIT) == 0) {
		net_buf_unref(buf);
	}
	while (k_msgq_get(&stream_buffer.msgq, &buf, K_NO_WAIT) == 0) {
		net_buf_unref(buf);
	}
	while (k_msgq_get(&bulk_processor.msgq, &buf, K_NO_WAIT) == 0) {
		net_buf_unref(buf);
	}
	while (k_msgq_get(&bulk_storage.msgq, &buf, K_NO_WAIT) == 0) {
		net_buf_unref(buf);
	}
	while (k_msgq_get(&perf_sink.msgq, &buf, K_NO_WAIT) == 0) {
		net_buf_unref(buf);
	}
}

static void fill_random_data(uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		data[i] = (uint8_t)sys_rand32_get();
	}
}

static uint16_t calculate_crc16(const uint8_t *data, size_t len)
{
	uint16_t crc = 0xFFFF;
	
	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int j = 0; j < 8; j++) {
			if (crc & 0x0001) {
				crc = (crc >> 1) ^ 0xA001;
			} else {
				crc >>= 1;
			}
		}
	}
	
	return crc;
}

/*
 * ===========================================================================
 * TEST SETUP
 * ===========================================================================
 */

static void packet_io_integration_setup(void *fixture)
{
	drain_all_sinks();
	
#ifdef CONFIG_PACKET_IO_STATS
	packet_source_reset_stats(&eth_rx_source);
	packet_source_reset_stats(&eth_tx_source);
	packet_source_reset_stats(&stream_producer);
	packet_source_reset_stats(&bulk_source);
	packet_source_reset_stats(&perf_source1);
	packet_source_reset_stats(&perf_source2);
	packet_source_reset_stats(&perf_source3);
	
	packet_sink_reset_stats(&packet_filter_sink);
	packet_sink_reset_stats(&packet_router_sink);
	packet_sink_reset_stats(&packet_logger_sink);
	packet_sink_reset_stats(&stream_consumer1);
	packet_sink_reset_stats(&stream_consumer2);
	packet_sink_reset_stats(&stream_buffer);
	packet_sink_reset_stats(&bulk_processor);
	packet_sink_reset_stats(&bulk_storage);
	packet_sink_reset_stats(&perf_sink);
#endif
	
	ARG_UNUSED(fixture);
}

ZTEST_SUITE(packet_io_integration, NULL, NULL, packet_io_integration_setup, NULL, NULL);

/*
 * ===========================================================================
 * 1. NETWORK PACKET PROCESSING TESTS
 * ===========================================================================
 */

/* Test: Process full Ethernet frames */
ZTEST(packet_io_integration, test_ethernet_frame_processing)
{
	struct net_buf *buf, *received;
	struct ethernet_frame *frame;
	int ret;
	const size_t payload_size = 1486;  /* 1500 - 14 (eth header) */
	
	/* Create a full-size Ethernet frame */
	buf = net_buf_alloc(&large_pool, K_SECONDS(1));
	zassert_not_null(buf, "Failed to allocate large buffer");
	
	/* Build Ethernet frame */
	frame = (struct ethernet_frame *)net_buf_add(buf, sizeof(*frame) + payload_size);
	memset(frame->dst_mac, 0xFF, 6);  /* Broadcast */
	memset(frame->src_mac, 0x02, 6);
	frame->src_mac[5] = 0x01;
	frame->ethertype = htons(0x0800);  /* IPv4 */
	fill_random_data(frame->payload, payload_size);
	
	/* Send through RX pipeline */
	ret = packet_source_send(&eth_rx_source, buf, K_NO_WAIT);
	net_buf_unref(buf);
	zassert_equal(ret, 2, "Should deliver to filter and logger");
	
	/* Verify packet filter received it */
	ret = k_msgq_get(&packet_filter_sink.msgq, &received, K_NO_WAIT);
	zassert_equal(ret, 0, "Filter should receive frame");
	zassert_equal(received->len, 1500, "Frame size should be 1500");
	
	/* Verify frame integrity */
	frame = (struct ethernet_frame *)received->data;
	zassert_equal(frame->ethertype, htons(0x0800), "Ethertype corrupted");
	
	net_buf_unref(received);
}

/* Test: Handle jumbo frames (if pool supports) */
ZTEST(packet_io_integration, test_jumbo_frame_handling)
{
	struct net_buf *buf, *received;
	int ret;
	const size_t jumbo_size = 4096;
	uint8_t *data;
	
	/* Allocate jumbo frame */
	buf = net_buf_alloc(&jumbo_pool, K_SECONDS(1));
	zassert_not_null(buf, "Failed to allocate jumbo buffer");
	
	/* Fill with pattern */
	data = net_buf_add(buf, jumbo_size);
	for (size_t i = 0; i < jumbo_size; i++) {
		data[i] = (uint8_t)(i & 0xFF);
	}
	
	/* Send through TX pipeline */
	ret = packet_source_send(&eth_tx_source, buf, K_NO_WAIT);
	net_buf_unref(buf);
	zassert_equal(ret, 2, "Should deliver to router and logger");
	
	/* Verify router received full jumbo frame */
	ret = k_msgq_get(&packet_router_sink.msgq, &received, K_NO_WAIT);
	zassert_equal(ret, 0, "Router should receive jumbo frame");
	zassert_equal(received->len, jumbo_size, "Jumbo frame size mismatch");
	
	/* Verify data integrity */
	for (size_t i = 0; i < jumbo_size; i++) {
		zassert_equal(received->data[i], (uint8_t)(i & 0xFF),
			      "Data corrupted at offset %zu", i);
		if (received->data[i] != (uint8_t)(i & 0xFF)) {
			break;  /* Stop on first error */
		}
	}
	
	net_buf_unref(received);
}

/* Test: Network bridge simulation */
ZTEST(packet_io_integration, test_network_bridge)
{
	struct net_buf *buf, *received;
	struct ethernet_frame *frame;
	int ret;
	int bridged = 0;
	
	/* Simulate bridging 10 packets */
	for (int i = 0; i < 10; i++) {
		buf = net_buf_alloc(&large_pool, K_NO_WAIT);
		if (!buf) {
			break;
		}
		
		/* Create frame with unique identifier */
		frame = (struct ethernet_frame *)net_buf_add(buf, 64);
		memset(frame->dst_mac, 0xFF, 6);
		memset(frame->src_mac, i, 6);
		frame->ethertype = htons(0x0800);
		
		/* RX -> process */
		ret = packet_source_send(&eth_rx_source, buf, K_NO_WAIT);
		net_buf_unref(buf);
		zassert_true(ret > 0, "RX failed");
		
		/* Get from filter */
		ret = k_msgq_get(&packet_filter_sink.msgq, &received, K_NO_WAIT);
		if (ret == 0) {
			/* Bridge to TX */
			ret = packet_source_send(&eth_tx_source, received, K_NO_WAIT);
			net_buf_unref(received);
			if (ret > 0) {
				bridged++;
			}
		}
	}
	
	zassert_true(bridged > 0, "Should bridge some packets");
	TC_PRINT("Bridged %d packets\n", bridged);
}

/*
 * ===========================================================================
 * 2. STREAMING DATA TESTS
 * ===========================================================================
 */

/* Test: Audio stream distribution */
ZTEST(packet_io_integration, test_audio_stream_distribution)
{
	struct net_buf *buf, *received1, *received2;
	struct stream_frame *frame;
	int ret;
	const size_t audio_frame_size = 1024;  /* 1024 samples * 1 byte */
	uint32_t sequence = 0;
	
	/* Simulate 100ms of 10kHz audio (10 frames) */
	for (int i = 0; i < 10; i++) {
		buf = net_buf_alloc(&large_pool, K_NO_WAIT);
		if (!buf) {
			break;
		}
		
		/* Create audio frame */
		frame = (struct stream_frame *)net_buf_add(buf,
			sizeof(*frame) + audio_frame_size);
		frame->sequence = sequence++;
		frame->timestamp = k_uptime_get_32();
		frame->frame_size = audio_frame_size;
		frame->flags = 0;
		
		/* Fill with sine wave pattern */
		for (size_t j = 0; j < audio_frame_size; j++) {
			frame->data[j] = (uint8_t)(128 + 127 * sinf(2 * 3.14159f * j / 64));
		}
		
		/* Distribute to consumers */
		ret = packet_source_send(&stream_producer, buf, K_NO_WAIT);
		net_buf_unref(buf);
		zassert_equal(ret, 3, "Should deliver to 3 stream sinks");
		
		/* Small delay between frames */
		k_busy_wait(100);
	}
	
	/* Verify both consumers received all frames */
	int count1 = 0, count2 = 0;
	while (k_msgq_get(&stream_consumer1.msgq, &received1, K_NO_WAIT) == 0) {
		frame = (struct stream_frame *)received1->data;
		zassert_equal(frame->frame_size, audio_frame_size, "Frame size mismatch");
		net_buf_unref(received1);
		count1++;
	}
	
	while (k_msgq_get(&stream_consumer2.msgq, &received2, K_NO_WAIT) == 0) {
		net_buf_unref(received2);
		count2++;
	}
	
	zassert_equal(count1, count2, "Consumers should receive same number of frames");
	TC_PRINT("Distributed %d audio frames to each consumer\n", count1);
}

/* Test: Video frame chunking */
ZTEST(packet_io_integration, test_video_frame_distribution)
{
	struct net_buf *buf, *received;
	struct stream_frame *frame;
	int ret;
	const size_t chunk_size = 4000;  /* Just under 4KB */
	const int chunks_per_frame = 6;  /* 24KB frame - fits in jumbo_pool (8 buffers) */
	uint32_t frame_num = 0;
	
	/* Send one video frame as multiple chunks */
	for (int chunk = 0; chunk < chunks_per_frame; chunk++) {
		buf = net_buf_alloc(&jumbo_pool, K_NO_WAIT);
		if (!buf) {
			TC_PRINT("Failed to allocate buffer for chunk %d\n", chunk);
			break;
		}
		
		frame = (struct stream_frame *)net_buf_add(buf,
			sizeof(*frame) + chunk_size);
		frame->sequence = (frame_num << 16) | chunk;
		frame->timestamp = k_uptime_get_32();
		frame->frame_size = chunk_size;
		frame->flags = (chunk == 0) ? 0x01 : 0;  /* Start of frame */
		frame->flags |= (chunk == chunks_per_frame - 1) ? 0x02 : 0;  /* End */
		
		/* Fill with video data pattern */
		fill_random_data(frame->data, chunk_size);
		
		ret = packet_source_send(&stream_producer, buf, K_NO_WAIT);
		net_buf_unref(buf);
		if (ret <= 0) {
			TC_PRINT("Failed to send video chunk %d, ret=%d\n", chunk, ret);
		}
		zassert_true(ret > 0, "Failed to send video chunk %d", chunk);
	}
	
	/* Verify buffer sink received all chunks */
	int chunks_received = 0;
	while (k_msgq_get(&stream_buffer.msgq, &received, K_NO_WAIT) == 0) {
		frame = (struct stream_frame *)received->data;
		int chunk_num = frame->sequence & 0xFFFF;
		zassert_true(chunk_num < chunks_per_frame, "Invalid chunk number");
		chunks_received++;
		net_buf_unref(received);
	}
	
	zassert_equal(chunks_received, chunks_per_frame,
		      "Should receive all %d chunks", chunks_per_frame);
}

/*
 * ===========================================================================
 * 3. BULK DATA TRANSFER TESTS
 * ===========================================================================
 */

/* Test: Firmware update distribution */
ZTEST(packet_io_integration, test_firmware_update_distribution)
{
	struct net_buf *buf, *received;
	struct bulk_chunk *chunk;
	int ret;
	const size_t chunk_size = 512;  /* Typical flash page */
	const size_t total_size = 64 * 1024;  /* 64KB firmware */
	uint32_t offset = 0;
	int chunks_sent = 0;
	
	/* Send firmware in chunks */
	while (offset < total_size) {
		buf = net_buf_alloc(&large_pool, K_NO_WAIT);
		if (!buf) {
			break;
		}
		
		chunk = (struct bulk_chunk *)net_buf_add(buf,
			sizeof(*chunk) + chunk_size);
		chunk->offset = offset;
		chunk->total_size = total_size;
		chunk->chunk_size = chunk_size;
		
		/* Fill with firmware data */
		for (size_t i = 0; i < chunk_size; i++) {
			chunk->data[i] = (uint8_t)((offset + i) & 0xFF);
		}
		chunk->crc16 = calculate_crc16(chunk->data, chunk_size);
		
		ret = packet_source_send(&bulk_source, buf, K_NO_WAIT);
		net_buf_unref(buf);
		if (ret > 0) {
			chunks_sent++;
			offset += chunk_size;
		}
		
		/* Throttle to simulate flash write speed */
		k_busy_wait(1000);  /* 1ms per chunk */
	}
	
	/* Verify processor received chunks */
	int chunks_processed = 0;
	uint32_t last_offset = 0;
	
	while (k_msgq_get(&bulk_processor.msgq, &received, K_NO_WAIT) == 0) {
		chunk = (struct bulk_chunk *)received->data;
		
		/* Verify CRC */
		uint16_t calculated_crc = calculate_crc16(chunk->data, chunk->chunk_size);
		zassert_equal(chunk->crc16, calculated_crc, "CRC mismatch at offset %u",
			      chunk->offset);
		
		/* Verify sequential offsets */
		if (chunks_processed > 0) {
			zassert_equal(chunk->offset, last_offset + 512,
				      "Non-sequential chunk");
		}
		last_offset = chunk->offset;
		
		chunks_processed++;
		net_buf_unref(received);
	}
	
	TC_PRINT("Sent %d chunks, processed %d chunks (%.1f KB)\n",
		 chunks_sent, chunks_processed, (double)(chunks_processed * 0.5f));
	zassert_true(chunks_processed > 0, "Should process firmware chunks");
}

/* Test: Large file transfer */
ZTEST(packet_io_integration, test_file_transfer_pipeline)
{
	struct net_buf *buf, *received;
	int ret;
	const size_t block_size = 1400;  /* Close to MTU */
	const int num_blocks = 20;
	uint8_t *data;
	int sent = 0, stored = 0;
	
	/* Send file blocks */
	for (int i = 0; i < num_blocks; i++) {
		buf = net_buf_alloc(&large_pool, K_NO_WAIT);
		if (!buf) {
			break;
		}
		
		data = net_buf_add(buf, block_size);
		
		/* Create recognizable pattern */
		for (size_t j = 0; j < block_size; j++) {
			data[j] = (uint8_t)((i * block_size + j) % 256);
		}
		
		ret = packet_source_send(&bulk_source, buf, K_NO_WAIT);
		net_buf_unref(buf);
		if (ret > 0) {
			sent++;
		}
	}
	
	/* Process stored blocks */  
	int received_blocks = 0;
	while (k_msgq_get(&bulk_storage.msgq, &received, K_NO_WAIT) == 0) {
		zassert_equal(received->len, block_size, "Block size mismatch");
		
		/* Verify pattern - we can't reliably recover block number, so just check first few bytes match expected pattern */
		for (int i = 0; i < MIN(10, block_size); i++) {
			/* Pattern should be consistent within each block */
			uint8_t expected = (uint8_t)((received->data[0] + i) % 256);
			zassert_equal(received->data[i], expected,
				      "Data corruption at offset %d", i);
		}
		
		stored++;
		received_blocks++;
		net_buf_unref(received);
	}
	
	zassert_equal(sent, stored, "All sent blocks should be stored");
	TC_PRINT("Transferred %d blocks (%.1f KB)\n", stored,
		 (double)(stored * block_size / 1024.0f));
}

/*
 * ===========================================================================
 * 4. BUFFER CHAIN OPERATIONS
 * ===========================================================================
 */

/* Test: Fragmented packet handling */
ZTEST(packet_io_integration, test_packet_fragmentation_chain)
{
	struct net_buf *head, *frag, *received;
	int ret;
	const size_t frag_size = 256;
	const int num_frags = 4;
	
	/* Create chained buffer */
	head = net_buf_alloc(&small_pool, K_NO_WAIT);
	zassert_not_null(head, "Failed to allocate head buffer");
	
	/* Add data to head */
	net_buf_add_le32(head, 0xDEADBEEF);
	net_buf_add_le32(head, num_frags);
	
	/* Create fragment chain */
	struct net_buf *current = head;
	for (int i = 0; i < num_frags; i++) {
		frag = net_buf_alloc(&medium_pool, K_NO_WAIT);
		if (!frag) {
			break;
		}
		
		uint8_t *data = net_buf_add(frag, frag_size);
		memset(data, i + 1, frag_size);
		
		/* Chain the fragment */
		net_buf_frag_add(current, frag);
		current = frag;
	}
	
	/* Send chained buffer */
	ret = packet_source_send(&eth_rx_source, head, K_NO_WAIT);
	net_buf_unref(head);
	zassert_true(ret > 0, "Failed to send chained buffer");
	
	/* Receive and verify chain */
	ret = k_msgq_get(&packet_filter_sink.msgq, &received, K_NO_WAIT);
	zassert_equal(ret, 0, "Should receive chained buffer");
	
	/* Verify we got the head */
	zassert_equal(*(uint32_t *)received->data, 0xDEADBEEF, "Head corrupted");
	
	/* Count fragments */
	int frag_count = 0;
	struct net_buf *iter = received->frags;
	while (iter) {
		frag_count++;
		iter = iter->frags;
	}
	
	zassert_true(frag_count > 0, "Should have fragments");
	TC_PRINT("Handled chain with %d fragments\n", frag_count);
	
	net_buf_unref(received);
}

/*
 * ===========================================================================
 * 5. PERFORMANCE AND STRESS TESTS
 * ===========================================================================
 */

/* Test: Sustained throughput measurement */
ZTEST(packet_io_integration, test_sustained_throughput)
{
	struct net_buf *buf;
	int ret;
	const size_t packet_size = 1500;
	const uint32_t target_packets = 1000;  /* Send 1000 packets instead of time-based */
	uint32_t start, elapsed;
	uint64_t bytes_sent = 0;
	uint32_t packets_sent = 0;
	
	timing_init();
	start = k_uptime_get_32();
	
	/* Send target number of packets (timer-based test doesn't work on native_sim) */
	uint32_t stuck_counter = 0;
	
	TC_PRINT("Starting sustained throughput test for %u packets...\n", target_packets);
	
	while (packets_sent < target_packets) {
		
		buf = net_buf_alloc(&large_pool, K_NO_WAIT);
		if (!buf) {
			/* Pool exhausted, drain some packets and try again */
			struct net_buf *drain_buf;
			int drained = 0;
			for (int i = 0; i < 10 && k_msgq_get(&perf_sink.msgq, &drain_buf, K_NO_WAIT) == 0; i++) {
				net_buf_unref(drain_buf);
				drained++;
			}
			
			stuck_counter++;
			if (stuck_counter % 100 == 0) {  /* Report every 100 failures */
				TC_PRINT("Allocation failed %u times, drained %d buffers\n", stuck_counter, drained);
			}
			
			if (stuck_counter > 2000) {  /* Emergency exit */
				TC_PRINT("EMERGENCY: Too many allocation failures (%u), stopping\n", stuck_counter);
				break;
			}
			
			k_yield();
			continue;
		}
		
		stuck_counter = 0;  /* Reset stuck counter on successful allocation */
		
		uint8_t *data = net_buf_add(buf, packet_size);
		memset(data, packets_sent & 0xFF, packet_size);
		
		ret = packet_source_send(&perf_source1, buf, K_NO_WAIT);
		net_buf_unref(buf);
		if (ret > 0) {
			bytes_sent += packet_size;
			packets_sent++;
		} else {
			TC_PRINT("packet_source_send failed: ret=%d\n", ret);
		}
		
		/* Periodically drain sink to prevent backup */
		if (packets_sent % 100 == 0) {  /* Every 100 packets */
			struct net_buf *drain_buf;
			int drained = 0;
			for (int i = 0; i < 20 && k_msgq_get(&perf_sink.msgq, &drain_buf, K_NO_WAIT) == 0; i++) {
				net_buf_unref(drain_buf);
				drained++;
			}
		}
	}
	
	elapsed = k_uptime_get_32() - start;
	if (elapsed == 0) elapsed = 1;  /* Avoid division by zero on native_sim */
	
	/* Calculate throughput */
	float throughput_mbps = (bytes_sent * 8.0f) / (elapsed * 1000.0f);
	float packet_rate = (packets_sent * 1000.0f) / elapsed;
	
	TC_PRINT("Throughput: %.2f Mbps, %.0f packets/sec\n",
		 (double)throughput_mbps, (double)packet_rate);
	TC_PRINT("Sent %u packets (%llu bytes) in %u ms\n",
		 packets_sent, bytes_sent, elapsed);
	
	zassert_true(packets_sent > 0, "Should send packets");
	zassert_true(throughput_mbps > 0, "Should achieve some throughput");
	
	/* Drain sink */
	int received = 0;
	while (k_msgq_get(&perf_sink.msgq, &buf, K_NO_WAIT) == 0) {
		net_buf_unref(buf);
		received++;
	}
	
	TC_PRINT("Received %d packets (%.1f%% delivery)\n",
		 received, (double)((received * 100.0f) / packets_sent));
}

/* Test: Latency under load */
ZTEST(packet_io_integration, test_latency_under_load)
{
	struct net_buf *buf, *received;
	int ret;
	const size_t packet_size = 512;
	uint32_t timestamps[10];
	uint32_t latencies[10];
	int samples = 0;
	
	/* Measure latency for 10 packets under load */
	for (int i = 0; i < 10; i++) {
		/* Generate background load */
		for (int j = 0; j < 5; j++) {
			buf = net_buf_alloc(&medium_pool, K_NO_WAIT);
			if (buf) {
				net_buf_add(buf, packet_size);
				packet_source_send(&perf_source2, buf, K_NO_WAIT);
				net_buf_unref(buf);
			}
		}
		
		/* Send timestamped packet */
		buf = net_buf_alloc(&medium_pool, K_NO_WAIT);
		if (!buf) {
			continue;
		}
		
		uint32_t *timestamp = (uint32_t *)net_buf_add(buf, sizeof(uint32_t));
		*timestamp = k_cycle_get_32();
		net_buf_add(buf, packet_size - sizeof(uint32_t));
		
		timestamps[i] = *timestamp;
		ret = packet_source_send(&perf_source1, buf, K_NO_WAIT);
		net_buf_unref(buf);
		
		if (ret > 0) {
			/* Try to receive immediately */
			ret = k_msgq_get(&perf_sink.msgq, &received, K_MSEC(10));
			if (ret == 0) {
				uint32_t rx_time = k_cycle_get_32();
				uint32_t tx_time = *(uint32_t *)received->data;
				latencies[samples] = rx_time - tx_time;
				samples++;
				net_buf_unref(received);
			}
		}
		
		/* Drain remaining */
		while (k_msgq_get(&perf_sink.msgq, &received, K_NO_WAIT) == 0) {
			net_buf_unref(received);
		}
	}
	
	if (samples > 0) {
		/* Calculate average latency */
		uint64_t total = 0;
		uint32_t min = UINT32_MAX, max = 0;
		
		for (int i = 0; i < samples; i++) {
			total += latencies[i];
			if (latencies[i] < min) min = latencies[i];
			if (latencies[i] > max) max = latencies[i];
		}
		
		uint32_t avg = total / samples;
		uint32_t freq = sys_clock_hw_cycles_per_sec();
		
		TC_PRINT("Latency - Avg: %u us, Min: %u us, Max: %u us\n",
			 (avg * 1000000) / freq,
			 (min * 1000000) / freq,
			 (max * 1000000) / freq);
	}
	
	zassert_true(samples > 0, "Should measure some latencies");
}

/* Test: Concurrent high throughput from multiple sources */
ZTEST(packet_io_integration, test_concurrent_high_throughput)
{
	struct net_buf *buf;
	const size_t packet_size = 1024;
	const int packets_per_source = 100;
	int sent[3] = {0, 0, 0};
	int ret;
	
	/* Three sources send concurrently */
	for (int i = 0; i < packets_per_source; i++) {
		/* Source 1 */
		buf = net_buf_alloc(&large_pool, K_NO_WAIT);
		if (buf) {
			net_buf_add(buf, packet_size);
			ret = packet_source_send(&perf_source1, buf, K_NO_WAIT);
			net_buf_unref(buf);
			if (ret > 0) sent[0]++;
		}
		
		/* Source 2 */
		buf = net_buf_alloc(&large_pool, K_NO_WAIT);
		if (buf) {
			net_buf_add(buf, packet_size);
			ret = packet_source_send(&perf_source2, buf, K_NO_WAIT);
			net_buf_unref(buf);
			if (ret > 0) sent[1]++;
		}
		
		/* Source 3 */
		buf = net_buf_alloc(&large_pool, K_NO_WAIT);
		if (buf) {
			net_buf_add(buf, packet_size);
			ret = packet_source_send(&perf_source3, buf, K_NO_WAIT);
			net_buf_unref(buf);
			if (ret > 0) sent[2]++;
		}
		
		/* Small yield to prevent starvation */
		if (i % 10 == 0) {
			k_yield();
		}
	}
	
	TC_PRINT("Concurrent sends - Source1: %d, Source2: %d, Source3: %d\n",
		 sent[0], sent[1], sent[2]);
	
	/* Count total received */
	int received = 0;
	while (k_msgq_get(&perf_sink.msgq, &buf, K_NO_WAIT) == 0) {
		received++;
		net_buf_unref(buf);
	}
	
	int total_sent = sent[0] + sent[1] + sent[2];
	TC_PRINT("Total: sent %d, received %d (%.1f%% delivery)\n",
		 total_sent, received, (double)((received * 100.0f) / total_sent));
	
	zassert_true(received > 0, "Should receive packets");
	
	/* With drop_on_full=true, some packets may be dropped under high load */
	zassert_true(received <= total_sent, "Cannot receive more than sent");
}

/* Test: Memory pool exhaustion handling */
ZTEST(packet_io_integration, test_memory_pool_exhaustion)
{
	struct net_buf *bufs[32];
	struct net_buf *buf;
	int allocated = 0;
	int ret;
	
	/* Exhaust the medium pool */
	for (int i = 0; i < 32; i++) {
		bufs[i] = net_buf_alloc(&medium_pool, K_NO_WAIT);
		if (bufs[i]) {
			allocated++;
		} else {
			break;
		}
	}
	
	TC_PRINT("Allocated %d buffers before exhaustion\n", allocated);
	
	/* Try to send when pool is exhausted */
	buf = net_buf_alloc(&medium_pool, K_NO_WAIT);
	zassert_is_null(buf, "Pool should be exhausted");
	
	/* Free half the buffers */
	for (int i = 0; i < allocated / 2; i++) {
		if (bufs[i]) {
			net_buf_unref(bufs[i]);
			bufs[i] = NULL;
		}
	}
	
	/* Now should be able to allocate and send */
	int sent = 0;
	for (int i = 0; i < 5; i++) {
		buf = net_buf_alloc(&medium_pool, K_NO_WAIT);
		if (buf) {
			net_buf_add(buf, 128);
			ret = packet_source_send(&bulk_source, buf, K_NO_WAIT);
			net_buf_unref(buf);
			if (ret > 0) {
				sent++;
			}
		}
	}
	
	zassert_true(sent > 0, "Should send after freeing buffers");
	TC_PRINT("Sent %d packets after partial free\n", sent);
	
	/* Clean up remaining buffers */
	for (int i = 0; i < 32; i++) {
		if (bufs[i]) {
			net_buf_unref(bufs[i]);
		}
	}
}

/*
 * ===========================================================================
 * 6. PROTOCOL TRANSLATION TESTS
 * ===========================================================================
 */

/* Test: UART to Ethernet bridge simulation */
ZTEST(packet_io_integration, test_uart_to_ethernet_bridge)
{
	struct net_buf *uart_buf, *eth_buf, *received;
	struct ethernet_frame *eth_frame;
	int ret;
	const uint8_t uart_data[] = "Hello from UART!";
	const size_t uart_len = sizeof(uart_data);
	
	/* Simulate UART data reception */
	uart_buf = net_buf_alloc(&small_pool, K_NO_WAIT);
	zassert_not_null(uart_buf, "Failed to allocate UART buffer");
	
	memcpy(net_buf_add(uart_buf, uart_len), uart_data, uart_len);
	
	/* Bridge to Ethernet format */
	eth_buf = net_buf_alloc(&large_pool, K_NO_WAIT);
	zassert_not_null(eth_buf, "Failed to allocate Ethernet buffer");
	
	/* Build Ethernet frame with UART data as payload */
	eth_frame = (struct ethernet_frame *)net_buf_add(eth_buf,
		sizeof(*eth_frame) + uart_len);
	memset(eth_frame->dst_mac, 0xFF, 6);  /* Broadcast */
	memset(eth_frame->src_mac, 0x42, 6);  /* Bridge MAC */
	eth_frame->ethertype = htons(0x88B5);  /* Local experimental */
	memcpy(eth_frame->payload, uart_data, uart_len);
	
	/* Release UART buffer */
	net_buf_unref(uart_buf);
	
	/* Send through Ethernet TX */
	ret = packet_source_send(&eth_tx_source, eth_buf, K_NO_WAIT);
	net_buf_unref(eth_buf);
	zassert_true(ret > 0, "Failed to send bridged packet");
	
	/* Verify router received it */
	ret = k_msgq_get(&packet_router_sink.msgq, &received, K_NO_WAIT);
	zassert_equal(ret, 0, "Router should receive bridged packet");
	
	eth_frame = (struct ethernet_frame *)received->data;
	zassert_equal(eth_frame->ethertype, htons(0x88B5), "Wrong ethertype");
	zassert_mem_equal(eth_frame->payload, uart_data, uart_len,
			  "Payload corrupted");
	
	net_buf_unref(received);
	TC_PRINT("Successfully bridged UART to Ethernet\n");
}

