/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/buf.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/timing/timing.h>
#include <zephyr/ztest.h>
#include <zephyr_io/flow/flow.h>

LOG_MODULE_REGISTER(flow_integration, LOG_LEVEL_INF);

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
 * TEST CAPTURE STRUCTURES
 * ===========================================================================
 */

struct test_capture {
  atomic_t count;
  atomic_t byte_count;
  uint32_t last_value;
  uint32_t last_sequence;
};

static struct test_capture eth_filter_capture;
static struct test_capture eth_router_capture;
static struct test_capture eth_logger_capture;
static struct test_capture stream_consumer1_capture;
static struct test_capture stream_consumer2_capture;
static struct test_capture stream_buffer_capture;
static struct test_capture bulk_processor_capture;
static struct test_capture bulk_storage_capture;
static struct test_capture perf_capture;

/*
 * ===========================================================================
 * HANDLER FUNCTIONS
 * ===========================================================================
 */

static void eth_filter_handler(struct flow_sink *sink, struct net_buf *buf) {
  atomic_inc(&eth_filter_capture.count);
  atomic_add(&eth_filter_capture.byte_count, buf->len);
  if (buf->len >= sizeof(struct ethernet_frame)) {
    struct ethernet_frame *frame = (struct ethernet_frame *)buf->data;
    eth_filter_capture.last_value = frame->ethertype;
  }
}

static void eth_router_handler(struct flow_sink *sink, struct net_buf *buf) {
  atomic_inc(&eth_router_capture.count);
  atomic_add(&eth_router_capture.byte_count, buf->len);
  if (buf->len >= sizeof(struct ethernet_frame)) {
    struct ethernet_frame *frame = (struct ethernet_frame *)buf->data;
    eth_router_capture.last_value = frame->ethertype;
  }
}

static void eth_logger_handler(struct flow_sink *sink, struct net_buf *buf) {
  atomic_inc(&eth_logger_capture.count);
  atomic_add(&eth_logger_capture.byte_count, buf->len);
}

static void stream_consumer1_handler(struct flow_sink *sink,
                                     struct net_buf *buf) {
  atomic_inc(&stream_consumer1_capture.count);
  if (buf->len >= sizeof(struct stream_frame)) {
    struct stream_frame *frame = (struct stream_frame *)buf->data;
    stream_consumer1_capture.last_sequence = frame->sequence;
  }
}

static void stream_consumer2_handler(struct flow_sink *sink,
                                     struct net_buf *buf) {
  atomic_inc(&stream_consumer2_capture.count);
  if (buf->len >= sizeof(struct stream_frame)) {
    struct stream_frame *frame = (struct stream_frame *)buf->data;
    stream_consumer2_capture.last_sequence = frame->sequence;
  }
}

static void stream_buffer_handler(struct flow_sink *sink, struct net_buf *buf) {
  atomic_inc(&stream_buffer_capture.count);
  if (buf->len >= sizeof(struct stream_frame)) {
    struct stream_frame *frame = (struct stream_frame *)buf->data;
    stream_buffer_capture.last_sequence = frame->sequence;
  }
}

static void bulk_processor_handler(struct flow_sink *sink,
                                   struct net_buf *buf) {
  atomic_inc(&bulk_processor_capture.count);
  atomic_add(&bulk_processor_capture.byte_count, buf->len);
  if (buf->len >= sizeof(struct bulk_chunk)) {
    struct bulk_chunk *chunk = (struct bulk_chunk *)buf->data;
    bulk_processor_capture.last_value = chunk->offset;
  }
}

static void bulk_storage_handler(struct flow_sink *sink, struct net_buf *buf) {
  atomic_inc(&bulk_storage_capture.count);
  atomic_add(&bulk_storage_capture.byte_count, buf->len);
  if (buf->len >= sizeof(struct bulk_chunk)) {
    struct bulk_chunk *chunk = (struct bulk_chunk *)buf->data;
    bulk_storage_capture.last_value = chunk->offset;
  }
}

static void perf_handler(struct flow_sink *sink, struct net_buf *buf) {
  atomic_inc(&perf_capture.count);
  atomic_add(&perf_capture.byte_count, buf->len);
}

