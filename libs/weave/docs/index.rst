.. _weave:

Weave Message Passing Framework
###############################

The :dfn:`Weave` framework is a lightweight message passing system for Zephyr RTOS
that provides efficient, decoupled communication between software components.

.. contents::
    :local:
    :depth: 2

Introduction
************

Why "Weave"?
============

The name is a play on words: Weave allows kernel **threads** to communicate and
interact with each other - much like threads of fabric being woven together to
create a cohesive whole. Each thread in your system can emit messages, receive
notifications, and call methods across thread boundaries, all woven together
through compile-time connections.

The Problem
===========

Embedded systems often need components to communicate without tight coupling.
Consider a sensor that produces data: the display module, network stack, and
data logger all need this data, but the sensor shouldn't need to know about
any of them.

Traditional approaches have drawbacks:

* **Direct function calls** create tight coupling - the sensor must know about
  every consumer and call each one explicitly
* **Zephyr's zbus** is powerful but can be heavyweight for simple use cases
* **Custom solutions** often reinvent the wheel and lack consistency

Weave provides a middle ground: a lightweight, zero-allocation pub/sub system
that uses Zephyr's native primitives (``k_msgq``, ``net_buf``, spinlocks) with
compile-time wiring for deterministic behavior.

Core Philosophy
===============

Weave is built on several principles:

1. **Zero runtime allocation** - All connections and structures are defined at
   compile time using Zephyr's iterable sections. No malloc, no heap fragmentation.

2. **Compile-time wiring** - Connections between producers and consumers are
   established statically. You can see the full system topology in your code.

3. **Flexible execution** - Handlers can run immediately in the caller's context
   or be queued for deferred processing in another thread.

4. **Native Zephyr integration** - Built on ``k_msgq`` for queuing, ``net_buf``
   for zero-copy packets, and standard kernel primitives.

5. **Single processing loop** - All mechanisms (packets, RPCs, observables) can
   share one message queue, enabling a single thread to handle everything.

Overview
********

Weave provides three communication patterns built on a common Core layer:

.. code-block:: text

    +-------------+-------------+--------------+
    |   Packet    |   Method    |  Observable  |
    | (net_buf)   |   (RPC)     |  (pub/sub)   |
    +-------------+-------------+--------------+
    |                  Core                    |
    |         (sources, sinks, queues)         |
    +------------------------------------------+

* **Packet**: Zero-copy routing of ``net_buf`` packets with reference counting
* **Method**: Type-safe RPC with request/response semantics
* **Observable**: Stateful values that notify observers on change

All three share the same underlying source/sink model and can use the same
message queue, enabling unified event processing.

Key Concepts
************

Sources and Sinks
=================

The fundamental building blocks are **sources** and **sinks**:

* A **source** emits messages (data, events, notifications)
* A **sink** receives messages and processes them via a handler function

Sources and sinks are connected at compile time. When a source emits a message,
all connected sinks receive it:

.. code-block:: text

    Source --+--> Sink A (logger)
             +--> Sink B (display)
             +--> Sink C (network)

This is a one-to-many relationship. A source can have multiple sinks, and a
sink can be connected to multiple sources (many-to-many when combined).

Immediate vs Queued Execution
=============================

When a source emits a message, each sink can process it in one of two modes:

**Immediate Mode** (``WV_IMMEDIATE``)
    The sink's handler runs directly in the caller's context. This is fast
    (no context switch) but blocks the caller until the handler completes.

    Use for: Quick operations, real-time responses, when caller can wait.

    .. code-block:: c

        /* Handler runs in whoever calls weave_source_emit() */
        WEAVE_SINK_DEFINE(my_sink, handler, WV_IMMEDIATE, NULL, WV_NO_OPS);

**Queued Mode** (``&queue``)
    The message is placed in a ``k_msgq`` for later processing. The caller
    returns immediately without waiting for the handler.

    Use for: Long operations, different thread context, decoupling timing.

    .. code-block:: c

        /* Handler runs when service_thread processes the queue */
        WEAVE_SINK_DEFINE(my_sink, handler, &service_queue, NULL, WV_NO_OPS);

This is configured per-sink, so one source can have both immediate and queued
sinks simultaneously.

Message Queues
==============

For queued execution, Weave uses Zephyr's ``k_msgq``. Define a queue with:

.. code-block:: c

    WEAVE_MSGQ_DEFINE(my_queue, 16);  /* 16 message slots */

Multiple sinks can share the same queue. A processing thread drains the queue:

