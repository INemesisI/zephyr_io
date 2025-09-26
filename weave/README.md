# Weave - Message Passing Framework for Zephyr

[![Zephyr Version](https://img.shields.io/badge/zephyr-v3.7.1-blue)](https://github.com/zephyrproject-rtos/zephyr)
[![License](https://img.shields.io/badge/license-Apache%202.0-green)](LICENSE)

Weave is a lightweight, D-Bus-inspired message passing framework for Zephyr RTOS. Unlike SwiftIO which focuses on many-to-many event streaming, Weave enables structured communication between modules using **method calls** and **signals**, making it ideal for configuration management, status queries, command execution, and distributed notifications.

## 🚀 Features

- **Method Calls**: RPC-style bidirectional communication with automatic correlation and timeouts
- **Signal Broadcasting**: Event notifications with multiple subscribers (fire-and-forget)
- **Static Memory**: No dynamic allocation - all resources allocated at compile time
- **Compile-Time Wiring**: Module connections established through declarative macros
- **Network Transparency**: Bridge modules enable seamless remote communication
- **Clean API**: Function-based runtime API with D-Bus inspired terminology

## 📋 Quick Start

### Prerequisites

- Zephyr RTOS v3.7.1+
- West build tool

### Basic Usage

```c
#include <zephyr_io/weave/weave.h>

// Define a temperature sensor module
WEAVE_MODULE_DEFINE(sensor_temperature,
    // Methods this module provides (service-side)
    METHODS(
        METHOD(read_temperature, handle_read_temperature),
        METHOD(write_config, handle_write_config)
    ),
    // Signals this module emits
    SIGNALS(
        SIGNAL(temperature_changed),
        SIGNAL(sensor_failed)
    )
);

// Method handler implementation
static int handle_read_temperature(void *instance,
                                   struct weave_call_context *ctx,
                                   const void *request,
                                   void *reply,
                                   size_t req_size,
                                   size_t rep_size) {
    struct read_temperature_reply *rep = reply;
    rep->value = read_sensor();
    return 0;
}

// Define a monitor module that calls methods and receives signals
WEAVE_MODULE_DEFINE(monitor_display,
    // Method calls this module makes (client-side)
    REQUIRED_METHODS(
        METHOD_CALL(call_read_temperature)
    ),
    // Signal handlers (what signals we receive)
    SLOTS(
        SLOT(on_temperature_changed, handle_temp_change)
    )
);

// Wire connections at compile time
WEAVE_METHOD_CONNECT(monitor_display, call_read_temperature,
                     sensor_temperature, read_temperature);

WEAVE_SIGNAL_MATCH(sensor_temperature, temperature_changed,
                   monitor_display, on_temperature_changed);

// Runtime usage - making a method call
void monitor_thread(void) {
    struct read_temperature_request req = {.unit = CELSIUS};
    struct read_temperature_reply reply;

    // Make synchronous method call with timeout
    int ret = weave_call_method(&call_read_temperature_port,
                                &req, &reply, K_MSEC(100));
    if (ret == 0) {
        LOG_INF("Temperature: %.1f", reply.value);
    }
}

// Runtime usage - emitting a signal
void sensor_thread(void) {
    struct temperature_changed_event event = {
        .temperature = current_temp,
        .timestamp = k_uptime_get()
    };
    weave_emit_signal(&temperature_changed_port, &event);
}
```

## 🏗️ Core Concepts

Weave adopts D-Bus terminology adapted for embedded systems:

| Concept | Description |
|---------|-------------|
| **Method** | A function that can be called on another module (expects reply) |
| **Method Call** | Request to execute a method with correlation and timeout |
| **Method Return** | Response from a method call |
| **Signal** | Event notification (no reply expected) |
| **Interface** | Logical grouping of related methods and signals |
| **Slot** | Signal handler/receiver |

## 📐 Communication Patterns

### Request/Response (Primary Pattern)
```text
[Module A] --METHOD_CALL--> [Module B]
           <--RETURN--------
```
Synchronous or asynchronous responses with automatic correlation and timeout handling.

### Signal Broadcasting
```text
[Producer] --SIGNAL--> [Subscriber 1]
                   \-> [Subscriber 2]
                   \-> [Subscriber N]
```
Event-driven notifications with multiple subscribers.

### Command/Acknowledgment
```text
[Controller] --COMMAND--> [Actuator]
             <--ACK/NACK--
```
Fire command with confirmation and optional result data.

## 🌐 Network Transparency

Weave achieves network transparency through bridge modules - regular modules that forward messages across transports:

```c
// Local wiring (direct connection)
WEAVE_METHOD_CONNECT(control, call_read_temp, sensor, read_temp);

// Remote wiring (via network bridge) - identical API!
WEAVE_METHOD_CONNECT(control, call_read_temp, network_bridge, read_temp);
```

The bridge module handles serialization and transport, keeping the core API transport-agnostic.

## 🔧 Sample Application

See `weave/samples/sensor_monitor/` for a complete example demonstrating:
- Sensor modules providing status methods
- Monitor module querying multiple sensors
- Configuration updates via method calls
- Event distribution using signals

```text
Module architecture:
  monitor ──call_read_temp──> sensor1
        │                      ├─signal: temp_changed
        ├──call_read_temp──> sensor2
        │                      └─signal: temp_changed
        └<─on_temp_changed─────┘
```

## 📊 Comparison with SwiftIO

| Aspect | SwiftIO | Weave |
|--------|---------|-------|
| **Pattern** | Many-to-many streaming | Method calls and signals |
| **Direction** | Unidirectional | Bidirectional for methods |
| **Correlation** | None | Automatic for method calls |
| **Timeouts** | Send timeouts only | Method completion timeouts |
| **Use Case** | High-throughput data | Control plane, configuration |

## 🛠️ Building and Testing

```bash
# Run all tests
ZEPHYR_EXTRA_MODULES=$PWD/weave \
  west twister \
  -T weave -p native_sim -v -O twister-out --no-clean

# Build sample application
ZEPHYR_EXTRA_MODULES=$PWD/weave \
  west build -p always -b native_sim -d build_weave \
  weave/samples/sensor_monitor

# Run sample
./build_weave/zephyr/zephyr.exe
```

## 🎯 Design Principles

1. **Static Memory Only**: No dynamic allocation - all resources fixed at compile time
2. **Compile-Time Wiring**: Connections established through declarative macros
3. **Timeout-Centric**: Every blocking operation must have a timeout
4. **Module Isolation**: Communication only through defined message interfaces
5. **Transport Agnostic**: Core API has no knowledge of transport mechanisms

## 🚧 Limitations

- **Static Configuration**: All connections must be defined at compile time
- **Fixed Resources**: Queue sizes and buffers are statically allocated
- **No Service Discovery**: Services and connections known at compile time

## 📖 Documentation

- **[Design Document](../message_passing_design.md)**: Complete architecture and design rationale
- **[API Reference](weave/include/zephyr_io/weave/weave.h)**: Function reference and API details
- **[Sample Code](weave/samples/)**: Working examples of common patterns
- **[Test Suite](weave/tests/)**: Comprehensive unit and integration tests

## 🤝 Integration with SwiftIO

Weave and SwiftIO complement each other for complete communication coverage:

```c
// High-throughput data streaming via SwiftIO
swift_io_source_send(&sensor_data, buffer, K_NO_WAIT);

// Control and configuration via Weave
struct set_config_request cfg = {.sample_rate = 1000};
struct set_config_reply reply;
weave_call_method(&set_config_port, &cfg, &reply, K_MSEC(100));

// Event notification via Weave signals
struct config_changed_event evt = {.new_rate = 1000};
weave_emit_signal(&config_changed_port, &evt);
```

## 🤝 Contributing

1. **Build Requirements**: Ensure Zephyr v3.7.1+ and Python virtual environment
2. **Testing**: All tests must pass before submitting changes
3. **Code Style**: Follow Zephyr coding conventions
4. **Documentation**: Update relevant documentation for API changes

---

*Built for Zephyr RTOS with ❤️*