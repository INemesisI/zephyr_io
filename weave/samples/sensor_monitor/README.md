# Weave Sensor Monitor Sample

Demonstrates Weave's message passing framework through a sensor monitoring system with method calls and signal notifications.

## Overview

Two modules demonstrate the complete Weave communication patterns:
- **Sensor** - Provides methods for reading data and configuration
- **Monitor** - Calls sensor methods and handles threshold signals

```
Module Communication Architecture:

┌─────────────┐                      ┌─────────────┐
│   MONITOR   │  ──METHOD CALLS───→  │   SENSOR    │
│             │                      │             │
│  - Queries  │  read_sensor         │  - Provides │
│    sensor   │  set_config          │    data     │
│    data     │  get_stats           │             │
│             │                      │  - Auto-    │
│  - Handles  │  ←──SIGNAL──────────│    samples  │
│    alerts   │  threshold_exceeded  │             │
└─────────────┘                      └─────────────┘

Communication Patterns:
• Method Calls: Monitor → Sensor (request/reply)
• Signals: Sensor → Monitor (broadcast events)
```

## How It Works

### Method Calls (RPC-Style)
1. Monitor defines method ports: `WEAVE_METHOD_PORT_DEFINE()`
2. Sensor registers methods: `WEAVE_METHOD_REGISTER()`
3. Main wires connections: `WEAVE_METHOD_CONNECT()`
4. Monitor makes synchronous calls with timeouts

### Signal Broadcasting
1. Sensor defines signals: `WEAVE_SIGNAL_DEFINE()`
2. Monitor registers handlers: `WEAVE_SIGNAL_HANDLER_REGISTER()`
3. Main wires connections: `WEAVE_SIGNAL_CONNECT()`
4. Sensor emits events when thresholds exceeded

### Key Features
- **Thread Integration**: Sensor runs in its own thread with message queue
- **Type Safety**: All connections type-checked at compile time
- **Timeout Handling**: Every method call has explicit timeout
- **Module Isolation**: Clean separation between modules

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
[00:00:00.000] <inf> app: Weave Thread Integration Sample
[00:00:00.000] <inf> app: ==============================================

[00:00:00.100] <inf> app: Test 1: Configure sensor with low threshold
[00:00:00.100] <inf> sensor: Config changed: threshold 100 → 70
[00:00:00.100] <inf> app: Config changed: old threshold=100, new=70

[00:00:00.600] <inf> sensor: Auto-sample: value=85 > threshold=70
[00:00:00.600] <wrn> monitor: ALERT: Sensor exceeded threshold! Value=85, Threshold=70

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

## Module Implementation

### Sensor Module
```c
// Provides three methods
WEAVE_METHOD_REGISTER(sensor_read_sensor, ...);
WEAVE_METHOD_REGISTER(sensor_set_config, ...);
WEAVE_METHOD_REGISTER(sensor_get_stats, ...);

// Emits threshold signal
WEAVE_SIGNAL_DEFINE(threshold_exceeded, ...);

// Runs in dedicated thread with message queue
K_THREAD_DEFINE(sensor_tid, ..., sensor_thread, ...);
```

### Monitor Module
```c
// Method call ports for sensor communication
WEAVE_METHOD_PORT_DEFINE(monitor_call_read_sensor, ...);
WEAVE_METHOD_PORT_DEFINE(monitor_call_set_config, ...);
WEAVE_METHOD_PORT_DEFINE(monitor_call_get_stats, ...);

// Signal handler for threshold events
WEAVE_SIGNAL_HANDLER_REGISTER(monitor_on_threshold_exceeded, ...);

// No thread - runs in main context
```

## Extending the Sample

### Adding a New Method
```c
// In sensor_module.h - define request/reply structures
struct calibrate_request {
    uint32_t offset;
};
struct calibrate_reply {
    bool success;
};

// In sensor_module.c - implement and register
int handle_calibrate(void *module, const void *req, void *rep) {
    // Implementation
}
WEAVE_METHOD_REGISTER(sensor_calibrate, handle_calibrate,
    struct calibrate_request, struct calibrate_reply);

// In monitor_module.c - define port
WEAVE_METHOD_PORT_DEFINE(monitor_call_calibrate,
    struct calibrate_request, struct calibrate_reply);

// In main.c - wire connection
WEAVE_METHOD_CONNECT(monitor_call_calibrate, sensor_calibrate);
```

### Adding a New Signal
```c
// In sensor_module.c - define and emit
WEAVE_SIGNAL_DEFINE(sensor_error, struct error_event);

// Emit when error occurs
struct error_event evt = {.code = ERROR_TIMEOUT};
weave_emit_signal(&sensor_error, &evt);

// In monitor_module.c - handle signal
void handle_error(void *module, const void *event) {
    // Handle error
}
WEAVE_SIGNAL_HANDLER_REGISTER(monitor_on_error, handle_error,
    struct error_event);

// In main.c - wire connection
WEAVE_SIGNAL_CONNECT(sensor_error, monitor_on_error);
```

## Further Reading

- [Weave API Documentation](../../include/zephyr_io/weave/weave.h) - Complete API reference
- [Main Weave README](../../README.md) - Overview of the Weave framework