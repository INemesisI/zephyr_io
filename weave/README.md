# Weave - Message Passing Framework

[![Zephyr Version](https://img.shields.io/badge/zephyr-v3.7.1-blue)](https://github.com/zephyrproject-rtos/zephyr)
[![License](https://img.shields.io/badge/license-Apache%202.0-green)](LICENSE)

Weave is a lightweight message passing framework for Zephyr RTOS that provides structured communication between components using **method calls** (request/reply) and **signals** (event broadcast), ideal for control plane operations, configuration management, and event notifications.

## 🚀 Features

- **Method Calls**: Synchronous RPC-style communication with automatic correlation and timeouts
- **Signal Broadcasting**: Fire-and-forget event notifications to multiple subscribers
- **Static Memory**: All resources allocated at compile time - no heap usage
- **Type Safety**: Compile-time type checking for all method and signal connections
- **Flexible Execution**: Immediate (caller context) or queued (deferred) processing
- **Zero Module Overhead**: Methods and signals connect directly without intermediate abstractions

## 📋 Quick Start

### Prerequisites

- Zephyr RTOS v3.7.1+
- West build tool

### Basic Usage

```c
#include <zephyr_io/weave/weave.h>

// === METHOD CALLS ===

// Define message queue for deferred processing (optional)
WEAVE_MSGQ_DEFINE(sensor_queue, 16);  // 16 messages max

// Define methods (service-side)
int handle_read_sensor(void *user_data,
                      const struct read_sensor_request *request,
                      struct read_sensor_reply *reply) {
    reply->value = read_hw_sensor(request->channel);
    return 0;
}

// Method with queued execution
WEAVE_METHOD_DEFINE_QUEUED(sensor_read_method, handle_read_sensor, &sensor_queue,
                          struct read_sensor_request, struct read_sensor_reply);

// Method with immediate execution (no queue)
WEAVE_METHOD_DEFINE_IMMEDIATE(sensor_config_method, handle_config,
                             struct config_request, struct config_reply);

// Define method ports (client-side)
WEAVE_METHOD_PORT_DEFINE(read_sensor_port,
                        struct read_sensor_request, struct read_sensor_reply);

// Wire connection at compile time
WEAVE_METHOD_CONNECT(read_sensor_port, sensor_read_method);

// Or connect at runtime
void init(void) {
    int ret = weave_method_connect(&read_sensor_port, &sensor_read_method);
    if (ret != 0) {
        LOG_ERR("Failed to connect: %d", ret);
    }
}

// Make method call
struct read_sensor_request req = {.channel = 0};
struct read_sensor_reply reply;
int ret = weave_call_method(&read_sensor_port, &req, sizeof(req),
                            &reply, sizeof(reply), K_MSEC(100));

// === SIGNALS ===

// Define signals
WEAVE_SIGNAL_DEFINE(threshold_exceeded, struct threshold_event);

// Define signal handlers
void handle_threshold(void *user_data, const struct threshold_event *event) {
    LOG_WRN("Threshold exceeded: %d", event->value);
}

// Handler with queued execution
WEAVE_SIGNAL_HANDLER_DEFINE_QUEUED(threshold_handler, handle_threshold,
                                   &sensor_queue, struct threshold_event);

// Handler with immediate execution
WEAVE_SIGNAL_HANDLER_DEFINE_IMMEDIATE(threshold_logger, log_threshold,
                                      struct threshold_event);

// Wire signal connection at compile time
WEAVE_SIGNAL_CONNECT(threshold_exceeded, threshold_handler);
WEAVE_SIGNAL_CONNECT(threshold_exceeded, threshold_logger);

// Emit signal
struct threshold_event event = {.value = 95, .channel = 0};
weave_emit_signal(&threshold_exceeded, &event);
```

### Processing Queued Messages

```c
// Processing thread for queued methods/signals
void sensor_thread(void) {
    while (1) {
        // Process one message with timeout
        weave_process_message(&sensor_queue, K_MSEC(100));

        // Or process all pending messages
        weave_process_all_messages(&sensor_queue);
    }
}
```

### Sample Application

See `weave/samples/sensor_monitor/` for a complete example demonstrating:
- Methods with immediate and queued execution
- Signal broadcasting to multiple handlers
- Runtime connection management
- Message queue processing patterns

## 🏗️ Core Concepts

| Concept | Description |
|---------|-------------|
| **Method** | Function callable with request/reply semantics |
| **Method Port** | Client-side endpoint for making method calls |
| **Signal** | Event notification broadcast to multiple handlers |
| **Signal Handler** | Receiver that processes signal events |
| **Message Queue** | Optional queue for deferred processing |

## 📐 Communication Patterns

### Request/Response
```
[Client] --METHOD_CALL--> [Method]
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

## 🔧 API Reference

### Method APIs

```c
// Connect port to method (validates size compatibility)
int weave_method_connect(struct weave_method_port *port,
                        struct weave_method *method);

// Disconnect a method port
void weave_method_disconnect(struct weave_method_port *port);

// Check if port is connected
bool weave_method_is_connected(const struct weave_method_port *port);

// Call a method
int weave_call_method(struct weave_method_port *port,
                     const void *request, size_t request_size,
                     void *reply, size_t reply_size,
                     k_timeout_t timeout);
```

### Signal APIs

```c
// Emit signal to all handlers
int weave_emit_signal(struct weave_signal *signal, const void *event);
```

### Message Processing

```c
// Process single message from queue
int weave_process_message(struct k_msgq *queue, k_timeout_t timeout);

// Process all pending messages
int weave_process_all_messages(struct k_msgq *queue);
```

## 🚧 Limitations

- **Static Configuration**: Connections can be defined at compile time or runtime
- **Fixed Resources**: Queue sizes and buffers are statically allocated
- **No Service Discovery**: Services must be known at compile/init time

## 🛠️ Building and Testing

```bash
# Run all tests with coverage
./scripts/generate_coverage.sh weave

# Run tests without coverage
./scripts/run_tests.sh weave

# Build sample application
cd weave/samples/sensor_monitor
west build -b native_sim
./build/zephyr/zephyr.exe
```

## 📊 Memory Usage

- **Message Context**: ~40 bytes per pending request (from slab allocator)
- **Data Buffers**: Variable size from heap pool (configurable)
- **Method**: ~32 bytes static
- **Signal**: ~16 bytes static
- **Handler**: ~24 bytes static

## 📈 Performance

- **Immediate Execution**: ~100 cycles overhead
- **Queued Execution**: ~500 cycles + context switch
- **Signal Emission**: O(n) where n = number of handlers

## ⚙️ Configuration

Key Kconfig options:

```kconfig
CONFIG_WEAVE=y                        # Enable weave subsystem
CONFIG_WEAVE_MAX_PENDING_REQUESTS=16  # Max concurrent method calls
CONFIG_WEAVE_DATA_HEAP_SIZE=4096     # Heap for variable data
CONFIG_WEAVE_LOG_LEVEL=2             # Log level (0-4)
```

## 📝 License

Apache 2.0 - See [LICENSE](LICENSE) for details.