/*
 * ===========================================================================
 * PACKET SOURCES AND SINKS (NEW API)
 * ===========================================================================
 */

/* Event queues for queued sinks */
FLOW_EVENT_QUEUE_DEFINE(eth_queue, 32);
FLOW_EVENT_QUEUE_DEFINE(stream_queue, 64);
FLOW_EVENT_QUEUE_DEFINE(bulk_queue, 32);
FLOW_EVENT_QUEUE_DEFINE(perf_queue, 128);

/* Network processing pipeline */
FLOW_SOURCE_DEFINE(eth_rx_source);
FLOW_SOURCE_DEFINE(eth_tx_source);

FLOW_SINK_DEFINE_QUEUED(packet_filter_sink, eth_filter_handler, eth_queue);
FLOW_SINK_DEFINE_QUEUED(packet_router_sink, eth_router_handler, eth_queue);
FLOW_SINK_DEFINE_IMMEDIATE(packet_logger_sink, eth_logger_handler);

/* Streaming pipeline */
FLOW_SOURCE_DEFINE(stream_producer);

FLOW_SINK_DEFINE_QUEUED(stream_consumer1, stream_consumer1_handler,
                        stream_queue);
FLOW_SINK_DEFINE_QUEUED(stream_consumer2, stream_consumer2_handler,
                        stream_queue);
FLOW_SINK_DEFINE_QUEUED(stream_buffer, stream_buffer_handler, stream_queue);

/* Bulk transfer pipeline */
FLOW_SOURCE_DEFINE(bulk_source);

FLOW_SINK_DEFINE_QUEUED(bulk_processor, bulk_processor_handler, bulk_queue);
FLOW_SINK_DEFINE_QUEUED(bulk_storage, bulk_storage_handler, bulk_queue);

/* Performance test sources/sinks */
FLOW_SOURCE_DEFINE(perf_source1);
FLOW_SOURCE_DEFINE(perf_source2);
FLOW_SOURCE_DEFINE(perf_source3);

FLOW_SINK_DEFINE_QUEUED(perf_sink, perf_handler, perf_queue);

/* Wire connections */
FLOW_CONNECT(&eth_rx_source, &packet_filter_sink);
FLOW_CONNECT(&eth_rx_source, &packet_logger_sink);
FLOW_CONNECT(&eth_tx_source, &packet_router_sink);
FLOW_CONNECT(&eth_tx_source, &packet_logger_sink);

FLOW_CONNECT(&stream_producer, &stream_consumer1);
FLOW_CONNECT(&stream_producer, &stream_consumer2);
FLOW_CONNECT(&stream_producer, &stream_buffer);

FLOW_CONNECT(&bulk_source, &bulk_processor);
FLOW_CONNECT(&bulk_source, &bulk_storage);

FLOW_CONNECT(&perf_source1, &perf_sink);
FLOW_CONNECT(&perf_source2, &perf_sink);
FLOW_CONNECT(&perf_source3, &perf_sink);

/*
 * ===========================================================================
 * HELPER FUNCTIONS
 * ===========================================================================
 */

static void reset_captures(void) {
  atomic_clear(&eth_filter_capture.count);
  atomic_clear(&eth_filter_capture.byte_count);
  atomic_clear(&eth_router_capture.count);
  atomic_clear(&eth_router_capture.byte_count);
  atomic_clear(&eth_logger_capture.count);
  atomic_clear(&eth_logger_capture.byte_count);
  atomic_clear(&stream_consumer1_capture.count);
  atomic_clear(&stream_consumer2_capture.count);
  atomic_clear(&stream_buffer_capture.count);
  atomic_clear(&bulk_processor_capture.count);
  atomic_clear(&bulk_processor_capture.byte_count);
  atomic_clear(&bulk_storage_capture.count);
  atomic_clear(&bulk_storage_capture.byte_count);
  atomic_clear(&perf_capture.count);
  atomic_clear(&perf_capture.byte_count);
}

