# Packet I/O - Zero-Copy Packet Distribution for Zephyr RTOS

[![Zephyr Version](https://img.shields.io/badge/zephyr-v3.7.1-blue)](https://github.com/zephyrproject-rtos/zephyr)
[![License](https://img.shields.io/badge/license-Apache%202.0-green)](LICENSE)

A high-performance, thread-safe, zero-copy packet distribution framework for Zephyr RTOS. Based on the **source/sink pattern**, sources (producers) send `net_buf` packets to multiple sinks (consumers) without data copying, using reference counting for efficient many-to-many communication. 

## 🚀 Features

- **Many-to-Many Routing**: Sources can send to multiple sinks, sinks can receive from multiple sources
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
#include <zephyr/packet_io/packet_io.h>

// Define a packet source
PACKET_SOURCE_DEFINE(sensor_source);

// Handler function for processing packets
void process_handler(struct packet_sink *sink, struct net_buf *buf)
{
    LOG_INF("Received %d bytes", buf->len);
    // Buffer is borrowed - DO NOT call net_buf_unref()
}

// Define immediate execution sink (runs in source context)
PACKET_SINK_DEFINE_IMMEDIATE(tcp_sink, process_handler);

// Define packet event queue for deferred processing
PACKET_EVENT_QUEUE_DEFINE(udp_queue, 32);  // 32 events max
PACKET_SINK_DEFINE_QUEUED(udp_sink, process_handler, udp_queue);

// Wire connections (compile-time)
PACKET_SOURCE_CONNECT(sensor_source, tcp_sink);
PACKET_SOURCE_CONNECT(sensor_source, udp_sink);

// Send packets (runtime)
packet_source_send(&sensor_source, buf, K_MSEC(100));        // Preserve reference
packet_source_send_consume(&sensor_source, buf2, K_NO_WAIT); // Consume reference
```

### Processing Thread for Queued Sinks

```c
// Processing thread for queued events
void processor_thread(void)
{
    while (1) {
        // Process events from the queue
        int ret = packet_event_process(&udp_queue, K_FOREVER);
        if (ret != 0 && ret != -EAGAIN) {
            LOG_ERR("Failed to process event: %d", ret);
        }
    }
}
```

### Sample Application

See `packet_io/samples/basic_packet_routing/` for a complete integration example demonstrating:
- Multi-sensor data collection with event-driven processing
- Zero-copy header addition in processor node
- Distribution to multiple sinks with different execution modes
- Packet validation with immediate handlers

```
Packet flow:
  sensor1 ─┐                    ┌─→ TCP sink (queued processing)
           ├─→ processor node ──┤
  sensor2 ─┘    (adds header)   └─→ Validator (immediate execution)
```

## 🚧 Limitations

- **Native buffer pools**: Designed for `net_buf` - other buffer types require adaptation
- **Single packet per send**: No scatter-gather or batch operations
- **Handler ownership**: Handlers receive borrowed buffers - must NOT call `net_buf_unref()`

## 🛠️ Building and Testing

```bash
# Run all tests and sample
ZEPHYR_EXTRA_MODULES=$PWD/packet_io \
  python3 zephyr/scripts/twister \
  -T packet_io -p native_sim -v -O twister-out --no-clean

# Build sample application
ZEPHYR_EXTRA_MODULES=$PWD/packet_io \
  west build -p always -b native_sim -d build_sample \
  packet_io/samples/basic_packet_routing

# Run sample
./build_sample/zephyr/zephyr.exe
```

## 📖 Documentation

- **[User Guide](packet_io/doc/index.rst)**: Comprehensive documentation with concepts, usage patterns, and examples
- **[API Reference](packet_io/include/zephyr/packet_io/packet_io.h)**: Function reference and detailed API documentation
- **[Sample Code](packet_io/samples/)**: Complete integration examples
- **[Test Suite](packet_io/tests/)**: Comprehensive unit and integration tests

## 🤝 Contributing

1. **Build Requirements**: Ensure Zephyr v3.7.1+ and Python virtual environment
2. **Testing**: All tests must pass before submitting changes
3. **Code Style**: Follow Zephyr coding conventions
4. **Documentation**: Update relevant documentation for API changes

---

*Built for Zephyr RTOS with ❤️*