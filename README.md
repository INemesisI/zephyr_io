# Zephyr I/O - Advanced Communication Modules for Zephyr RTOS

[![Zephyr Version](https://img.shields.io/badge/zephyr-v3.7.1-blue)](https://github.com/zephyrproject-rtos/zephyr)
[![License](https://img.shields.io/badge/license-Apache%202.0-green)](LICENSE)

A collection of high-performance, thread-safe communication and I/O modules for Zephyr RTOS, designed to bridge modern event-driven architectures with legacy protocols and enable efficient packet distribution systems.

## 🔧 Modules

### [Flow](flow/) - Fast Lightweight Object Wiring

Flow is a high-performance, zero-copy packet distribution framework based on the **source/sink pattern**. Sources (producers) send `net_buf` packets to multiple sinks (consumers) without data copying, using reference counting for efficient many-to-many communication.

**Key Features:**
- **Many-to-Many Routing**: Sources can send to multiple sinks, sinks can receive from multiple sources
- **Flexible Execution Modes**: Immediate (in source context) or queued (deferred processing) handler execution
- **Zero-Copy Distribution**: Efficient packet sharing using `net_buf` reference counting
- **Protocol Packaging**: Chain buffers to add headers/footers without copying payload data

### [Register Mapper](register_mapper/) - Legacy Protocol Bridge

A compile-time register mapping system that bridges external register-based interfaces (UART, Modbus, SPI) with internal ZBUS channels. Maps register addresses to specific fields within ZBUS message structures, enabling legacy protocols to interact seamlessly with modern event-driven architectures.

**Key Features:**
- **Static Mapping**: Register-to-channel mappings defined at compile time
- **Type Safety**: Compile-time validation of field existence and type sizes
- **Zero-Copy Access**: Direct read/write to ZBUS channel message buffers
- **Event Notifications**: Automatic ZBUS notifications on register writes

## 🚀 Quick Start

### Prerequisites

- Zephyr RTOS v3.7.1+
- West build tool
- Python virtual environment (for testing)

### Environment Setup

```bash
# Clone with submodules
git clone --recursive https://github.com/your-org/zephyr_io.git
cd zephyr_io

# Set up Zephyr environment (if not already done)
west init -l zephyr
west update
```

### Building and Testing

#### Run All Tests

```bash
# Activate virtual environment
source zephyr/.venv/bin/activate

# Run flow tests and samples
ZEPHYR_EXTRA_MODULES=$PWD/flow \
  west twister -T flow -p native_sim -v -O twister-out --no-clean

# Run register_mapper tests
ZEPHYR_EXTRA_MODULES=$PWD/register_mapper \
  west twister -T register_mapper -p native_sim -v -O twister-out --no-clean
```

#### Code Coverage

```bash
# Activate virtual environment
source zephyr/.venv/bin/activate

# Generate coverage for flow
ZEPHYR_EXTRA_MODULES=$PWD/flow \
  west twister --coverage -p native_sim -T flow -v -O twister-coverage --no-clean

# Generate coverage for register_mapper
ZEPHYR_EXTRA_MODULES=$PWD/register_mapper \
  west twister --coverage -p native_sim -T register_mapper -v -O twister-coverage --no-clean
```

## 🤝 Contributing

1. **Build Requirements**: Ensure Zephyr v3.7.1+ and Python virtual environment
2. **Testing**: All module tests must pass before submitting changes
3. **Code Style**: Follow Zephyr coding conventions
4. **Documentation**: Update relevant documentation for API changes

---

*Built for Zephyr RTOS with ❤️*