static void process_all_events(void) {
  int processed = 0;

  /* Process all queued events */
  while (flow_event_process(&eth_queue, K_NO_WAIT) == 0 ||
         flow_event_process(&stream_queue, K_NO_WAIT) == 0 ||
         flow_event_process(&bulk_queue, K_NO_WAIT) == 0 ||
         flow_event_process(&perf_queue, K_NO_WAIT) == 0) {
    processed++;
    if (processed > 1000) {
      break; /* Safety limit */
    }
  }
}

static void fill_random_data(uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    data[i] = (uint8_t)sys_rand32_get();
  }
}

static uint16_t calculate_crc16(const uint8_t *data, size_t len) {
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

static void flow_integration_setup(void *fixture) {
  reset_captures();
  process_all_events(); /* Drain any lingering events */

#ifdef CONFIG_FLOW_STATS
  flow_source_reset_stats(&eth_rx_source);
  flow_source_reset_stats(&eth_tx_source);
  flow_source_reset_stats(&stream_producer);
  flow_source_reset_stats(&bulk_source);
  flow_source_reset_stats(&perf_source1);
  flow_source_reset_stats(&perf_source2);
  flow_source_reset_stats(&perf_source3);

  flow_sink_reset_stats(&packet_filter_sink);
  flow_sink_reset_stats(&packet_router_sink);
  flow_sink_reset_stats(&packet_logger_sink);
  flow_sink_reset_stats(&stream_consumer1);
  flow_sink_reset_stats(&stream_consumer2);
  flow_sink_reset_stats(&stream_buffer);
  flow_sink_reset_stats(&bulk_processor);
  flow_sink_reset_stats(&bulk_storage);
  flow_sink_reset_stats(&perf_sink);
#endif

  ARG_UNUSED(fixture);
}

ZTEST_SUITE(flow_integration, NULL, NULL, flow_integration_setup, NULL, NULL);

/*
 * ===========================================================================
 * 1. NETWORK PACKET PROCESSING TESTS
 * ===========================================================================
 */

/* Test: Process full Ethernet frames */
ZTEST(flow_integration, test_ethernet_frame_processing) {
  struct net_buf *buf;
  struct ethernet_frame *frame;
  int ret;
  const size_t payload_size = 1486; /* 1500 - 14 (eth header) */

  /* Create a full-size Ethernet frame */
  buf = net_buf_alloc(&large_pool, K_SECONDS(1));
  zassert_not_null(buf, "Failed to allocate large buffer");

  /* Build Ethernet frame */
  frame =
      (struct ethernet_frame *)net_buf_add(buf, sizeof(*frame) + payload_size);
  memset(frame->dst_mac, 0xFF, 6); /* Broadcast */
  memset(frame->src_mac, 0x02, 6);
  frame->src_mac[5] = 0x01;
  frame->ethertype = htons(0x0800); /* IPv4 */
  fill_random_data(frame->payload, payload_size);

  /* Send through RX pipeline */
  ret = flow_source_send(&eth_rx_source, buf, K_NO_WAIT);
  net_buf_unref(buf);
  zassert_equal(ret, 2, "Should deliver to filter and logger");

  /* Process events */
  process_all_events();

  /* Verify captures */
  zassert_equal(atomic_get(&eth_filter_capture.count), 1,
                "Filter should receive frame");
  zassert_equal(atomic_get(&eth_logger_capture.count), 1,
                "Logger should receive frame");
  zassert_equal(eth_filter_capture.last_value, htons(0x0800),
                "Ethertype should match");
}

/* Test: Handle jumbo frames */
ZTEST(flow_integration, test_jumbo_frame_handling) {
  struct net_buf *buf;
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
  ret = flow_source_send(&eth_tx_source, buf, K_NO_WAIT);
  net_buf_unref(buf);
  zassert_equal(ret, 2, "Should deliver to router and logger");

  /* Process events */
  process_all_events();

  /* Verify router received full jumbo frame */
  zassert_equal(atomic_get(&eth_router_capture.count), 1,
                "Router should receive jumbo frame");
  zassert_equal(atomic_get(&eth_router_capture.byte_count), jumbo_size,
                "Jumbo frame size mismatch");
  zassert_equal(atomic_get(&eth_logger_capture.count), 1,
                "Logger should receive frame");
}

/* Test: Network bridge simulation */
ZTEST(flow_integration, test_network_bridge) {
  struct net_buf *buf;
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
    ret = flow_source_send(&eth_rx_source, buf, K_NO_WAIT);
    if (ret > 0) {
      /* Forward to TX */
      ret = flow_source_send(&eth_tx_source, buf, K_NO_WAIT);
      if (ret > 0) {
        bridged++;
      }
    }
    net_buf_unref(buf);
  }

  /* Process all events */
  process_all_events();

  zassert_true(bridged > 0, "Should bridge some packets");
  zassert_true(atomic_get(&eth_filter_capture.count) >= bridged,
               "Filter should receive packets");
  zassert_true(atomic_get(&eth_router_capture.count) >= bridged,
               "Router should receive packets");

  TC_PRINT("Bridged %d packets\n", bridged);
}

