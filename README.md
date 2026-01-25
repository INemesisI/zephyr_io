# Zephyr I/O - Advanced Communication Modules for Zephyr RTOS

[![Zephyr Version](https://img.shields.io/badge/zephyr-v3.7.1-blue)](https://github.com/zephyrproject-rtos/zephyr)
[![License](https://img.shields.io/badge/license-Apache%202.0-green)](LICENSE)

A collection of high-performance, thread-safe communication modules for Zephyr RTOS.

## Modules

### [Weave](libs/weave/) - Message Passing Framework

Weave is a lightweight, zero-allocation message passing framework providing structured communication between modules. It includes three subsystems:

**Core** - Source/Sink Pattern
- Many-to-many message routing with static compile-time wiring
- Immediate (in source context) or queued (deferred) handler execution
- Reference-counted payload lifecycle management
- Thread-safe with spinlock protection

**Packet** - Zero-Copy Buffer Distribution
- Distributes `net_buf` packets from sources to multiple sinks
- Automatic reference counting - no data copying
- Packet metadata: ID, counter, timestamp, client ID
- Sink-side filtering by packet ID

**Method** - Synchronous RPC
- Type-safe request/response calls with automatic correlation
- Synchronous and async variants with configurable timeouts
- Zero heap allocation - all context on caller's stack

**Observable** - Stateful Pub/Sub
- State variables with change notifications
- Optional validation before updates
- Owner handlers for immediate reaction
- External observers via source/sink mechanism

## Quick Start

### Prerequisites

- Zephyr RTOS v3.7.1+
- West build tool
- Python virtual environment (for testing)

### Building and Testing

```bash
# Run all weave tests
./scripts/run_tests.sh weave

# With verbose output
./scripts/run_tests.sh weave -v

# Generate coverage report
./scripts/generate_coverage.sh weave
```

### Using Weave in Your Project

Add to your `CMakeLists.txt`:
```cmake
set(ZEPHYR_EXTRA_MODULES /path/to/zephyr_io/libs/weave)
```

Enable in `prj.conf`:
```conf
CONFIG_WEAVE=y
CONFIG_WEAVE_PACKET=y      # For packet routing
CONFIG_WEAVE_METHOD=y      # For RPC calls
CONFIG_WEAVE_OBSERVABLE=y  # For stateful pub/sub
```

## Documentation

See [libs/weave/docs/](libs/weave/docs/) for detailed API documentation and examples.

## Contributing

1. All tests must pass: `./scripts/run_tests.sh weave`
2. Coverage: 100% Functions, >90% lines, >80% branches
3. Follow Zephyr coding conventions

---

*Built for Zephyr RTOS with ❤️*