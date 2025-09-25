# Packet Router Sample

Demonstrates a generalized packet router with protocol-agnostic design.

## Architecture

### Components

1. **Generic Router** (`packet_router.h/c`) - Protocol-agnostic routing framework
2. **IoTSense Router** (`iotsense_router.h/c`) - IoTSense protocol implementation with 4-byte headers and packet IDs
3. **Application Modules** - Independent packet producers/consumers

### Application Modules

- **Temperature Sensor** - Outbound-only, ID 0x01, periodic data
- **System Control** - Bidirectional, ID 0x09, ping/pong only
- **LED Controller** - Inbound-only, ID 0x02, simple toggle
- **TCP Server** - Network interface, handles client connections and packet transmission

## Key Features

### Protocol-Specific Implementation

The router is application-specific, owning its network interfaces and protocol handling:

```c
ROUTER_DEFINE(iotsense_router, inbound_handler, outbound_handler);
```

### Route Definition Macros
```c
ROUTER_INBOUND_ROUTE_DEFINE(router, packet_id, app_sink);
ROUTER_OUTBOUND_ROUTE_DEFINE(router, packet_id, app_source);
```

### Sparse Packet ID Support

Handles non-contiguous IDs (1, 9, 200, 500) efficiently with O(n) lookup.

### Zero-Copy Operation

Header buffers are chained with payload buffers without copying data.

## Packet Flow

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│  Temperature    │───▶│   IoTSense       │───▶│   TCP Server    │
│  Sensor         │    │   Router         │    │   Port 8080     │
│  (ID: 0x01)     │    │                  │    │       ↕         │
└─────────────────┘    │                  │    │   TCP Client    │
                       │                  │    │                 │
┌─────────────────┐    │                  │    │                 │
│  System         │◄──▶│                  │◄──▶│                 │
│  Control        │    │                  │    │                 │
│  (ID: 0x09)     │    │                  │    │                 │
└─────────────────┘    │                  │    │                 │
                       │                  │    │                 │
┌─────────────────┐    │                  │    │                 │
│  LED            │◄───│                  │◄───│                 │
│  Controller     │    │                  │    │                 │
│  (ID: 0x02)     │    │                  │    │                 │
└─────────────────┘    └──────────────────┘    └─────────────────┘
```

## Building and Running

```bash
# Set up environment
export ZEPHYR_EXTRA_MODULES=$PWD/packet_io

# Build the sample
west build -p always -b native_sim packet_io/samples/router_sample

# Run the sample
./build/zephyr/zephyr.exe
```

## Expected Output

```
Packet Router Sample - Simplified IoTSense Protocol
[00:00:00.000] <inf> tcp_server: TCP server listening on port 8080
[00:00:00.000] <inf> packet_router: Router ready: 2 inbound, 2 outbound routes
[00:00:00.000] <inf> iotsense_router: IoTSense router initialized
[00:00:00.000] <inf> tcp_server: TCP server listening on port 8080
[00:00:00.000] <inf> temp_sensor: Temperature sensor started
[00:00:00.000] <inf> temp_sensor: Temp: 25.00°C, Humidity: 48.24% (sample #1)
[00:00:00.000] <inf> tcp_server: TCP TX: packet_id=0x0001, 12 bytes

# When TCP client connects and sends ping:
[00:00:05.000] <inf> tcp_server: TCP RX: packet_id=0x0009, 3 bytes
[00:00:05.001] <inf> sys_control: Ping #1 (seq=0)
[00:00:05.002] <inf> tcp_server: TCP TX: packet_id=0x0009, 7 bytes

# When LED toggle command received:
[00:00:08.000] <inf> tcp_server: TCP RX: packet_id=0x0002, 1 bytes
[00:00:08.001] <inf> led_controller: LED toggle #1: ON
```

## 🧪 Testing with TCP Client

The sample includes a real TCP server on port 8080. You can:

1. **Connect with Python test client**: Use the pytest tests in `pytest/` directory
2. **Temperature Data**: Automatically sent every 200ms to connected clients
3. **System Commands**: Send ping commands and receive pong responses
4. **LED Control**: Send toggle commands to switch LED state
5. **Error Handling**: Unknown packet IDs are gracefully dropped
6. **Statistics**: Router performance metrics every 30 seconds

### Quick Demo with Python Client

A standalone Python client is provided for easy testing:

```bash
# In terminal 1: Start the router sample
./build/zephyr/zephyr.exe

# In terminal 2: Run the demo client
cd packet_io/samples/router_sample
python3 tcp_client_demo.py --demo

# Or run interactive mode:
python3 tcp_client_demo.py
```

The demo script demonstrates:
- Receiving temperature sensor data
- Sending ping commands and receiving pong responses
- LED toggle commands
- Interactive mode for manual testing

### Running Automated Tests

```bash
# Or run just the router sample in isolation (with venv activated)
# Set pytest alias to avoid plugin conflicts
alias pytest="python -m pytest"
ZEPHYR_EXTRA_MODULES=$PWD/packet_io \
  west twister -T packet_io/samples/router_sample -p native_sim \
  -O twister-out --no-clean -v
```

The main script automatically:
- Builds all packet_io tests and samples
- Runs unit tests, integration tests, and sample tests
- Includes the router sample pytest scenarios
- Provides a summary of all test results

## 🎯 Module Design Philosophy

### Module Independence

Modules only:
- Generate or consume application payloads
- Use packet_io sources/sinks for communication

Modules have no knowledge of:
- Network protocols or headers
- Other modules or their implementations
- Router internals or packet IDs

### Router Architecture

The system uses a two-layer router design:

**Generic Router Library** (`packet_router.h/c`):
- Protocol-agnostic routing framework
- Route registration and lookup
- Packet distribution mechanics
- Statistics and connection management

**Protocol-Specific Router** (`iotsense_router.c`):
- Provides sink/source for receiving/sending packets
- Implements protocol handling
  - Strips protocol from inbound packets and delivers payloads to application modules
  - Adds protocol to outbound payloads from  application modules
- Packet validation and error handling