/*
 * ===========================================================================
 * 2. STREAMING DATA TESTS
 * ===========================================================================
 */

/* Test: Audio stream distribution */
ZTEST(flow_integration, test_audio_stream_distribution) {
  struct net_buf *buf;
  struct stream_frame *frame;
  int ret;
  const size_t audio_frame_size = 1024; /* 1024 samples * 1 byte */
  uint32_t sequence = 0;
  const int num_frames = 10;

  /* Simulate 100ms of 10kHz audio (10 frames) */
  for (int i = 0; i < num_frames; i++) {
    buf = net_buf_alloc(&large_pool, K_NO_WAIT);
    if (!buf) {
      break;
    }

    /* Create audio frame */
    frame = (struct stream_frame *)net_buf_add(buf, sizeof(*frame) +
                                                        audio_frame_size);
    frame->sequence = sequence++;
    frame->timestamp = k_uptime_get_32();
    frame->frame_size = audio_frame_size;
    frame->flags = 0;

    /* Fill with sine wave pattern */
    for (size_t j = 0; j < audio_frame_size; j++) {
      frame->data[j] = (uint8_t)(128 + 127 * sinf(2 * 3.14159f * j / 64));
    }

    /* Distribute to consumers */
    ret = flow_source_send(&stream_producer, buf, K_NO_WAIT);
    net_buf_unref(buf);
    zassert_equal(ret, 3, "Should deliver to 3 stream sinks");

    /* Small delay between frames */
    k_busy_wait(100);
  }

  /* Process all events */
  process_all_events();

  /* Verify both consumers received all frames */
  int count1 = atomic_get(&stream_consumer1_capture.count);
  int count2 = atomic_get(&stream_consumer2_capture.count);
  int buffer_count = atomic_get(&stream_buffer_capture.count);

  zassert_equal(count1, num_frames, "Consumer1 should receive all frames");
  zassert_equal(count2, num_frames, "Consumer2 should receive all frames");
  zassert_equal(buffer_count, num_frames, "Buffer should receive all frames");
  zassert_equal(count1, count2,
                "Consumers should receive same number of frames");

  TC_PRINT("Distributed %d audio frames to each consumer\n", count1);
}

/* Test: Video frame chunking */
ZTEST(flow_integration, test_video_frame_distribution) {
  struct net_buf *buf;
  struct stream_frame *frame;
  int ret;
  const size_t chunk_size = 4000; /* Just under 4KB */
  const int chunks_per_frame = 6; /* 24KB frame */
  uint32_t frame_num = 0;

  /* Send one video frame as multiple chunks */
  for (int chunk = 0; chunk < chunks_per_frame; chunk++) {
    buf = net_buf_alloc(&jumbo_pool, K_NO_WAIT);
    if (!buf) {
      TC_PRINT("Failed to allocate buffer for chunk %d\n", chunk);
      break;
    }

    frame =
        (struct stream_frame *)net_buf_add(buf, sizeof(*frame) + chunk_size);
    frame->sequence = (frame_num << 16) | chunk;
    frame->timestamp = k_uptime_get_32();
    frame->frame_size = chunk_size;
    frame->flags = (chunk == 0) ? 0x01 : 0; /* Start of frame */
    frame->flags |= (chunk == chunks_per_frame - 1) ? 0x02 : 0; /* End */

    /* Fill with video data pattern */
    fill_random_data(frame->data, chunk_size);

    ret = flow_source_send(&stream_producer, buf, K_NO_WAIT);
    net_buf_unref(buf);
    if (ret <= 0) {
      TC_PRINT("Failed to send video chunk %d, ret=%d\n", chunk, ret);
    }
    zassert_true(ret > 0, "Failed to send video chunk %d", chunk);
  }

  /* Process all events */
  process_all_events();

  /* Verify buffer sink received all chunks */
  int chunks_received = atomic_get(&stream_buffer_capture.count);

  zassert_equal(chunks_received, chunks_per_frame,
                "Should receive all %d chunks", chunks_per_frame);
}

