# Packet I/O Basic Sample - Packet Routing with Header Addition

This sample demonstrates the fundamental concepts of the packet_io subsystem through a practical networking scenario.

## Overview

The sample simulates a packet processing pipeline where:
1. Two sensors generate data packets
2. A processor node receives packets, adds protocol headers, and forwards them
3. A TCP sink queues packets for network transmission

```
sensor1 ─┐
         ├─→ processor ─→ TCP sink
sensor2 ─┘
```

## Key Concepts Demonstrated

- **Sources and Sinks**: How to define packet sources and sinks
- **Static Wiring**: Compile-time connections using `PACKET_SOURCE_CONNECT`
- **Zero-Copy**: Efficient packet handling with reference counting
- **Processing Pipeline**: Adding headers while maintaining packet flow
- **Queue Management**: Different queue sizes and drop policies

## Building and Running

```bash
west build -b native_sim packet_io/samples/basic_packet_routing
./build/zephyr/zephyr.exe
```

## Expected Output

```
[00:00:00.000] <inf> packet_sample: Packet I/O Routing Sample
[00:00:00.000] <inf> packet_sample: ==========================
[00:00:00.000] <inf> packet_sample: Demonstrating packet flow with header addition
[00:00:00.001] <inf> packet_sample: Packet processor started
[00:00:00.001] <inf> packet_sample: TCP transmitter started
[00:00:01.002] <inf> packet_sample: TX: Sensor 1, Seq 0, Size 13 bytes
[00:00:02.003] <inf> packet_sample: TX: Sensor 2, Seq 1, Size 14 bytes
...
```

## Code Walkthrough

### 1. Define the packet flow components:
```c
PACKET_SOURCE_DEFINE(sensor1_source);
PACKET_SINK_DEFINE(processor_sink, 10, false);
```

### 2. Wire connections at compile time:
```c
PACKET_SOURCE_CONNECT(sensor1_source, processor_sink);
```

### 3. Send packets:
```c
ret = packet_source_send(&sensor1_source, buf);
```

### 4. Receive and process packets:
```c
ret = k_msgq_get(&processor_sink.msgq, &in_buf, K_FOREVER);
```

## Customization

- Adjust queue sizes in the `PACKET_SINK_DEFINE` macros
- Modify the header structure for your protocol
- Change packet generation rates in `generate_sensor_data()`
- Add more sources or processing stages as needed