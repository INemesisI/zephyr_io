# Register Mapper - Type-Safe Register-to-ZBUS Bridge for Zephyr RTOS

[![Zephyr Version](https://img.shields.io/badge/zephyr-v3.7.1-blue)](https://github.com/zephyrproject-rtos/zephyr)
[![License](https://img.shields.io/badge/license-Apache%202.0-green)](LICENSE)

A compile-time register mapping framework for Zephyr RTOS that bridges external register-based protocols (UART, Modbus, SPI, I2C) with internal ZBUS channels. Maps register addresses to specific fields within ZBUS message structures, enabling legacy interfaces to interact seamlessly with modern event-driven architectures.

## 🚀 Features

- **O(log n) Binary Search**: Automatic sorting via linker for fast register lookup
- **Type-Safe Mapping**: Compile-time validation ensures register types match channel field sizes
- **Zero-Copy Access**: Direct read/write to ZBUS message buffers without intermediate copies
- **Protocol Agnostic**: Works with any register-based protocol (Modbus, SPI, I2C, UART)
- **Atomic Transactions**: Block write support for consistent multi-register updates
- **Event-Driven**: Automatic ZBUS notifications trigger observers on register changes
- **Permission Control**: Per-register read/write access flags for security

**Note**: Binary search requires consistent address notation (all hex or all decimal)

## 📋 Quick Start

### Prerequisites

- Zephyr RTOS v3.7.1+
- ZBUS enabled (`CONFIG_ZBUS=y`)
- West build tool

### Basic Usage

```c
#include <zephyr_io/register_mapper/register_mapper.h>
#include <zephyr_io/register_mapper/register_channel.h>
#include <zephyr/zbus/zbus.h>

// Define configuration structure
struct sensor_config {
    uint16_t sample_rate;
    uint16_t filter_cutoff;
    uint8_t  gain;
    uint8_t  mode;
} __packed;

// Create ZBUS channel with automatic state tracking for block writes
REGISTER_CHAN_DEFINE(sensor_config_chan, struct sensor_config,
                     NULL, NULL, ZBUS_OBSERVERS_EMPTY,
                     (ZBUS_MSG_INIT(.sample_rate = 100,
                                   .filter_cutoff = 50,
                                   .gain = 1,
                                   .mode = 0)));

// Map registers to channel fields with compile-time validation
REG_MAPPING_DEFINE(reg_sample_rate, 0x1000, &sensor_config_chan,
                   struct sensor_config, sample_rate,
                   REG_TYPE_U16, REG_FLAGS_RW);

REG_MAPPING_DEFINE(reg_filter_cutoff, 0x1002, &sensor_config_chan,
                   struct sensor_config, filter_cutoff,
                   REG_TYPE_U16, REG_FLAGS_RW);

// Read register (runtime)
struct reg_value value;
reg_read_value(0x1000, &value);
LOG_INF("Sample rate: %u", value.val.u16);

// Write register with automatic ZBUS notification
value.type = REG_TYPE_U16;
value.val.u16 = 200;
reg_write_value(0x1000, value, K_MSEC(100));
```

### ZBUS Observer Integration

```c
// Module reacts to register changes
static void config_change_handler(const struct zbus_channel *chan)
{
    struct sensor_config config;
    zbus_chan_read(chan, &config, K_FOREVER);

    LOG_INF("Configuration updated via register write:");
    LOG_INF("  Sample rate: %u Hz", config.sample_rate);
    LOG_INF("  Filter: %u Hz", config.filter_cutoff);

    // Apply new configuration
    sensor_apply_config(&config);
}

// Register observer
ZBUS_LISTENER_DEFINE(config_listener, config_change_handler);
ZBUS_CHAN_ADD_OBS(sensor_config_chan, config_listener, 1);
```

### Block Write Transactions

```c
// Atomic update of multiple registers
int ret = reg_block_write_begin(K_MSEC(100));
if (ret == 0) {
    // Update multiple registers without triggering notifications
    reg_block_write_register(0x1000, REG_VALUE_U16(500));  // Sample rate
    reg_block_write_register(0x1002, REG_VALUE_U16(100));  // Filter cutoff
    reg_block_write_register(0x1004, REG_VALUE_U8(10));    // Gain

    // Commit - sends single notification with all changes
    reg_block_write_commit(K_MSEC(100));
}
```

### Sample Application

See `register_mapper/samples/device_config/` for a complete example demonstrating:
- Sensor module with separate status, config, and command channels
- Motor module with combined config/status channel
- Register mappings organized by address ranges:
  - 0x1000-0x1FFF: Status registers (read-only)
  - 0x2000-0x2FFF: Configuration registers (read-write)
  - 0x3000-0x3FFF: Data registers (read-only)
  - 0x4000-0x4FFF: Command registers (write-only)
- UART command handler simulating external protocol access
- Automatic ZBUS notifications on configuration changes

```
Architecture:
  UART Commands ──→ Register Mapper ──→ ZBUS Channels ──→ Module Observers
                         ↑                    ↓
                    Address Maps         Notifications
```

## 🚧 Limitations

- **Linear lookup**: O(n) register search (optimize for >100 registers)
- **Static mapping**: All register mappings defined at compile time
- **Type constraints**: Limited to basic integer types (u8/u16/u32/u64, i8/i16/i32/i64)
- **ZBUS dependency**: Requires ZBUS message bus subsystem

## 🛠️ Building and Testing

```bash
# Run all tests using the test script (RECOMMENDED)
./scripts/run_register_mapper_tests.sh

# Or manually with Twister
ZEPHYR_EXTRA_MODULES=$PWD/register_mapper \
PYTHON_PREFER=$PWD/.venv/bin/python3 CMAKE_PREFIX_PATH=$PWD/.venv \
  .venv/bin/python zephyr/scripts/twister \
  -T register_mapper/tests -p native_sim -v -O twister-out --no-clean

# Build sample application
ZEPHYR_EXTRA_MODULES=$PWD/register_mapper \
  .venv/bin/west build -p always -b native_sim -d build_sample \
  register_mapper/samples/device_config

# Run sample
./build_sample/zephyr/zephyr.exe

# Generate coverage report
./scripts/generate_coverage_register_mapper.sh
```

## 📖 Documentation

- **[User Guide](register_mapper/doc/index.rst)**: Comprehensive documentation with concepts, patterns, and examples
- **[API Reference](register_mapper/include/zephyr_io/register_mapper/register_mapper.h)**: Detailed API documentation
- **[Sample Code](register_mapper/samples/)**: Complete device configuration example
- **[Test Suite](register_mapper/tests/)**: Unit and integration tests

## 🎯 Use Cases

- **Legacy Protocol Bridge**: Connect Modbus/SPI devices to modern event-driven systems
- **Device Configuration**: External configuration via standard register interfaces
- **Industrial Control**: PLC-style register banks with automatic event distribution
- **IoT Gateways**: Protocol translation between field devices and cloud services

## 🤝 Contributing

1. **Build Requirements**: Ensure Zephyr v3.7.1+ with ZBUS enabled
2. **Testing**: All tests must pass before submitting changes
3. **Type Safety**: Maintain compile-time validation for all mappings
4. **Documentation**: Update relevant documentation for API changes

---

*Built for Zephyr RTOS with ❤️*