/*
 * ===========================================================================
 * 3. BULK DATA TRANSFER TESTS
 * ===========================================================================
 */

/* Test: Firmware update distribution */
ZTEST(flow_integration, test_firmware_update_distribution) {
  struct net_buf *buf;
  struct bulk_chunk *chunk;
  int ret;
  const size_t chunk_size = 512; /* Typical flash page */
  const size_t total_size =
      16 * 1024; /* 16KB firmware (smaller for faster test) */
  uint32_t offset = 0;
  int chunks_sent = 0;

  /* Send firmware in chunks */
  while (offset < total_size) {
    buf = net_buf_alloc(&large_pool, K_NO_WAIT);
    if (!buf) {
      break;
    }

    chunk = (struct bulk_chunk *)net_buf_add(buf, sizeof(*chunk) + chunk_size);
    chunk->offset = offset;
    chunk->total_size = total_size;
    chunk->chunk_size = chunk_size;

    /* Fill with firmware data */
    for (size_t i = 0; i < chunk_size; i++) {
      chunk->data[i] = (uint8_t)((offset + i) & 0xFF);
    }
    chunk->crc16 = calculate_crc16(chunk->data, chunk_size);

    ret = flow_source_send(&bulk_source, buf, K_NO_WAIT);
    net_buf_unref(buf);
    if (ret > 0) {
      chunks_sent++;
      offset += chunk_size;
    }

    /* Throttle to simulate flash write speed */
    k_busy_wait(1000); /* 1ms per chunk */
  }

  /* Process all events */
  process_all_events();

  /* Verify processor received chunks */
  int chunks_processed = atomic_get(&bulk_processor_capture.count);
  int chunks_stored = atomic_get(&bulk_storage_capture.count);

  TC_PRINT("Sent %d chunks, processed %d chunks, stored %d chunks (%.1f KB)\n",
           chunks_sent, chunks_processed, chunks_stored,
           (double)(chunks_processed * 0.5f));
  zassert_true(chunks_processed > 0, "Should process firmware chunks");
  zassert_equal(chunks_processed, chunks_stored,
                "All processed chunks should be stored");
}

/* Test: Large file transfer */
ZTEST(flow_integration, test_file_transfer_pipeline) {
  struct net_buf *buf;
  int ret;
  const size_t block_size = 1400; /* Close to MTU */
  const int num_blocks = 20;
  uint8_t *data;
  int sent = 0;

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

    ret = flow_source_send(&bulk_source, buf, K_NO_WAIT);
    net_buf_unref(buf);
    if (ret > 0) {
      sent++;
    }
  }

  /* Process all events */
  process_all_events();

  /* Verify storage received blocks */
  int stored = atomic_get(&bulk_storage_capture.count);

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
ZTEST(flow_integration, test_packet_fragmentation_chain) {
  struct net_buf *head, *frag;
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
  ret = flow_source_send(&eth_rx_source, head, K_NO_WAIT);
  net_buf_unref(head);
  zassert_true(ret > 0, "Failed to send chained buffer");

  /* Process events */
  process_all_events();

  /* Verify receipt */
  zassert_true(atomic_get(&eth_filter_capture.count) > 0,
               "Should receive chained buffer");
  zassert_true(atomic_get(&eth_logger_capture.count) > 0,
               "Logger should receive chained buffer");

  TC_PRINT("Handled chain with %d fragments\n", num_frags);
}

/*
 * ===========================================================================
 * 5. PERFORMANCE AND STRESS TESTS
 * ===========================================================================
 */

