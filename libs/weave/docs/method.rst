.. _weave_method:

Weave Method
############

Weave Method provides type-safe remote procedure calls (RPC) with request/response
semantics. Methods execute synchronously from the caller's perspective, with optional
queued execution for thread isolation.

.. contents::
    :local:
    :depth: 2

Introduction
************

Why RPC?
========

In embedded systems, you often need to invoke operations in a different thread context.
For example, a network thread might need to read a sensor value, but sensor access must
happen in the sensor service thread. Traditional approaches have problems:

* **Direct function calls** execute in the caller's thread, which may not have the
  right permissions, priority, or access to thread-local state.
* **Message queues** require manual serialization, matching request IDs to responses,
  and custom waiting logic.
* **Global shared state** requires careful synchronization and is error-prone.

Weave Method provides a clean abstraction: define a method with request/response types,
and callers invoke it like a function call. The framework handles queuing, thread
switching, synchronization, and response delivery - all with compile-time type safety.

When to Use Method
==================

Method is ideal for:

* **Service APIs** - Expose sensor reads, configuration changes, or device control
  as callable methods
* **Thread isolation** - Execute operations in a specific thread context (e.g., all
  hardware access in one thread)
* **Request/response patterns** - When you need confirmation that an operation completed
  and want to retrieve results
* **Command handling** - Process commands from shell, network, or other interfaces

For streaming data without responses, consider :doc:`packet` instead. For state that
multiple components need to track, consider :doc:`observable`.

Concepts
********

RPC Semantics
=============

Method calls follow synchronous RPC semantics - the caller blocks until the handler
completes and returns a response. This is the familiar function-call model, but the
handler can execute in a different thread:

