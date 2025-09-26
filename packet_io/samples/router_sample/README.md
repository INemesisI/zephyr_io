# Packet Router Sample

Advanced packet routing demonstration showcasing the power of the packet_io subsystem with a reusable, protocol-agnostic routing framework.

## Overview

This sample demonstrates a sophisticated packet routing architecture with:

- **Generic routing framework** - Reusable for any protocol
- **Bidirectional transformation** - Add/strip protocol headers transparently
- **Zero-copy operation** - Leverages packet_io's reference counting
- **Real-world networking** - TCP server integration with fragmented packet handling
- **Compile-time wiring** - Static routing tables with minimal overhead

## Architecture

### Two-Layer Design

```
┌──────────────────────────────────────────────────────────┐
│                   Application Layer                      │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────┐    │
│  │ Temp     │  │ LED      │  │ System   │  │ TCP    │    │
│  │ Sensor   │  │ Control  │  │ Control  │  │ Server │    │
│  └─────┬────┘  └────▲─────┘  └─────▲────┘  └────▲───┘    │
│        │            │              │            │        │
├────────┼────────────┼──────────────┼────────────┼────────┤
│        │     Protocol Router Layer │            │        │
│        │            │              │            │        │
│  ┌─────▼────────────┴──────────────▼────────────▼─────┐  │
│  │            IoTSense Protocol Router                │  │
│  │  - Adds/strips 4-byte headers                      │  │
│  │  - Routes by packet ID                             │  │
│  │  - Built on generic framework                      │  │
│  └─────▲────────────▲──────────────▲────────────▲─────┘  │
│        │            │              │            │        │
├────────┼────────────┼──────────────┼────────────┼────────┤
│        │     Generic Router Framework           │        │
│        │            │              │            │        │
│  ┌─────▼────────────▼──────────────▼────────────▼─────┐  │
│  │         packet_router.c/h (Reusable)               │  │
│  │  - Protocol-agnostic routing engine                │  │
│  │  - Compile-time route registration                 │  │
│  │  - Bidirectional packet transformation             │  │
│  └────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────┘
```

### Packet Flow

**Outbound (Sensor → Network):**

```
Sensor generates data → Router adds header → TCP server transmits
     [payload]      →    [hdr|payload]    →    Network
```

**Inbound (Network → Actuator):**

```
Network packet → Router strips header → LED controller processes
  [hdr|payload] →      [payload]      →    Toggle LED
```

## Key Concepts Demonstrated

### 1. Protocol-Agnostic Framework

The generic `packet_router` framework (`src/framework/`) can be reused for any protocol. It provides:

- Route registration macros
- Header manipulation hooks
- Statistics tracking
- Zero-copy packet distribution

### 2. Static Routing Tables

Routes are defined at compile-time using macros:

```c
// Inbound: Network packets with ID 0x02 go to LED controller
ROUTER_INBOUND_ROUTE_DEFINE(iotsense_router, 0x02, led_controller_sink);

// Outbound: Temperature sensor packets get ID 0x01 header
ROUTER_OUTBOUND_ROUTE_DEFINE(iotsense_router, 0x01, temperature_sensor_source);
```

### 3. IoTSense Protocol

Simple 4-byte header demonstration:

```
┌──────────┬───────────┬──────────────┐
│ Version  │ Packet ID │ Payload Len  │
│ (1 byte) │ (1 byte)  │  (2 bytes)   │
└──────────┴───────────┴──────────────┘
```

### 4. Zero-Copy Distribution

Multiple sinks can receive the same packet without copying:

- Router increments reference count for each sink
- Each sink releases its reference when done
- Buffer freed when last reference released

## Building and Running

### Native Simulator (Default)

```bash
# Build for native_sim (uses host networking)
west build -b native_sim packet_io/samples/router_sample

# Run the sample
./build/zephyr/zephyr.exe
```

The build system automatically selects the appropriate configuration from the `boards/` directory.

## Testing

### TCP Client Tool

A Python client is provided for testing:

```bash
# Run demo sequence
python3 tools/tcp_client.py

# Send ping command
python3 tools/tcp_client.py ping

# Toggle LED
python3 tools/tcp_client.py led

# Monitor sensor data
python3 tools/tcp_client.py monitor --time 30
```

### Automated Tests

```bash
# Run pytest suite
pytest tests/test_router_tcp.py
```

## Project Structure

```
router_sample/
├── boards/              # Board-specific configurations
│   ├── native_sim.conf  # Native simulator settings
├── src/
│   ├── framework/       # Generic routing framework (reusable)
│   │   ├── packet_router.h
│   │   └── packet_router.c
│   ├── protocols/       # Protocol implementations
│   │   ├── iotsense_router.h
│   │   └── iotsense_router.c
│   ├── modules/         # Application modules
│   │   ├── temperature_sensor.c/h
│   │   ├── tcp_server.c/h
│   │   ├── system_control.c/h
│   │   └── led_controller.c/h
│   └── main.c
├── tools/               # Testing utilities
│   └── tcp_client.py
├── tests/               # Test suite
│   └── test_router_tcp.py
├── prj.conf            # Platform-agnostic configuration
├── CMakeLists.txt
├── testcase.yaml
└── sections-rom.ld     # Linker sections for routing tables
```

## Configuration Options

Key options in `prj.conf`:

- `CONFIG_PACKET_IO_STATS=y` - Enable statistics tracking
- `CONFIG_PACKET_IO_NAMES=y` - Debug names for sources/sinks
- `CONFIG_PACKET_IO_LOG_LEVEL=2` - Logging verbosity

## Performance

The sample demonstrates:

- **Zero-copy operation** - No data copying during routing
- **Compile-time wiring** - No runtime overhead for route lookup
- **Lock-free packet distribution** - Using packet_io's atomic operations
- **Efficient buffer management** - Reference counting prevents leaks

## See Also

- [packet_io Design Document](../../packet_io_design.md)
- [Basic Packet Routing Sample](../basic_packet_routing/)
- [Zephyr Networking Documentation](https://docs.zephyrproject.org/latest/connectivity/networking/)