/* Test: Sustained throughput measurement */
ZTEST(flow_integration, test_sustained_throughput) {
  struct net_buf *buf;
  int ret;
  const size_t packet_size = 1500;
  const uint32_t target_packets = 500; /* Reduced for faster test */
  uint32_t start, elapsed;
  uint64_t bytes_sent = 0;
  uint32_t packets_sent = 0;

  start = k_uptime_get_32();

  TC_PRINT("Starting sustained throughput test for %u packets...\n",
           target_packets);

  /* Send target number of packets */
  while (packets_sent < target_packets) {
    buf = net_buf_alloc(&large_pool, K_NO_WAIT);
    if (!buf) {
      /* Process events to free up buffers */
      process_all_events();
      k_yield();
      continue;
    }

    uint8_t *data = net_buf_add(buf, packet_size);
    memset(data, packets_sent & 0xFF, packet_size);

    ret = flow_source_send(&perf_source1, buf, K_NO_WAIT);
    net_buf_unref(buf);
    if (ret > 0) {
      bytes_sent += packet_size;
      packets_sent++;
    }

    /* Periodically process events */
    if (packets_sent % 50 == 0) {
      process_all_events();
    }
  }

  /* Process remaining events */
  process_all_events();

  elapsed = k_uptime_get_32() - start;
  if (elapsed == 0) {
    elapsed = 1; /* Avoid division by zero */
  }

  /* Calculate throughput */
  float throughput_mbps = (bytes_sent * 8.0f) / (elapsed * 1000.0f);
  float packet_rate = (packets_sent * 1000.0f) / elapsed;

  TC_PRINT("Throughput: %.2f Mbps, %.0f packets/sec\n", (double)throughput_mbps,
           (double)packet_rate);
  TC_PRINT("Sent %u packets (%llu bytes) in %u ms\n", packets_sent, bytes_sent,
           elapsed);

  int received = atomic_get(&perf_capture.count);
  TC_PRINT("Received %d packets (%.1f%% delivery)\n", received,
           (double)((received * 100.0f) / packets_sent));

  zassert_true(packets_sent > 0, "Should send packets");
  zassert_true(received > 0, "Should receive packets");
  zassert_true(throughput_mbps > 0, "Should achieve some throughput");
}

/* Test: Concurrent high throughput from multiple sources */
ZTEST(flow_integration, test_concurrent_high_throughput) {
  struct net_buf *buf;
  const size_t packet_size = 1024;
  const int packets_per_source = 50; /* Reduced for faster test */
  int sent[3] = {0, 0, 0};
  int ret;

  /* Three sources send concurrently */
  for (int i = 0; i < packets_per_source; i++) {
    /* Source 1 */
    buf = net_buf_alloc(&large_pool, K_NO_WAIT);
    if (buf) {
      net_buf_add(buf, packet_size);
      ret = flow_source_send(&perf_source1, buf, K_NO_WAIT);
      net_buf_unref(buf);
      if (ret > 0) {
        sent[0]++;
      }
    }

    /* Source 2 */
    buf = net_buf_alloc(&large_pool, K_NO_WAIT);
    if (buf) {
      net_buf_add(buf, packet_size);
      ret = flow_source_send(&perf_source2, buf, K_NO_WAIT);
      net_buf_unref(buf);
      if (ret > 0) {
        sent[1]++;
      }
    }

    /* Source 3 */
    buf = net_buf_alloc(&large_pool, K_NO_WAIT);
    if (buf) {
      net_buf_add(buf, packet_size);
      ret = flow_source_send(&perf_source3, buf, K_NO_WAIT);
      net_buf_unref(buf);
      if (ret > 0) {
        sent[2]++;
      }
    }

    /* Process events periodically */
    if (i % 10 == 0) {
      process_all_events();
      k_yield();
    }
  }

  /* Process all remaining events */
  process_all_events();

  TC_PRINT("Concurrent sends - Source1: %d, Source2: %d, Source3: %d\n",
           sent[0], sent[1], sent[2]);

  /* Count total received */
  int received = atomic_get(&perf_capture.count);
  int total_sent = sent[0] + sent[1] + sent[2];

  TC_PRINT("Total: sent %d, received %d (%.1f%% delivery)\n", total_sent,
           received, (double)((received * 100.0f) / total_sent));

  zassert_true(received > 0, "Should receive packets");
  zassert_true(received <= total_sent, "Cannot receive more than sent");
}

