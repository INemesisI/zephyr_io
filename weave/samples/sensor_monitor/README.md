# Weave Sensor Monitor Sample

Demonstrates Weave's message passing framework through a sensor monitoring system with method calls and signal notifications.

## Overview

Three components work together to demonstrate Weave communication:
- **Sensor** - Provides methods for reading data and configuration
- **Monitor** - Handles threshold signals and tracks statistics
- **Main** - Defines method ports, wires connections, and orchestrates tests

```
Component Communication Architecture:

┌─────────────┐                      ┌─────────────┐
│   MONITOR   │                      │   SENSOR    │
│             │                      │             │
│  - Handles  │  ←──SIGNAL──────────│  - Provides │
│    alerts   │  threshold_exceeded  │    methods  │
│             │                      │  - Auto-    │
│  - Tracks   │                      │    samples  │
│    stats    │                      │             │
└─────────────┘                      └─────────────┘
        ↑                                    ↑
        └──────────── MAIN ─────────────────┘
              (Defines method ports,
               wires connections, and
               orchestrates test calls)

Communication Flow:
• Method Calls: Main → Sensor (using method ports)
• Signals: Sensor → Monitor (threshold events)
• Wiring: Main connects everything at startup
```

## What This Sample Shows

- **Method calls with timeouts** - Main orchestrates sensor queries via method ports
- **Signal broadcasting** - Sensor emits threshold alerts to monitor
- **Mixed execution modes** - Methods use queued execution, signals use immediate
- **Separation of concerns** - Main owns the wiring and test logic

## Component Responsibilities

### Main (main.c)
- Defines method ports for calling sensor methods
- Wires method ports and signals
- Runs test scenarios and displays results

### Sensor (sensor_module.c)
- Implements methods (read_sensor, set_config, get_stats)
- Runs auto-sampling in a background thread
- Emits threshold_exceeded signals
- Processes queued method calls

### Monitor (monitor_module.c)
- Handles threshold_exceeded signals
- Tracks alert statistics
- Provides immediate signal processing (no queue)

## Test Scenarios

The sample runs through four test scenarios:

### Test 1: Configure sensor with low threshold
- Sets threshold to 70 (down from default 100)
- Demonstrates configuration method call
- Shows old and new threshold values

### Test 2: Manual sensor reads
- Performs 5 manual sensor reads on different channels
- Demonstrates read_sensor method calls
- Shows individual channel values

### Test 3: Increase threshold
- Sets threshold to 150 (high value)
- Stops triggering alerts
- Demonstrates dynamic configuration

### Test 4: Get statistics
- Retrieves and displays sensor statistics (reads, events, value range)
- Retrieves and displays monitor statistics (alerts received, last value)
- Shows accumulated operational data

## Building and Running

```bash
# Build for native simulator
west build -b native_sim weave/samples/sensor_monitor

# Run the sample
./build/zephyr/zephyr.exe
```

## Expected Output

```
[00:00:00.000] <inf> app: ==============================================
[00:00:00.000] <inf> app: Weave Sensor Monitor Sample
[00:00:00.000] <inf> app: ==============================================

[00:00:00.100] <inf> app: Test 1: Configure sensor with low threshold
[00:00:00.100] <inf> sensor: Config changed: threshold 100 → 70
[00:00:00.100] <inf> app: Config changed: old threshold=100, new=70

[00:00:00.600] <inf> sensor: Auto-sample: value=85 > threshold=70
[00:00:00.600] <wrn> monitor: ALERT: Threshold exceeded! Value=85, Threshold=70

[00:00:03.100] <inf> app: Test 2: Manual sensor reads
[00:00:03.100] <inf> sensor: Manual read channel 0: value=45
[00:00:03.100] <inf> app: Manual read ch0: value=45

[00:00:08.100] <inf> app: Test 3: Increase threshold
[00:00:08.100] <inf> sensor: Config changed: threshold 70 → 150
[00:00:08.100] <inf> app: Threshold increased to 150

[00:00:11.100] <inf> app: Test 4: Get statistics
[00:00:11.100] <inf> app: Sensor Statistics:
[00:00:11.100] <inf> app:   Total reads:      15
[00:00:11.100] <inf> app:   Threshold events: 3
[00:00:11.100] <inf> app:   Value range:      25 to 95

[00:00:11.100] <inf> app: Monitor Statistics:
[00:00:11.100] <inf> app:   Alerts received: 3
[00:00:11.100] <inf> app:   Last alert value: 85
```

## Sample Structure

```
sensor_monitor/
├── src/
│   ├── main.c              # Method ports, wiring, and test orchestration
│   ├── sensor_module.c      # Sensor methods and signal emission
│   ├── sensor_module.h      # Message structures and sensor API
│   ├── monitor_module.c     # Signal handler and statistics
│   └── monitor_module.h     # Monitor API
├── prj.conf                 # Configuration
├── sample.yaml              # Twister test definition
└── README.md
```

## Further Reading

- [Main Weave README](../../README.md) - Overview and API documentation