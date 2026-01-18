# Flow - Fast Lightweight Object Wiring

[![Zephyr Version](https://img.shields.io/badge/zephyr-v3.7.1-blue)](https://github.com/zephyrproject-rtos/zephyr)
[![License](https://img.shields.io/badge/license-Apache%202.0-green)](LICENSE)

Flow (Fast Lightweight Object Wiring) is a high-performance, thread-safe, zero-copy packet distribution framework for Zephyr RTOS. Based on the **source/sink pattern**, sources (producers) send `net_buf` packets to multiple sinks (consumers) without data copying, using reference counting for efficient many-to-many communication.

## 🚀 Features

- **Many-to-Many Routing**: Sources can send to multiple sinks, sinks can receive from multiple sources
- **Packet ID Filtering**: Sources can stamp packets with IDs, sinks can filter by specific IDs
- **Flexible Execution Modes**: Immediate (in source context) or queued (deferred processing) handler based processing
- **Zero-Copy Distribution**: Efficient packet sharing using `net_buf` reference counting
- **Protocol Packaging**: Chain buffers to add headers/footers without copying payload data
- **Static & Runtime Wiring**: Compile-time connections for performance, runtime for flexibility
- **Overflow Protection**: Automatic packet dropping when queues are full, with statistics tracking

## 📋 Quick Start

### Prerequisites

- Zephyr RTOS v3.7.1+
- West build tool

### Simple Example

```c
#include <zephyr_io/flow/flow.h>

// 1. Define a buffer pool for packet allocation
FLOW_BUF_POOL_DEFINE(sensor_pool, 16, 128, NULL);  // 16 buffers, 128 bytes each

// 2. Define a packet source (producer)
FLOW_SOURCE_DEFINE(sensor_source);

// 3. Define a handler function to process packets
void log_handler(struct flow_sink *sink, struct net_buf *buf_ref)
{
    LOG_INF("Received %d bytes", buf_ref->len);
}

// 4. Define a sink (consumer) with the handler
FLOW_SINK_DEFINE_IMMEDIATE(log_sink, log_handler);

// 5. Connect source to sink at compile time
FLOW_CONNECT(&sensor_source, &log_sink);

// 6. Send packets at runtime
void send_sensor_data(void)
{
    struct net_buf *buf = flow_buf_alloc(&sensor_pool, K_NO_WAIT);
    if (buf) {
        net_buf_add_mem(buf, sensor_data, sizeof(sensor_data));
        flow_source_send(&sensor_source, buf, K_NO_WAIT);
    }
}
```

### Deferred Processing with Event Queues

For processing packets in a separate thread, use queued sinks:

```c
// Define an event queue for deferred processing
FLOW_EVENT_QUEUE_DEFINE(process_queue, 32);  // Max 32 events

// Handler function
void process_handler(struct flow_sink *sink, struct net_buf *buf_ref)
{
    LOG_INF("Processing %d bytes in worker thread", buf_ref->len);
}

// Define a queued sink (handler runs in processor thread)
FLOW_SINK_DEFINE_QUEUED(queued_sink, process_handler, &process_queue);

// Connect source to queued sink
FLOW_CONNECT(&sensor_source, &queued_sink);

// Processing thread
void processor_thread(void)
{
    while (1) {
        // Process one event from the queue (blocks until available)
        flow_event_process(&process_queue, K_FOREVER);
    }
}
K_THREAD_DEFINE(processor, 1024, processor_thread, NULL, NULL, NULL, 5, 0, 0);
```

### Packet ID Routing

```c
// Define a single source that accepts any packet ID
FLOW_SOURCE_DEFINE(data_source);

// Define sinks that filter by specific packet IDs
FLOW_SINK_DEFINE_ROUTED_IMMEDIATE(type1_handler, process_type1, 0x01);
FLOW_SINK_DEFINE_ROUTED_IMMEDIATE(type2_handler, process_type2, 0x02);
FLOW_SINK_DEFINE_IMMEDIATE(all_handler, process_all); // Accepts any ID

// Connect source to all sinks - filtering happens at sink level
FLOW_CONNECT(&data_source, &type1_handler);
FLOW_CONNECT(&data_source, &type2_handler);
FLOW_CONNECT(&data_source, &all_handler);

// Send packets with different IDs at runtime
void send_typed_data(uint8_t packet_type, void *data, size_t len)
{
    struct net_buf *buf = flow_buf_alloc_with_id(&pool, packet_type, K_NO_WAIT);
    if (buf) {
        net_buf_add_mem(buf, data, len);
        flow_source_send(&data_source, buf, K_NO_WAIT);
        // type1_handler receives only if packet_type == 0x01
        // type2_handler receives only if packet_type == 0x02
        // all_handler receives all packets regardless of type
    }
}
```

### Sample Application

See `flow/samples/router_sample/` for a complete integration example demonstrating:
- TCP server with packet streaming
- Protocol header addition/removal
- Command processing pipeline
- Multi-sensor data collection

## 🚧 Limitations

- **Native buffer pools**: Designed for `net_buf` - other buffer types require adaptation
- **Handler ownership**: Handlers receive borrowed buffers - must NOT call `net_buf_unref()`

## 🛠️ Building and Testing

```bash
# Run all tests and sample
ZEPHYR_EXTRA_MODULES=$PWD/flow \
  west twister \
  -T flow -p native_sim -v -O twister-out --no-clean

# Build sample application
ZEPHYR_EXTRA_MODULES=$PWD/flow \
  west build -p always -b native_sim -d build_sample \
  flow/samples/basic_packet_routing

# Run sample
./build_sample/zephyr/zephyr.exe
```

## 📖 Documentation

- **[User Guide](flow/doc/index.rst)**: Comprehensive documentation with concepts, usage patterns, and examples
- **[API Reference](flow/include/zephyr_io/flow/flow.h)**: Function reference and detailed API documentation
- **[Sample Code](flow/samples/)**: Complete integration examples
- **[Test Suite](flow/tests/)**: Comprehensive unit and integration tests

## 🤝 Contributing

1. **Build Requirements**: Ensure Zephyr v3.7.1+ and Python virtual environment
2. **Testing**: All tests must pass before submitting changes
3. **Code Style**: Follow Zephyr coding conventions
4. **Documentation**: Update relevant documentation for API changes

---

*Built for Zephyr RTOS with ❤️*