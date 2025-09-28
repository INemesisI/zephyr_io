# Weave - Message Passing Framework

[![Zephyr Version](https://img.shields.io/badge/zephyr-v3.7.1-blue)](https://github.com/zephyrproject-rtos/zephyr)
[![License](https://img.shields.io/badge/license-Apache%202.0-green)](LICENSE)

Weave is a lightweight message passing framework for Zephyr RTOS that provides structured communication between modules using **method calls** (request/reply) and **signals** (event broadcast), ideal for control plane operations, configuration management, and event notifications.

## 🚀 Features

- **Method Calls**: Synchronous RPC-style communication with automatic correlation and timeouts
- **Signal Broadcasting**: Fire-and-forget event notifications to multiple subscribers
- **Static Memory**: All resources allocated at compile time - no heap usage
- **Type Safety**: Compile-time type checking for all method and signal connections
- **Flexible Execution**: Immediate (caller context) or queued (deferred) processing
- **Thread Integration**: Works seamlessly with existing threads or standalone modules

## 📋 Quick Start

### Prerequisites

- Zephyr RTOS v3.7.1+
- West build tool

### Basic Usage

```c
#include <zephyr_io/weave/weave.h>

// === METHOD CALLS ===

// Define module (needed for method and signal registration)
WEAVE_MODULE_DEFINE(sensor_module, NULL);  // NULL = immediate execution

// Define methods this module provides (service-side)
int handle_read_sensor(struct weave_module *module,
                      const struct read_sensor_request *request,
                      struct read_sensor_reply *reply) {
    reply->value = read_hw_sensor();
    return 0;
}
WEAVE_METHOD_REGISTER(sensor_read_sensor, handle_read_sensor,
    struct read_sensor_request, struct read_sensor_reply);

// Associate method with module (in init function)
sensor_read_sensor.module = &sensor_module;

// Define method call ports (client-side)
WEAVE_METHOD_PORT_DEFINE(call_read_sensor,
    struct read_sensor_request, struct read_sensor_reply);

// Wire method connection at compile time
WEAVE_METHOD_CONNECT(call_read_sensor, sensor_read_sensor);

// Runtime: Make method call
struct read_sensor_request req = {.channel = 0};
struct read_sensor_reply reply;
int ret = weave_call_method(&call_read_sensor, &req, sizeof(req),
                            &reply, sizeof(reply), K_MSEC(100));

// === SIGNALS ===

// Define signals
WEAVE_SIGNAL_DEFINE(threshold_exceeded, struct threshold_event);

// Define signal handlers
void handle_threshold(struct weave_module *module,
                     const struct threshold_event *event) {
    LOG_WRN("Threshold exceeded: %d", event->value);
}
WEAVE_SIGNAL_HANDLER_REGISTER(on_threshold, handle_threshold,
    struct threshold_event);

// Wire signal connection at compile time
WEAVE_SIGNAL_CONNECT(threshold_exceeded, on_threshold);

// Runtime: Emit signal
struct threshold_event event = {.value = 95, .channel = 0};
weave_emit_signal(&threshold_exceeded, &event);
```

### Module Definition with Message Queue

```c
// Define message queue for deferred processing
WEAVE_MSGQ_DEFINE(sensor_queue, 16);  // 16 messages max

// Define module with queue
WEAVE_MODULE_DEFINE(sensor_module, &sensor_queue);

// Processing thread
void sensor_thread(void) {
    while (1) {
        // Process queued method calls
        weave_process_all_messages(&sensor_module);
        k_sleep(K_MSEC(10));
    }
}
```

### Sample Application

See `weave/samples/sensor_monitor/` for a complete example demonstrating:
- Sensor module providing configuration and data methods
- Monitor module making periodic method calls
- Threshold event notifications via signals
- Clean separation of module implementations

## 🏗️ Core Concepts

| Concept | Description |
|---------|-------------|
| **Method** | Function callable on another module with request/reply semantics |
| **Method Port** | Client-side endpoint for making method calls |
| **Signal** | Event notification broadcast to multiple handlers |
| **Signal Handler** | Receiver that processes signal events |
| **Module** | Container for methods, signals, and optional message queue |

## 📐 Communication Patterns

### Request/Response
```
[Client] --METHOD_CALL--> [Service]
         <--REPLY--------
```
Synchronous calls with timeout and automatic correlation.

### Signal Broadcasting
```
[Producer] --SIGNAL--> [Handler 1]
                   \-> [Handler 2]
                   \-> [Handler N]
```
One-to-many event distribution without acknowledgment.

## 🚧 Limitations

- **Static Configuration**: All connections must be defined at compile time
- **Fixed Resources**: Queue sizes and buffers are statically allocated
- **No Service Discovery**: Services and connections known at compile time

## 🛠️ Building and Testing

```bash
# Run all tests with coverage
./scripts/generate_coverage.sh weave

# Run tests without coverage
./scripts/run_tests.sh weave

# Build sample application
ZEPHYR_EXTRA_MODULES=$PWD/weave \
  west build -p always -b native_sim -d build_weave \
  weave/samples/sensor_monitor

# Run sample
./build_weave/zephyr/zephyr.exe
```

## 📖 Documentation

- **[API Reference](include/zephyr_io/weave/weave.h)**: Function reference and detailed API documentation
- **[Sample Code](samples/)**: Working examples of common patterns
- **[Test Suite](tests/)**: Comprehensive unit tests covering all scenarios

## 🤝 Contributing

1. **Build Requirements**: Ensure Zephyr v3.7.1+ and Python virtual environment
2. **Testing**: All tests must pass before submitting changes
3. **Code Style**: Follow Zephyr coding conventions
4. **Documentation**: Update relevant documentation for API changes

---

*Built for Zephyr RTOS with ❤️*