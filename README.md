# Weave - Message Passing Framework for Zephyr

[![Zephyr Version](https://img.shields.io/badge/zephyr-v3.7.1-blue)](https://github.com/zephyrproject-rtos/zephyr)
[![License](https://img.shields.io/badge/license-Apache%202.0-green)](LICENSE)

**Weave** allows kernel *threads* to communicate - like threads of fabric being woven together.

Weave is a lightweight message passing framework for Zephyr RTOS that lets different parts of your system interact across thread boundaries through compile-time connections. It provides three communication patterns: zero-copy packet routing, RPC method calls, and stateful observables - all woven together through a common foundation.

## Architecture

```
┌─────────────┬─────────────┬──────────────┐
│   Packet    │   Method    │  Observable  │
│ (net_buf)   │   (RPC)     │  (pub/sub)   │
├─────────────┴─────────────┴──────────────┤
│                  Core                    │
│         (sources, sinks, queues)         │
└──────────────────────────────────────────┘
```

**Core** provides generic source/sink message passing with compile-time wiring.
Each mechanism builds on Core with specialized behavior.

## Quick Examples

### Packet - Zero-Copy Data Streams

```c
#include <weave/packet.h>

WEAVE_PACKET_POOL_DEFINE(pool, 16, 128, NULL);
WEAVE_PACKET_SOURCE_DEFINE(sensor_source);

void handler(struct net_buf *buf, void *user_data) {
    LOG_INF("Received %d bytes", buf->len);
}

WEAVE_PACKET_SINK_DEFINE(log_sink, handler, WV_IMMEDIATE, WV_NO_FILTER, NULL);
WEAVE_CONNECT(&sensor_source, &log_sink);

void send_data(void) {
    struct net_buf *buf = weave_packet_alloc(&pool, K_NO_WAIT);
    net_buf_add_mem(buf, data, len);
    weave_packet_send(&sensor_source, buf, K_NO_WAIT);
}
```

### Method - Request/Response RPC

```c
#include <weave/method.h>

WEAVE_MSGQ_DEFINE(service_queue, 8);

int read_handler(const struct read_req *req, struct read_res *res, void *user_data) {
    res->value = get_sensor_value(req->channel);
    return 0;
}

WEAVE_METHOD_DEFINE(read_sensor, read_handler, &service_queue, NULL,
                    struct read_req, struct read_res);

/* Caller side - blocks until handler completes */
struct read_req req = {.channel = 0};
struct read_res res;
int ret = WEAVE_METHOD_CALL(read_sensor, &req, &res);
```

### Observable - State with Notifications

```c
#include <weave/observable.h>

struct config { uint32_t sample_rate; bool enabled; };

/* Owner handler called on every change */
void on_change(struct weave_observable *obs, void *user_data) {
    struct config cfg;
    WEAVE_OBSERVABLE_GET(settings, &cfg);
    apply_config(&cfg);
}

/* Optional validator can reject invalid values */
int validate(struct weave_observable *obs, const void *new_value, void *user_data) {
    const struct config *cfg = new_value;
    return (cfg->sample_rate <= 10000) ? 0 : -EINVAL;
}

WEAVE_OBSERVABLE_DEFINE(settings, struct config,
                        on_change, WV_IMMEDIATE, NULL,  /* owner handler */
                        validate);                       /* validator */

/* External observers can also subscribe */
WEAVE_OBSERVER_DEFINE(logger, log_change, &log_queue, NULL);
WEAVE_OBSERVER_CONNECT(settings, logger);

/* Update triggers validation, then owner handler, then observers */
struct config new_cfg = {.sample_rate = 100, .enabled = true};
int ret = WEAVE_OBSERVABLE_SET(settings, &new_cfg);
if (ret < 0) { /* Validation failed */ }
```

## When to Use What

| Mechanism | Use Case | Key Feature |
|-----------|----------|-------------|
| **Packet** | Sensor data, network I/O | Zero-copy, ID filtering |
| **Method** | Commands, queries | Type-safe RPC |
| **Observable** | Configuration, status | State + change notifications |

## Building

```bash
# Run all tests
./scripts/run_tests.sh weave

# Generate coverage
./scripts/generate_coverage.sh weave
```

## Configuration

Enable in `prj.conf`:

```kconfig
CONFIG_WEAVE=y
CONFIG_WEAVE_PACKET=y     # For packet routing
CONFIG_WEAVE_METHOD=y     # For RPC
CONFIG_WEAVE_OBSERVABLE=y # For observables
```

## Documentation

See [docs/](docs/) for detailed documentation:

- [Core Concepts](docs/core.rst) - Sources, sinks, queues, wiring
- [Packet](docs/packet.rst) - Zero-copy net_buf routing
- [Method](docs/method.rst) - RPC framework
- [Observable](docs/observable.rst) - Stateful pub/sub

## Samples

- `samples/packet_routing/` - TCP server with sensor data routing
- `samples/sensor_rpc/` - RPC-based sensor service
- `samples/observable/` - Settings management with observers

## License

Apache 2.0
