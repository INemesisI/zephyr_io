# Flow Basic Sample - Packet ID-Based Routing

Demonstrates Flow's automatic packet ID routing through a multi-stage processing pipeline.

## Overview

Four modules demonstrate packet routing:
- **Sensors** - Generate packets with unique IDs (0x0001, 0x0002)
- **Processor** - Adds/removes headers, preserves packet IDs
- **Echo** - Simulates network echo service
- **Validators** - Per-sensor validation with automatic ID filtering

```
Module-Level Packet Flow:

┌─────────────┐     Raw Packets       ┌─────────────┐
│   SENSORS   │ ───────────────────→  │  PROCESSOR  │
│             │     (ID: 0x0001)      │  (Outbound) │
│  sensor1    │     (ID: 0x0002)      │             │
│  sensor2    │                       │   - Adds    │
└─────────────┘                       │    headers  │
                                      │   - Forward │
                                      └──────┬──────┘
                                             │
                                    Packets with Headers
                                             ↓
                                      ┌─────────────┐
                                      │    ECHO     │
                                      │             │
                                      │  - Simulates│
                                      │    network  │
                                      │    echo     │
                                      │  - Loops    │
                                      │    back     │
                                      │    unchanged│
                                      └──────┬──────┘
                                             │
                                      Echoed Packets
                                             ↓
┌─────────────┐                       ┌─────────────┐
│ VALIDATORS  │  ←──────────────────  │  PROCESSOR  │
│             │   Routed by Packet ID │  (Inbound)  │
│ validator1  │   (0x0001 → val1)     │             │
│ validator2  │   (0x0002 → val2)     │   - Extracts│
└─────────────┘                       │    payload  │
                                      │   - Forward │
                                      └─────────────┘

Packet ID Filtering (Automatic via Flow framework):
- Sensor1 packets (ID: 0x0001) → Validator1 only
- Sensor2 packets (ID: 0x0002) → Validator2 only
```

## How It Works

### Packet ID Routing
1. Sensors use `FLOW_SOURCE_DEFINE_ROUTED(name, id)` to stamp packets with IDs
2. Processor adds header with packet ID, preserves ID through pipeline
3. Validators use `FLOW_SINK_DEFINE_ROUTED_IMMEDIATE(name, handler, id)` to filter
4. Flow framework automatically routes packets - no manual filtering code needed

### Key Features
- **Zero-copy**: Headers added via buffer chaining (`net_buf_frag_add`)
- **Auto-start**: Modules use `K_THREAD_DEFINE` to start automatically
- **ID preservation**: Packet IDs stored in buffer user data

## Building and Running

```bash
# Build for native simulator
west build -b native_sim flow/samples/basic_packet_routing

# Run the sample
./build/zephyr/zephyr.exe
```

## Expected Output

```
[00:00:00.000] <inf> validator: Validator1: VALID packet from Sensor 1 (ID: 0x0001)
[00:00:01.010] <inf> validator: Validator2: VALID packet from Sensor 2 (ID: 0x0002)
...
[00:00:09.090] <inf> validator: Validator1 stats: Valid=5, Failed=0 (Sensor 1 only)
[00:00:09.090] <inf> validator: Validator2 stats: Valid=5, Failed=0 (Sensor 2 only)
```

Note: Each validator only receives packets from its matching sensor.


## Extending the Sample

### Adding a New Sensor
```c
// New sensor with unique ID
FLOW_SOURCE_DEFINE_ROUTED(sensor3_source, 0x0003);

// Validator that only accepts sensor3 packets
FLOW_SINK_DEFINE_ROUTED_IMMEDIATE(sensor3_validator, handler, 0x0003);

// Connect them - filtering is automatic!
FLOW_CONNECT(&sensor3_source, &processor_outbound_sink);
FLOW_CONNECT(&processor_inbound_source, &sensor3_validator);
```


## Further Reading

- [Flow API Documentation](../../doc/index.rst) - Complete API reference
- [Main Flow README](../../README.md) - Overview of the Flow framework