/* Test: Protocol translation */
ZTEST(flow_integration, test_protocol_translation) {
  struct net_buf *uart_buf, *eth_buf;
  struct ethernet_frame *eth_frame;
  int ret;
  const uint8_t uart_data[] = "Hello from UART!";
  const size_t uart_len = sizeof(uart_data);

  /* Simulate UART data reception */
  uart_buf = net_buf_alloc(&small_pool, K_NO_WAIT);
  zassert_not_null(uart_buf, "Failed to allocate UART buffer");

  memcpy(net_buf_add(uart_buf, uart_len), uart_data, uart_len);

  /* Translate to Ethernet format */
  eth_buf = net_buf_alloc(&large_pool, K_NO_WAIT);
  zassert_not_null(eth_buf, "Failed to allocate Ethernet buffer");

  /* Build Ethernet frame with UART data as payload */
  eth_frame = (struct ethernet_frame *)net_buf_add(eth_buf, sizeof(*eth_frame) +
                                                                uart_len);
  memset(eth_frame->dst_mac, 0xFF, 6);  /* Broadcast */
  memset(eth_frame->src_mac, 0x42, 6);  /* Bridge MAC */
  eth_frame->ethertype = htons(0x88B5); /* Local experimental */
  memcpy(eth_frame->payload, uart_data, uart_len);

  /* Release UART buffer */
  net_buf_unref(uart_buf);

  /* Send through Ethernet TX */
  ret = flow_source_send(&eth_tx_source, eth_buf, K_NO_WAIT);
  net_buf_unref(eth_buf);
  zassert_true(ret > 0, "Failed to send bridged packet");

  /* Process events */
  process_all_events();

  /* Verify router received it */
  zassert_true(atomic_get(&eth_router_capture.count) > 0,
               "Router should receive bridged packet");
  zassert_equal(eth_router_capture.last_value, htons(0x88B5),
                "Wrong ethertype");

  TC_PRINT("Successfully bridged UART to Ethernet\n");
}

/* Test: Latency under load */
ZTEST(flow_integration, test_latency_under_load) {
  struct net_buf *buf;
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
        flow_source_send(&perf_source2, buf, K_NO_WAIT);
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
    ret = flow_source_send(&perf_source1, buf, K_NO_WAIT);
    net_buf_unref(buf);

    if (ret > 0) {
      /* Process events and measure latency */
      process_all_events();
      uint32_t rx_time = k_cycle_get_32();
      latencies[samples] = rx_time - timestamps[i];
      samples++;
    }
  }

  if (samples > 0) {
    /* Calculate average latency */
    uint64_t total = 0;
    uint32_t min = UINT32_MAX, max = 0;

    for (int i = 0; i < samples; i++) {
      total += latencies[i];
      if (latencies[i] < min) {
        min = latencies[i];
      }
      if (latencies[i] > max) {
        max = latencies[i];
      }
    }

    uint32_t avg = total / samples;
    uint32_t freq = sys_clock_hw_cycles_per_sec();

    TC_PRINT("Latency - Avg: %u us, Min: %u us, Max: %u us\n",
             (avg * 1000000) / freq, (min * 1000000) / freq,
             (max * 1000000) / freq);
  }

  zassert_true(samples > 0, "Should measure some latencies");
}

/* Test: Memory pool exhaustion handling */
ZTEST(flow_integration, test_memory_pool_exhaustion) {
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
      ret = flow_source_send(&bulk_source, buf, K_NO_WAIT);
      net_buf_unref(buf);
      if (ret > 0) {
        sent++;
      }
    }
  }

  process_all_events();

  zassert_true(sent > 0, "Should send after freeing buffers");
  TC_PRINT("Sent %d packets after partial free\n", sent);

  /* Clean up remaining buffers */
  for (int i = 0; i < 32; i++) {
    if (bufs[i]) {
      net_buf_unref(bufs[i]);
    }
  }
}