1. Caller prepares request on stack
2. Caller invokes method (blocks until complete)
3. Handler executes (in caller's context or processing thread)
4. Handler writes response and returns a status code
5. Caller receives response and the handler's return code

The handler's return value becomes the return value of ``WEAVE_METHOD_CALL``. This
allows handlers to report success (0) or errors (negative errno) directly to the
caller, just like a regular function call.

.. code-block:: text

    Caller                          Handler
      |                                |
      |-- WEAVE_METHOD_CALL() -------->|
      |         (blocks)               |
      |                        [executes handler]
      |                        [writes response]
      |                        [returns status]
      |<------- status ----------------|
      |                                |
    [ret == handler's return value]

Zero Allocation
===============

Method calls use no heap allocation. All data lives on the caller's stack:

* Request structure: caller's stack
* Response structure: caller's stack
* Context (for sync): caller's stack or embedded semaphore

This design has several benefits:

* **Predictable memory usage** - No heap fragmentation or allocation failures at runtime
* **Real-time friendly** - No variable-latency malloc/free operations
* **Simple resource management** - When the function returns, all resources are released
* **No cleanup required** - The stack naturally reclaims memory

The only dynamic resource is the message queue slot, which is statically allocated
at compile time with a fixed capacity you control.

Type Safety
===========

The ``WEAVE_METHOD_CALL`` macro provides compile-time type checking:

* Request pointer type must match method definition
* Response pointer type must match method definition
* Sizes are automatically derived from pointer types

Type mismatches cause compiler errors, not runtime failures.

Usage
*****

Defining Methods
================

A method consists of:

* **Handler function** - Implements the actual logic, receives request and writes response
* **Request type** - Input data structure (or ``WV_VOID`` for no input)
* **Response type** - Output data structure (or ``WV_VOID`` for no output)
* **Execution mode** - Where the handler runs: immediate or queued
* **User data** - Optional context pointer passed to handler

Execution Modes
---------------

**Queued execution** (``&queue``) is the typical choice for service methods:

* Handler runs in the processing thread that drains the queue
* Caller blocks until handler completes (via semaphore)
* Provides thread isolation - all handlers for a queue run in one thread
* Serializes concurrent calls naturally (processed in order)

**Immediate execution** (``WV_IMMEDIATE``) runs the handler directly:

* Handler runs in the caller's thread context
* No queuing overhead, lowest latency
* Use when: handler is fast, no thread isolation needed, caller context is acceptable

**Basic method definition:**

.. code-block:: c

    /* Request and response structures */
    struct read_sensor_request {
        uint8_t channel;
    };

    struct read_sensor_response {
        int32_t value;
        uint32_t timestamp;
    };

    /* Handler function */
    int read_sensor_handler(const struct read_sensor_request *req,
                            struct read_sensor_response *res,
                            void *user_data) {
        res->value = get_sensor_value(req->channel);
        res->timestamp = k_uptime_get_32();
        return 0;  /* Success */
    }

    /* Method definition - queued execution */
    WEAVE_MSGQ_DEFINE(sensor_queue, 8);
    WEAVE_METHOD_DEFINE(read_sensor, read_sensor_handler, &sensor_queue, NULL,
                        struct read_sensor_request, struct read_sensor_response);

**Immediate execution:**

.. code-block:: c

    /* Handler runs in caller's context */
    WEAVE_METHOD_DEFINE(get_status, status_handler, WV_IMMEDIATE, NULL,
                        struct status_request, struct status_response);

Void Types
==========

Use ``WV_VOID`` for methods with no request or no response:

.. code-block:: c

    /* No request - only response */
    int get_stats_handler(const void *req, struct stats_response *res, void *user_data) {
        res->total_reads = stats.total_reads;
        res->errors = stats.errors;
        return 0;
    }

    WEAVE_METHOD_DEFINE(get_stats, get_stats_handler, &queue, NULL,
                        WV_VOID, struct stats_response);

    /* No response - only request */
    int set_config_handler(const struct config_request *req, void *res, void *user_data) {
        apply_config(req);
        return 0;
    }

    WEAVE_METHOD_DEFINE(set_config, set_config_handler, &queue, NULL,
                        struct config_request, WV_VOID);

Calling Methods
===============

**Synchronous call (blocking):**

.. code-block:: c

    struct read_sensor_request req = {.channel = 0};
    struct read_sensor_response res;

    int ret = WEAVE_METHOD_CALL(read_sensor, &req, &res);
    if (ret == 0) {
        LOG_INF("Value: %d at %u", res.value, res.timestamp);
    } else {
        LOG_ERR("Method failed: %d", ret);
    }

**Calling with void types:**

.. code-block:: c

    /* No request */
    struct stats_response stats;
    ret = WEAVE_METHOD_CALL(get_stats, WV_VOID, &stats);

    /* No response */
    struct config_request cfg = {.rate = 100};
    ret = WEAVE_METHOD_CALL(set_config, &cfg, WV_VOID);

Asynchronous Calls
==================

The synchronous ``WEAVE_METHOD_CALL`` is convenient but blocks the caller. When you
need to do other work while waiting, or want to set a timeout, use the async pattern:

1. ``WEAVE_METHOD_CALL_ASYNC`` - Queue the request and return immediately
2. Do other work while the handler executes
3. ``WEAVE_METHOD_WAIT`` - Block until completion (with optional timeout)

This is useful for parallel operations, implementing timeouts, or when the caller
needs to remain responsive:

.. code-block:: c

    struct weave_method_context ctx;
    struct read_sensor_request req = {.channel = 0};
    struct read_sensor_response res;

    /* Queue the call - returns immediately */
    int ret = WEAVE_METHOD_CALL_ASYNC(read_sensor, &req, &res, &ctx);
    if (ret != 0) {
        LOG_ERR("Failed to queue: %d", ret);
        return ret;
    }

    /* Do other work while method executes... */
    do_other_work();

    /* Wait for completion */
    ret = WEAVE_METHOD_WAIT(&ctx, K_MSEC(100));
    if (ret == -EAGAIN) {
        LOG_WRN("Timeout waiting for method");
    } else if (ret == 0) {
        LOG_INF("Value: %d", res.value);
    }

.. important::

   The context, request, and response buffers must remain valid until
   ``WEAVE_METHOD_WAIT`` returns. They live on the caller's stack and
   are accessed by the handler.

Processing Thread
=================

Queued methods don't execute themselves - a processing thread must drain the queue.
This thread is where your handlers actually run, giving you control over:

* **Thread priority** - Set appropriate priority for the service
* **Stack size** - Allocate enough stack for handler execution
* **Isolation** - All handlers for a queue run in this single thread

The processing loop is simple - call ``weave_process_messages()`` which waits for
messages and executes handlers:

.. code-block:: c

    void sensor_service_thread(void) {
        while (1) {
            weave_process_messages(&sensor_queue, K_FOREVER);
        }
    }

    K_THREAD_DEFINE(sensor_svc, 1024, sensor_service_thread,
                    NULL, NULL, NULL, 7, 0, 0);

Header Declarations
===================

For modular code, declare methods in headers:

.. code-block:: c

    /* sensor_module.h */
    #include <weave/method.h>

    struct read_sensor_request { uint8_t channel; };
    struct read_sensor_response { int32_t value; uint32_t timestamp; };

    WEAVE_METHOD_DECLARE(read_sensor,
                         struct read_sensor_request,
                         struct read_sensor_response);

    struct get_stats_response { uint32_t total; uint32_t errors; };
    WEAVE_METHOD_DECLARE(get_stats, WV_VOID, struct get_stats_response);

    /* sensor_module.c */
    #include "sensor_module.h"

    WEAVE_METHOD_DEFINE(read_sensor, read_handler, &queue, NULL,
                        struct read_sensor_request, struct read_sensor_response);

    WEAVE_METHOD_DEFINE(get_stats, stats_handler, &queue, NULL,
                        WV_VOID, struct get_stats_response);

Error Handling
==============

The handler's return value is passed directly to the caller. Whatever your handler
returns becomes the return value of ``WEAVE_METHOD_CALL``:

.. code-block:: c

    /* Handler */
    int read_handler(const struct request *req, struct response *res, void *ctx) {
        if (req->channel >= MAX_CHANNELS) {
            return -EINVAL;  /* This error goes to caller */
        }
        if (!sensor_ready(req->channel)) {
            return -EBUSY;   /* This error goes to caller */
        }
        res->value = read_sensor(req->channel);
        return 0;  /* Success goes to caller */
    }

    /* Caller */
    struct request req = {.channel = 5};
    struct response res;
    int ret = WEAVE_METHOD_CALL(read_sensor, &req, &res);

    /* ret == handler's return value */
    if (ret == -EINVAL) {
        LOG_ERR("Invalid channel");
    } else if (ret == -EBUSY) {
        LOG_WRN("Sensor not ready, retrying...");
    } else if (ret == 0) {
        LOG_INF("Value: %d", res.value);
    }

This makes error handling straightforward - define error codes in your handler
and check them at the call site, just like a regular function.

**Framework errors** occur before your handler runs:

* ``-EINVAL``: Size mismatch between call and definition (indicates a bug)
* ``-EAGAIN``: Timeout waiting for completion (async wait)
* ``-ENOBUFS``: Queue full, couldn't enqueue the request

These are rare in practice - size mismatches are caught during development, and
queue-full errors can be avoided by sizing queues appropriately.

Design Patterns
***************

Service Module
==============

A common pattern is to encapsulate related methods in a service module. The module:

* Defines all its methods in a header (for callers)
* Implements handlers in a source file
* Uses a single shared queue for all methods
* Runs a single processing thread

This creates a clean API boundary and ensures all operations on the service's
resources are serialized through one thread:

.. code-block:: c

    /* sensor_service.h */
    struct sensor_read_req { uint8_t channel; };
    struct sensor_read_res { int32_t value; };
    WEAVE_METHOD_DECLARE(sensor_read, struct sensor_read_req, struct sensor_read_res);

    struct sensor_config_req { uint16_t rate_ms; bool enable; };
    WEAVE_METHOD_DECLARE(sensor_config, struct sensor_config_req, WV_VOID);

    WEAVE_METHOD_DECLARE(sensor_stats, WV_VOID, struct sensor_stats_res);

    /* sensor_service.c */
    static WEAVE_MSGQ_DEFINE(sensor_queue, 8);

    /* All methods share one queue = one service thread */
    WEAVE_METHOD_DEFINE(sensor_read, read_handler, &sensor_queue, NULL, ...);
    WEAVE_METHOD_DEFINE(sensor_config, config_handler, &sensor_queue, NULL, ...);
    WEAVE_METHOD_DEFINE(sensor_stats, stats_handler, &sensor_queue, NULL, ...);

    void sensor_service_thread(void) {
        while (1) {
            weave_process_messages(&sensor_queue, K_FOREVER);
        }
    }

Command Handler
===============

When you have multiple command types that share the same request/response flow,
you can use an enum to dispatch in a single handler. This is useful for protocol
implementations or shell command handlers:

.. code-block:: c

    enum cmd_type { CMD_READ, CMD_WRITE, CMD_RESET };

    struct command_request {
        enum cmd_type type;
        uint16_t address;
        uint32_t value;
    };

    struct command_response {
        int status;
        uint32_t value;
    };

    int command_handler(const struct command_request *req,
                        struct command_response *res,
                        void *user_data) {
        switch (req->type) {
        case CMD_READ:
            res->value = read_register(req->address);
            res->status = 0;
            break;
        case CMD_WRITE:
            write_register(req->address, req->value);
            res->status = 0;
            break;
        case CMD_RESET:
            reset_device();
            res->status = 0;
            break;
        default:
            res->status = -EINVAL;
        }
        return res->status;
    }

    WEAVE_METHOD_DEFINE(device_command, command_handler, &cmd_queue, NULL,
                        struct command_request, struct command_response);

Timeout Handling
================

When calling methods that might take a long time, or when you need bounded latency,
use the async pattern with a timeout. Be aware that a timeout doesn't cancel the
handler - it just stops waiting:

.. code-block:: c

    struct weave_method_context ctx;
    struct request req = {...};
    struct response res;

    int ret = WEAVE_METHOD_CALL_ASYNC(slow_method, &req, &res, &ctx);
    if (ret != 0) return ret;

    /* Wait with timeout */
    ret = WEAVE_METHOD_WAIT(&ctx, K_MSEC(500));
    if (ret == -EAGAIN) {
        LOG_WRN("Method timed out - service may be overloaded");
        /* Note: handler may still execute later */
        return -ETIMEDOUT;
    }

    return ret;

.. warning::

   After a timeout, the handler may still execute and write to the response buffer.
   If you return from your function, the response buffer (on stack) becomes invalid.
   Design handlers to be safe if the caller has given up, or use a longer timeout.

Configuration
*************

Enable method support in ``prj.conf``:

.. code-block:: kconfig

    CONFIG_WEAVE=y
    CONFIG_WEAVE_METHOD=y

Thread Safety
*************

Weave Method is designed for safe concurrent use:

**Calling methods:**

* Methods can be called from any thread context
* Multiple threads can call the same method concurrently
* Each call is independent - request/response buffers are on each caller's stack

**Queued execution:**

* All handlers for a queue run in the processing thread
* Handlers execute one at a time, serializing access to shared resources
* Callers block on their individual semaphores until their handler completes
* This natural serialization often eliminates the need for explicit locking in handlers

**Immediate execution:**

* Handler runs directly in the caller's thread
* If multiple threads call an immediate method concurrently, handlers run concurrently
* You must handle any necessary synchronization in the handler

**Best practice:** Use queued methods for operations that access shared state. The
processing thread provides natural serialization, making handlers simpler and safer.

----

*This documentation was generated with AI assistance and reviewed by a human.*
