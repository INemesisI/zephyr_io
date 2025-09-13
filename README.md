# Packet I/O - Zero-Copy Packet Distribution for Zephyr RTOS

[![Zephyr Version](https://img.shields.io/badge/zephyr-v3.7.1-blue)](https://github.com/zephyrproject-rtos/zephyr)
[![License](https://img.shields.io/badge/license-Apache%202.0-green)](LICENSE)

A high-performance, zero-copy packet distribution system for Zephyr RTOS that enables efficient many-to-many packet routing using `net_buf` reference counting.

## 🚀 Features

- **Zero-Copy Distribution**: Efficient packet sharing using `net_buf` reference counting
- **Many-to-Many Routing**: Sources can send to multiple sinks, sinks can receive from multiple sources
- **Static Compile-Time Wiring**: All connections defined at compile time for optimal performance
- **Configurable Queue Policies**: Drop-on-full or block-on-full per sink
- **Timeout Support**: Configurable blocking behavior with fair timeout handling
- **Thread-Safe**: Built with Zephyr's `k_spinlock` for safe concurrent access

## 📋 Quick Start

### Prerequisites

- Zephyr RTOS v3.7.1+
- West build tool
- Python virtual environment with required dependencies

### Basic Usage

```c
#include <zephyr/packet_io/packet_io.h>

// Define components
PACKET_SOURCE_DEFINE(sensor_source);
PACKET_SINK_DEFINE(tcp_sink, 10, true);   // Queue size 10, drop on full
PACKET_SINK_DEFINE(udp_sink, 5, false);   // Queue size 5, block on full

// Wire connections (compile-time)
PACKET_SOURCE_CONNECT(sensor_source, tcp_sink);
PACKET_SOURCE_CONNECT(sensor_source, udp_sink);

// Send packets (runtime)
int delivered = packet_source_send(&sensor_source, packet, K_MSEC(100));
```

### Sample Application

See `packet_io/samples/basic_packet_routing/` for a complete integration example demonstrating:
- Multi-sensor data collection
- Packet processing with header addition
- Zero-copy distribution to multiple network sinks

```
Packet flow:
  sensor1 ─┐                    ┌─→ TCP sink (reliable, 50ms TX)
           ├─→ processor node ──┤
  sensor2 ─┘                    └─→ UDP sink (fast, 10ms TX)
```

## 🚧 Limitations

- **Static connections only**: All source-to-sink connections must be defined at compile time
- **Queue-based flow control**: Backpressure handled via configurable queue sizes and drop policies
- **Native buffer pools**: Designed for `net_buf` - other buffer types require adaptation
- **Single packet per send**: No scatter-gather or batch operations
- **Platform requirements**: Requires Zephyr's iterable sections and atomic operations support

## 🛠️ Building and Testing

```bash
# Run all tests and sample
ZEPHYR_EXTRA_MODULES=$PWD/packet_io \
PYTHON_PREFER=$PWD/.venv/bin/python3 CMAKE_PREFIX_PATH=$PWD/.venv \
  .venv/bin/python zephyr/scripts/twister \
  -T packet_io -p native_sim -v -O twister-out --no-clean

# Build sample application
ZEPHYR_EXTRA_MODULES=$PWD/packet_io \
  .venv/bin/west build -p always -b native_sim -d build_sample \
  packet_io/samples/basic_packet_routing

# Run sample
./build_sample/zephyr/zephyr.exe
```

## 📖 Documentation

- **[API Documentation](packet_io/include/zephyr/packet_io/packet_io.h)**: Function reference and usage examples
- **[Sample Code](packet_io/samples/)**: Complete integration examples
- **[Test Suite](packet_io/tests/)**: Comprehensive unit and integration tests

## 🤝 Contributing

1. **Build Requirements**: Ensure Zephyr v3.7.1+ and Python virtual environment
2. **Testing**: All tests must pass before submitting changes
3. **Code Style**: Follow Zephyr coding conventions
4. **Documentation**: Update relevant documentation for API changes

---

*Built for Zephyr RTOS with ❤️*