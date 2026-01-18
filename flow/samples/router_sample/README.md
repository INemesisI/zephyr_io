# Flow Router Sample - TCP-Controlled Sensor Data Router

Demonstrates a TCP server that routes sensor data to connected clients with remote control capabilities.

## Overview

This sample implements a network-accessible data router with the following components:
- **Sensors** - Generate data packets with metadata (packet ID, counter, timestamp)
- **Protocol** - Adds protocol headers to packets for network transmission
- **TCP Server** - Sends packets to connected clients and receives control commands
- **Command Handler** - Processes start/stop sampling commands

```
Data Flow:

┌─────────────┐                      ┌─────────────┐
│   SENSORS   │  Raw sensor data     │  PROTOCOL   │
│             │ ──────────────────→  │             │
│  Sensor 1   │  with metadata       │  - Add 14B  │
│  Sensor 2   │  (ID, counter, ts)   │    header   │
└─────────────┘                      │  - Forward  │
                                     └──────┬──────┘
                                            │
                                     Packets with
                                     protocol header
                                            ↓
                                     ┌─────────────┐     TCP Socket
                                     │ TCP SERVER  │ ←──────────────→ Python Client
                                     │             │                  (tcp_client.py)
                                     │  Port 4242  │
                                     └──────┬──────┘
                                            │
                                     Command packets
                                     (start/stop)
                                            ↓
                                     ┌─────────────┐
                                     │   COMMAND   │
                                     │   HANDLER   │
                                     │             │
                                     │ - Start/Stop│
                                     │   sampling  │
                                     └─────────────┘

Protocol Header Format (14 bytes):
┌──────────┬──────────┬─────────┬───────────────┬──────────────┐
│ PacketID │ Reserved │ Counter │ ContentLength │ TimestampNS  │
│ (1 byte) │ (1 byte) │(2 bytes)│   (2 bytes)   │  (8 bytes)   │
└──────────┴──────────┴─────────┴───────────────┴──────────────┘
```

## Features

- **TCP Server**: Listens on port 4242 for client connections
- **Remote Control**: Start/stop sensor sampling via TCP commands
- **High-Resolution Timestamps**: 64-bit nanosecond timestamps in packet headers
- **Zero-Copy Distribution**: Uses Flow framework's efficient buffer management
- **Metadata Propagation**: Packet ID, counter, and timestamp metadata
- **Python Client**: Interactive CLI and library for testing

## Building and Running

### Build the Sample
```bash
# From the Zephyr workspace root
cd /home/beliasson/projects/zephyr_io

# Build for native simulator with offloaded sockets
ZEPHYR_EXTRA_MODULES=$PWD/flow \
  .venv/bin/west build -p always -b native_sim \
  -d build_router flow/samples/router_sample

# Run the sample
./build_router/zephyr/zephyr.exe
```

### Connect with Python Client

In another terminal:
```bash
cd flow/samples/router_sample

# Interactive mode
python tcp_client.py

# Or with direct command
python tcp_client.py start    # Start sampling
python tcp_client.py stop     # Stop sampling
```

## Expected Output

### Server Side
```
*** Booting Zephyr OS build v3.7.1 ***
[00:00:00.000] <inf> main: Flow Router Sample with TCP Server
[00:00:00.000] <inf> main: ====================================
[00:00:00.000] <inf> main: Commands:
[00:00:00.000] <inf> main:   0x01 - Start sampling
[00:00:00.000] <inf> main:   0x02 - Stop sampling
[00:00:00.000] <inf> tcp_server: TCP server listening on 127.0.0.1:4242
[00:00:01.234] <inf> tcp_server: Client connected from 127.0.0.1:51234
[00:00:01.234] <inf> tcp_server: Received command: 0x01
[00:00:01.234] <inf> protocol: Processed: Sensor 1, counter=0, timestamp=1234000000 ns, 270 bytes
[00:00:01.334] <inf> protocol: Processed: Sensor 2, counter=1, timestamp=1334000000 ns, 398 bytes
```

### Client Side
```
Connected to 127.0.0.1:4242

Interactive commands: 's'=start, 't'=stop, 'q'=quit
> s
Started sampling

[SENSOR1] Packet #0
  Timestamp: 1234.000 ms
  Size: 256 bytes
  Data: a1 a1 a1 a1 a1 a1 a1 a1 a1 a1 a1 a1 a1 a1 a1 a1...

[SENSOR2] Packet #1
  Timestamp: 1334.000 ms
  Size: 384 bytes
  Data: b2 b2 b2 b2 b2 b2 b2 b2 b2 b2 b2 b2 b2 b2 b2 b2...
```

## Configuration

Key configuration options in `prj.conf`:

```conf
# Flow subsystem
CONFIG_FLOW=y
CONFIG_FLOW_BUF_TIMESTAMP_HIRES=y   # 64-bit timestamps

# Networking
CONFIG_NET_TCP=y
CONFIG_NET_SOCKETS=y
CONFIG_NET_SOCKETS_OFFLOAD=y        # Use host sockets for native_sim
CONFIG_NET_NATIVE_OFFLOADED_SOCKETS=y
```

## Testing

The sample includes pytest tests for automated testing:

```bash
# Run the tests using Twister
./scripts/run_tests.sh flow -s sample.flow.router_tcp

# Tests verify:
# - TCP server initialization
# - Packet reception
# - Start/stop commands
# - Packet metadata correctness
# - Payload patterns
```

## Protocol Details

### Commands (Client to Server)
- `0x01` - Start sensor sampling
- `0x02` - Stop sensor sampling

### Data Packets (Server to Client)
Each packet contains:
1. **Header** (14 bytes):
   - Packet ID (1 byte): Sensor identifier (1 or 2)
   - Reserved (1 byte): Future use
   - Counter (2 bytes): Incremental packet counter
   - Content Length (2 bytes): Payload size
   - Timestamp (8 bytes): Nanoseconds since start

2. **Payload** (variable):
   - Sensor 1: 256 bytes of 0xA1 pattern
   - Sensor 2: 384 bytes of 0xB2 pattern

## Implementation Notes

- **Sampling Control**: Sensors start with sampling DISABLED to prevent packets before client connection
- **Thread Safety**: Uses semaphore for thread-safe start/stop control
- **Event-Driven**: Uses Flow framework's event queues for packet processing
- **Native Simulator**: Configured with offloaded sockets for host connectivity

## Troubleshooting

### Port Already in Use
If you see "Failed to bind socket: 98", kill any lingering processes:
```bash
killall zephyr.exe
```

### Connection Refused
Ensure `CONFIG_NET_NATIVE_OFFLOADED_SOCKETS=y` is set for native_sim to use host networking.

### No Packets Received
- Check that sampling is started (send command 0x01)
- Verify TCP connection is established
- Check sensor threads are running

## Extending the Sample

### Adding New Commands
1. Define command in `tcp_server.h`
2. Add handler case in `cmd_handler.c`
3. Update Python client with new command

### Adding More Sensors
1. Create new source in `sensors.c`
2. Connect to protocol sink in `main.c`
3. Update packet ID handling as needed

## Further Reading

- [Flow API Documentation](../../doc/index.rst) - Complete API reference
- [Main Flow README](../../README.md) - Overview of the Flow framework
- [Basic Packet Routing Sample](../basic_packet_routing/) - Simpler example without networking