.. code-block:: c

    void service_thread(void) {
        while (1) {
            /* Block until a message arrives, then process all pending */
            weave_process_messages(&my_queue, K_FOREVER);
        }
    }

``weave_process_messages()`` waits for the first message (with the given timeout),
then processes all available messages before returning. This batching reduces
context switches.

Compile-Time Wiring
===================

Connections are established at compile time using macros that leverage Zephyr's
iterable sections:

.. code-block:: c

    /* Define a source */
    WEAVE_SOURCE_DEFINE(my_source, WV_NO_OPS);

    /* Define a sink */
    WEAVE_SINK_DEFINE(my_sink, handler, &queue, user_data, WV_NO_OPS);

    /* Connect them - this creates a static struct in ROM */
    WEAVE_CONNECT(&my_source, &my_sink);

At boot, ``SYS_INIT`` iterates over all ``WEAVE_CONNECT`` entries and links
them together. Benefits:

* **Deterministic** - No runtime failures from allocation
* **Visible** - Grep for ``WEAVE_CONNECT`` to see all connections
* **Efficient** - No linked list traversal at connection time
* **Safe** - Invalid connections fail at compile/link time

The Three Mechanisms
********************

Packet
======

For high-throughput data streams using Zephyr's ``net_buf``:

.. code-block:: c

    /* Source sends packets */
    WEAVE_PACKET_SOURCE_DEFINE(sensor_src, SENSOR_PACKET_ID);

    /* Sink receives packets */
    void on_packet(struct net_buf *buf, void *user_data) {
        /* Process buf... */
        /* net_buf is automatically unref'd after handler returns */
    }
    WEAVE_PACKET_SINK_DEFINE(logger_sink, on_packet, &queue, NULL);

    /* Connect */
    WEAVE_PACKET_CONNECT(&sensor_src, &logger_sink);

    /* Send a packet */
    struct net_buf *buf = net_buf_alloc(&my_pool, K_NO_WAIT);
    /* Fill buf... */
    weave_packet_send(&sensor_src, buf, K_NO_WAIT);

Key features:

* **Zero-copy** - Buffer pointers are passed, not data
* **Reference counting** - Each sink gets a ref, automatically released
* **Packet IDs** - Filter which packets a sink receives

See :doc:`packet` for details.

Method
======

For RPC-style request/response communication:

.. code-block:: c

    /* Define request/response types */
    struct get_temp_req { uint8_t sensor_id; };
    struct get_temp_rsp { int32_t temperature; int status; };

    /* Implement the method handler */
    int handle_get_temp(const struct get_temp_req *req,
                        struct get_temp_rsp *rsp, void *ctx) {
        rsp->temperature = read_sensor(req->sensor_id);
        rsp->status = 0;
        return 0;
    }

    /* Define the method */
    WEAVE_METHOD_DEFINE(get_temp, handle_get_temp, &service_queue, NULL,
                        struct get_temp_req, struct get_temp_rsp);

    /* Call it (synchronously) */
    struct get_temp_req req = { .sensor_id = 1 };
    struct get_temp_rsp rsp;
    int ret = WEAVE_METHOD_CALL(get_temp, &req, &rsp, K_MSEC(100));

Key features:

* **Type-safe** - Compile-time checking of request/response types
* **Synchronous** - Caller blocks until response (with timeout)
* **Stack-allocated** - No heap allocation for call context

See :doc:`method` for details.

Observable
==========

For stateful values that notify observers on change:

.. code-block:: c

    /* Define the value type */
    struct device_config {
        uint32_t sample_rate;
        bool enabled;
    };

    /* Optional: validate changes before applying */
    int validate_config(struct weave_observable *obs,
                        const void *new_value, void *user_data) {
        const struct device_config *cfg = new_value;
        if (cfg->sample_rate > 10000) return -EINVAL;
        return 0;  /* Accept */
    }

    /* Optional: owner handler called on every change */
    void on_config_change(struct weave_observable *obs, void *user_data) {
        struct device_config cfg;
        weave_observable_get_unchecked(obs, &cfg);
        apply_config(&cfg);
    }

    /* Define the observable */
    WEAVE_OBSERVABLE_DEFINE(device_config, struct device_config,
                            on_config_change, WV_IMMEDIATE, NULL,
                            validate_config);

    /* External observers can also subscribe */
    WEAVE_OBSERVER_DEFINE(logger, log_config, &log_queue, NULL);
    WEAVE_OBSERVER_CONNECT(device_config, logger);

    /* Update the value - notifies all observers */
    struct device_config new_cfg = { .sample_rate = 1000, .enabled = true };
    int ret = WEAVE_OBSERVABLE_SET(device_config, &new_cfg);
    if (ret < 0) {
        /* Validation failed */
    }

