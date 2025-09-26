# SwiftIO Basic Sample - Modular Packet Routing

This sample demonstrates advanced swift_io concepts through a modular, multi-stage packet processing pipeline with integrity validation.

## Overview

The sample implements a complete packet processing system with four independent modules:
1. **Sensors** - Generate test data packets from two sources
2. **Processor** - Adds protocol headers using zero-copy buffer chaining
3. **Network** - Handles TCP transmission
4. **Validator** - Verifies packet integrity and content correctness

```
sensor1_source ─┐                              ┌─→ tcp_sink (network)
                ├─→ processor_sink → processor_source ─┤
sensor2_source ─┘                              └─→ validator_sink
```

## Architecture

### Modular Design
Each module is completely self-contained with:
- Its own buffer pool
- Dedicated processing thread (started via `K_THREAD_DEFINE`)
- Independent logging
- Can work in isolation even without main.c

### Zero-Copy Processing
The processor module demonstrates efficient header prepending using buffer chaining:
- Headers are added as a separate buffer fragment
- Original packet data is never copied
- `net_buf_frag_add()` chains buffers together
- `net_buf_frags_len()` calculates total packet size

### Packet Structure
```c
struct packet_header {
    uint8_t  source_id;      // Sensor identifier
    uint8_t  packet_type;    // Data or control packet
    uint16_t sequence;       // Global sequence number
    uint32_t timestamp;      // Packet creation time
    uint16_t content_length; // Payload size (excluding header)
    uint16_t reserved;       // Future use
} __packed;
```

## Module Descriptions

### Sensors Module (`sensors.c`)
- Generates packets from two simulated sensors
- Each sensor produces distinct data patterns (0xA0... and 0xB0...)
- 1-second interval between packets
- Own 64-byte buffer pool for small sensor data

### Processor Module (`processor.c`)
- Receives raw sensor packets
- Adds 12-byte protocol header
- Uses buffer chaining for zero-copy operation
- Maintains global sequence counter
- Minimal buffer pool (header size only)

### Network Module (`network.c`)
- Simulates TCP network transmission
- 50ms transmission delay per packet
- Logs packet details including size breakdown
- Handles chained buffers transparently

### Validator Module (`validator.c`)
- Comprehensive packet validation:
  - Size integrity (header content_length vs actual size)
  - Source ID validation
  - Packet type validation
  - Sequence number progression tracking
  - Content pattern verification
- Maintains validation statistics
- Reports success rate every 10 packets

## Key Concepts Demonstrated

- **Modular Independence**: Each module can work standalone
- **Static Initialization**: Threads start automatically via `K_THREAD_DEFINE`
- **Buffer Chaining**: Efficient header addition without copying
- **Packet Integrity**: Content length validation
- **Multi-Sink Distribution**: Packets sent to both network and validator
- **Declaration/Definition Pattern**: Using `PACKET_*_DECLARE` in headers

## Building and Running

```bash
# Build for native simulator
west build -b native_sim swift_io/samples/basic_packet_routing

# Run the sample
./build/zephyr/zephyr.exe
```

## Expected Output

```
*** Booting Zephyr OS build v3.7.1 ***
[00:00:00.000] <inf> main: SwiftIO Routing Sample
[00:00:00.000] <inf> main: ==========================
[00:00:00.000] <inf> main: Module threads start automatically:
[00:00:00.000] <inf> main:   - Sensors: Generate test packets
[00:00:00.000] <inf> main:   - Processor: Add headers and forward
[00:00:00.000] <inf> main:   - Network: TCP transmission
[00:00:00.000] <inf> main:   - Validator: Check packet integrity
[00:00:00.000] <inf> network: TCP TX: Sensor 1, Seq 0, Total 17 bytes (hdr 12 + data 5)
[00:00:00.000] <inf> validator: VALID: Sensor 1, Seq 0, 17 bytes [Total validated: 1]
[00:00:01.010] <inf> network: TCP TX: Sensor 2, Seq 1, Total 18 bytes (hdr 12 + data 6)
[00:00:01.010] <inf> validator: VALID: Sensor 2, Seq 1, 18 bytes [Total validated: 2]
...
[00:00:09.090] <inf> validator: Validation stats: Valid=10, Failed=0, Success rate=100%
```

## Code Structure

```
src/
├── main.c          # Wiring and initialization only
├── packet_defs.h   # Common packet structures
├── sensors.c/h     # Sensor data generation
├── processor.c/h   # Header addition logic
├── network.c/h     # TCP transmission
└── validator.c/h   # Integrity validation
```

## Customization

### Add New Processing Stages
1. Create new module files (e.g., `encryption.c/h`)
2. Define sink/source with `PACKET_SINK_DEFINE`/`PACKET_SOURCE_DEFINE`
3. Add `PACKET_*_DECLARE` in header
4. Wire connections in `main.c`
5. Add to `CMakeLists.txt`

### Modify Packet Validation
Edit `validator.c` to add custom checks:
- Checksum validation
- Encryption verification
- Rate limiting
- Pattern matching

### Change Buffer Sizes
Each module has its own buffer pool that can be tuned:
```c
// In sensors.c - small buffers for sensor data
NET_BUF_POOL_DEFINE(sensor_pool, 8, 64, 4, NULL);

// In processor.c - header-sized buffers only
NET_BUF_POOL_DEFINE(processor_pool, 16, sizeof(struct packet_header), 4, NULL);
```

### Adjust Queue Policies
```c
// Drop packets if queue full (lossy)
PACKET_SINK_DEFINE(tcp_sink, 10, true);

// Wait if queue full (lossless)
PACKET_SINK_DEFINE(validator_sink, 10, false);
```

## Advanced Features

### Buffer Chain Inspection
The validator demonstrates how to work with chained buffers:
```c
// Calculate total size of all fragments
size_t total_len = net_buf_frags_len(buf);

// Access data in fragments
if (buf->frags) {
    uint8_t first_byte = buf->frags->data[0];
}
```

### Thread Priorities
Threads are configured with different priorities:
- Processor: Priority 5 (highest)
- Network: Priority 5
- Validator: Priority 6
- Sensors: Priority 7 (lowest)

### Statistics Tracking
The validator maintains and reports statistics:
- Total packets validated
- Total packets failed
- Success rate percentage
- Per-packet validation details

## Troubleshooting

**No output**: Ensure `CONFIG_LOG=y` in `prj.conf`

**Buffer allocation failures**: Increase pool sizes in module definitions

**Sequence gaps**: Normal if packets are dropped due to full queues

**Build errors**: Ensure `ZEPHYR_EXTRA_MODULES` points to swift_io module

## Further Reading

- [SwiftIO Design Document](../../swift_io_design.md)
- [Zephyr Network Buffers](https://docs.zephyrproject.org/latest/reference/networking/net_buf.html)
- [Zephyr Threading](https://docs.zephyrproject.org/latest/reference/kernel/threads/index.html)