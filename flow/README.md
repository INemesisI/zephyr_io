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

### Basic Usage

```c
#include <zephyr_io/flow/flow.h>

// Define a Flow IO source
FLOW_SOURCE_DEFINE(data_source);

// Flow IO sink handler function for processing packets
void process_handler(struct flow_sink *sink, struct net_buf *buf)
{
    LOG_INF("Received %d bytes", buf->len);
    // Buffer is borrowed - DO NOT call net_buf_unref()
}

// Define immediate execution sink (runs in source thread context)
FLOW_SINK_DEFINE_IMMEDIATE(immediate_sink, process_handler);

// Or define Flow IO event queue sink for deferred processing in another thread
FLOW_EVENT_QUEUE_DEFINE(udp_queue, 32);  // 32 events max
FLOW_SINK_DEFINE_QUEUED(queued_sink, process_handler,  &udp_queue);

// Wire connections
FLOW_CONNECT(&data_source, &immediate_sink);
FLOW_CONNECT(&data_source, &queued_sink);

// Send packets (runtime)
flow_source_send(&data_source, buf, K_MSEC(100));
```

### Packet ID Routing

```c
// Define routed sources with specific packet IDs
FLOW_SOURCE_DEFINE_ROUTED(sensor1_source, 0x1001);
FLOW_SOURCE_DEFINE_ROUTED(sensor2_source, 0x1002);

// Define routed sinks that filter by packet ID
FLOW_SINK_DEFINE_ROUTED_IMMEDIATE(sensor1_processor, handle_sensor1, 0x1001);
FLOW_SINK_DEFINE_ROUTED_IMMEDIATE(sensor2_processor, handle_sensor2, 0x1002);
FLOW_SINK_DEFINE_IMMEDIATE(all_sensors, handle_any_sensor); // Accepts all IDs

// Connect sources to sinks - filtering happens automatically
FLOW_CONNECT(&sensor1_source, &sensor1_processor);  // Only 0x1001 packets
FLOW_CONNECT(&sensor2_source, &sensor2_processor);  // Only 0x1002 packets
FLOW_CONNECT(&sensor1_source, &all_sensors);        // Accepts any ID
FLOW_CONNECT(&sensor2_source, &all_sensors);        // Accepts any ID

// Packets are automatically stamped with source's ID and filtered at sinks
flow_source_send(&sensor1_source, buf, K_NO_WAIT);  // Stamped with 0x1001
```

### Processing Thread for Queued Sinks

```c
// Processing thread for queued events
void processor_thread(void)
{
    while (1) {
        // Process events from the queue (will call the handler on event)
        int ret = flow_event_process(&queued_sink, K_FOREVER);
        if (ret != 0) {
            LOG_ERR("Failed to process event: %d", ret);
        }
    }
}
```

### Sample Application

See `flow/samples/basic_packet_routing/` for a complete integration example demonstrating:
- Multi-sensor data collection with packet ID-based routing
- Zero-copy header addition in processor node
- Echo service with packet loopback
- Incoming packet validation

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