Key features:

* **Holds state** - Value persists, readable at any time
* **Validation** - Optional validator can reject invalid changes
* **Owner handler** - Called before external observers
* **Multiple observers** - Any number can subscribe

See :doc:`observable` for details.

Choosing a Mechanism
====================

.. list-table::
   :header-rows: 1
   :widths: 15 45 40

   * - Mechanism
     - Use When
     - Examples
   * - **Packet**
     - Streaming data, network I/O, high throughput, variable-size data
     - Sensor readings, network frames, audio buffers
   * - **Method**
     - Commands needing acknowledgment, queries, configuration changes
     - "Get sensor value", "Set LED state", "Start calibration"
   * - **Observable**
     - State that multiple components track, configuration, status
     - Device settings, connection state, sensor calibration

Example: Unified Event Loop
***************************

All mechanisms can share one queue, processed by a single thread:

.. code-block:: c

    /* Shared queue for all mechanisms */
    WEAVE_MSGQ_DEFINE(service_queue, 32);

    /* Packet sink */
    void on_packet(struct net_buf *buf, void *user_data) { /* ... */ }
    WEAVE_PACKET_SINK_DEFINE(packet_sink, on_packet, &service_queue, NULL);

    /* Method */
    int on_rpc(const struct req *req, struct rsp *rsp, void *ctx) { return 0; }
    WEAVE_METHOD_DEFINE(my_method, on_rpc, &service_queue, NULL,
                        struct req, struct rsp);

    /* Observable (owner handler uses shared queue) */
    void on_change(struct weave_observable *obs, void *user_data) { /* ... */ }
    WEAVE_OBSERVABLE_DEFINE(my_state, struct state,
                            on_change, &service_queue, NULL, WV_NO_VALID);

    /* Single thread handles everything */
    void service_thread(void) {
        while (1) {
            weave_process_messages(&service_queue, K_FOREVER);
        }
    }

One thread processes packets, RPCs, and observable notifications in order.

Thread Safety
*************

Weave provides the following guarantees:

* **Source emission** is thread-safe - multiple threads can emit to the same source
* **Queue operations** use ``k_msgq`` which is inherently thread-safe
* **Observable get/set** uses spinlocks for atomic value access
* **Immediate handlers** run in the caller's thread with the source lock released

However:

* **Handler execution** is your responsibility - if multiple threads process
  the same queue, handlers may run concurrently
* **User data** access in handlers is not synchronized by Weave - immediate
  handlers run in the caller's context (not the sink's thread), so protecting
  shared user_data is your responsibility

For simple designs, use one processing thread per queue to avoid handler
concurrency issues.

Configuration
*************

Enable Weave and desired mechanisms in ``prj.conf``:

.. code-block:: kconfig

    CONFIG_WEAVE=y
    CONFIG_WEAVE_PACKET=y      # Zero-copy packet routing
    CONFIG_WEAVE_METHOD=y      # RPC framework
    CONFIG_WEAVE_OBSERVABLE=y  # Stateful observables

Additional options:

* ``CONFIG_WEAVE_LOG_LEVEL``: Logging level (0=off, 1=err, 2=wrn, 3=inf, 4=dbg)
* ``CONFIG_WEAVE_INIT_PRIORITY``: SYS_INIT priority for wiring (default 50)

Getting Started
***************

1. Add Weave to your build (it's a Zephyr module)
2. Enable desired mechanisms in ``prj.conf``
5. Create a processing thread if using queued execution

Start with the samples to see complete working examples.

Detailed Documentation
**********************

.. toctree::
   :maxdepth: 1

   core
   packet
   method
   observable

Samples
*******

The following samples demonstrate Weave usage:

* **Packet Routing** (``samples/packet_routing/``) - TCP server with sensor data
  routing, protocol headers, and remote control commands. Shows multi-source,
  multi-sink packet distribution with filtering.

* **Sensor RPC** (``samples/sensor_rpc/``) - RPC-based sensor service with
  type-safe method calls. Demonstrates synchronous request/response pattern.

* **Observable Settings** (``samples/observable/``) - Configuration management
  with shell commands and multiple observers. Shows validation and owner handlers.

API Reference
*************

See the header files for complete API documentation:

* ``<weave/core.h>`` - Core source/sink primitives
* ``<weave/packet.h>`` - Zero-copy packet routing
* ``<weave/method.h>`` - RPC framework
* ``<weave/observable.h>`` - Stateful observables

----

*This documentation was generated with AI assistance and reviewed by a human.*
