.. _weave_core:

Weave Core
##########

The Core layer provides the foundation for all Weave mechanisms. It implements
generic source/sink message passing with compile-time wiring and flexible
execution modes.

.. contents::
    :local:
    :depth: 2

Concepts
********

Sources
=======

A **source** is a message origin point that distributes messages to one or more
connected sinks. Think of it as a publisher in a pub/sub system - it doesn't know
or care how many subscribers exist or what they do with the messages.

Sources enable one-to-many communication: a single ``weave_source_emit()`` call
can deliver to any number of sinks. This is useful for broadcasting sensor data,
distributing events, or implementing fan-out patterns.

.. code-block:: c

    /* Define a source with optional payload ops */
    WEAVE_SOURCE_DEFINE(my_source, WV_NO_OPS);

    /* Emit to all connected sinks */
    weave_source_emit(&my_source, ptr, K_MSEC(100));

When emitting, the source:

1. Acquires a spinlock to protect the connection list
2. Iterates through all connected sinks
3. For each sink, calls the ref callback (if ops provided) to take a reference
   and optionally filter
4. Delivers to immediate sinks directly, or queues events for deferred sinks
5. Releases the spinlock

The return value indicates how many sinks successfully received the message,
which can be useful for diagnostics or flow control.

Sinks
=====

A **sink** receives messages and processes them via a handler callback. Each sink
has a handler function that is invoked when a message arrives.

.. code-block:: c

    void my_handler(void *ptr, void *user_data) {
        /* Process the message */
    }

    WEAVE_SINK_DEFINE(my_sink, my_handler, WV_IMMEDIATE, NULL, WV_NO_OPS);

**Handler parameters:**

* ``ptr``: The message payload pointer
* ``user_data``: Custom data from sink definition, useful for passing context
  or distinguishing between multiple sinks using the same handler

Execution Modes
===============

Sinks support two execution modes that determine when and where the handler runs:

**Immediate mode** (``WV_IMMEDIATE``):

The handler executes synchronously in the sender's thread context. This provides
the lowest latency since there is no context switch, but the handler blocks the
sender until it completes. Use immediate mode when:

* The handler is fast and non-blocking
* You need the lowest possible latency
* The sender can tolerate being blocked

.. code-block:: c

    /* Handler runs immediately when message arrives */
    WEAVE_SINK_DEFINE(fast_sink, my_handler, WV_IMMEDIATE, NULL, WV_NO_OPS);

**Queued mode** (``&queue``):

The message is placed in a queue and the sender continues immediately. A separate
processing thread later retrieves the message and executes the handler. This
decouples the sender from the handler execution, allowing:

* Handlers that block or take significant time
* Processing in a specific thread context
* Priority control via thread priorities

.. code-block:: c

    /* Define a queue to hold pending messages */
    WEAVE_MSGQ_DEFINE(work_queue, 16);

    /* Handler runs later in processing thread */
    WEAVE_SINK_DEFINE(deferred_sink, my_handler, &work_queue, NULL, WV_NO_OPS);

    /* Processing thread drains the queue */
    void worker_thread(void) {
        while (1) {
            weave_process_messages(&work_queue, K_FOREVER);
        }
    }

Connections
===========

Connections wire sources to sinks. Use ``WEAVE_CONNECT`` for compile-time wiring:

.. code-block:: c

    /* Connect at compile time */
    WEAVE_CONNECT(&my_source, &my_sink);

This creates a static connection structure in ROM. At system initialization,
all connections are wired automatically.

**Modular design pattern:**

Sources and sinks can be defined in separate modules, with declarations in headers.
This allows modules to be developed independently - each module exposes its sources
and sinks through headers, while the application decides how to wire them together.
Modules don't need to know about each other; only the application (e.g., ``main.c``)
includes both headers and creates the connections.

.. code-block:: c

    /* sensor.h - declares source */
    WEAVE_SOURCE_DECLARE(sensor_source);

    /* processor.h - declares sink */
    WEAVE_SINK_DECLARE(processor_sink);

    /* main.c - wires them together */
    #include "sensor.h"
    #include "processor.h"
    WEAVE_CONNECT(&sensor_source, &processor_sink);

Payload Operations
==================

Payload operations provide pluggable lifecycle management for message payloads.
This is what makes Core generic - it doesn't know what kind of data you're passing,
but you can teach it how to manage that data's lifecycle.

The Weave mechanisms use payload ops to implement their specific behaviors:

* **Packet** uses ops to call ``net_buf_ref()``/``net_buf_unref()`` and filter by packet ID
* **Observable** uses passthrough ops (no lifecycle needed for static observables)
* **Method** uses no ops (context lives on caller's stack)

You can also define custom ops for your own buffer types, memory pools, or filtering logic.

.. code-block:: c

    struct weave_payload_ops {
        int (*ref)(void *ptr, struct weave_sink *sink);   /* Take reference, optionally filter */
        void (*unref)(void *ptr);                          /* Release reference */
    };

**ref callback:**

* Called before delivery to each sink
* Return 0 to proceed with delivery
* Return negative errno to skip this sink (e.g., ``-ENOTSUP`` for filter mismatch)
* Can increment reference counts, validate payloads, check sink-specific filters, etc.

**unref callback:**

* Called after handler completes to release the reference
* Also called on delivery failure (queue full, etc.) to clean up
* If NULL, no cleanup is performed

**Example: Reference counting ops**

.. code-block:: c

    static int my_ref(void *ptr, struct weave_sink *sink) {
        struct my_buffer *buf = ptr;
        atomic_inc(&buf->refcount);
        return 0;
    }

    static void my_unref(void *ptr) {
        struct my_buffer *buf = ptr;
        if (atomic_dec(&buf->refcount) == 0) {
            free_buffer(buf);
        }
    }

    static const struct weave_payload_ops my_ops = {
        .ref = my_ref,
        .unref = my_unref,
    };

    WEAVE_SOURCE_DEFINE(my_source, &my_ops);

Usage
*****

Defining Message Queues
=======================

Message queues are required for queued execution mode. They act as buffers between
senders and the processing thread, allowing senders to continue without waiting
for handlers to complete.

.. code-block:: c

    /* Define queue with capacity for 16 events */
    WEAVE_MSGQ_DEFINE(my_queue, 16);

Each event is small (8 bytes on 32-bit systems): just the sink pointer and payload
pointer. The actual payload data is not copied into the queue.

Queue sizing is important: if the queue fills up, new messages are dropped.
Size your queues based on expected burst rates and processing latency.

Processing Messages
===================

Queued sinks require a processing thread that retrieves messages and executes
handlers. This thread is where your handler code actually runs.

.. code-block:: c

    void worker_thread(void) {
        while (1) {
            /* Wait for first message, then drain all available */
            int count = weave_process_messages(&my_queue, K_FOREVER);
            if (count < 0) {
                LOG_ERR("Processing error: %d", count);
            }
        }
    }

``weave_process_messages()`` is designed for efficiency:

1. Waits for first message with given timeout (can block here)
2. Drains all remaining messages without blocking (batch processing)
3. For each message, calls the sink's handler
4. Calls unref (if ops provided) after each handler completes
5. Returns count of messages processed, or negative errno

The batch draining behavior means that if multiple messages arrive while
processing, they are all handled before the thread blocks again.

For threads that need to listen to multiple queues or combine message processing
with other Zephyr primitives, see `Integration with k_poll`_ below.

Direct Sink Send
================

Sometimes you need point-to-point delivery without going through a source.
``weave_sink_send()`` delivers directly to a specific sink:

.. code-block:: c

    /* Send directly to a sink with optional ops for lifecycle management */
    int ret = weave_sink_send(&target_sink, ptr, ops, K_MSEC(100));

    /* Or without lifecycle management */
    int ret = weave_sink_send(&target_sink, ptr, NULL, K_MSEC(100));

This bypasses source routing entirely - useful for reply channels or direct
communication between specific components. The provided ops (if non-NULL) are
used for lifecycle management.

.. note::

   Direct sink send creates coupling between modules: the sender must know about
   and include the sink's header. In contrast, the source/sink pattern with
   ``WEAVE_CONNECT`` keeps modules completely separated - each module only exposes
   its sources and sinks, while the application decides how to wire them together.

Timeout Handling
================

When emitting to multiple sinks with a timeout, time is distributed fairly:

.. code-block:: c

    /* Total timeout shared across all sinks */
    weave_source_emit(&source, ptr, K_MSEC(100));

The timeout is converted to an absolute deadline. Each sink gets the remaining
time, ensuring later sinks still have a chance even if earlier ones block.

Thread Safety
*************

Weave Core is designed for concurrent use from multiple threads and ISRs.
Understanding the synchronization model helps you design correct systems.

**Spinlock protection:**

Sources use per-source spinlocks to protect their connection lists during
iteration. This means:

* Multiple threads can emit to different sources concurrently
* Emitting to the same source from multiple threads is safe (serialized)
* Critical sections are very short (microseconds) to minimize latency
* Spinlocks are IRQ-safe, so emitting from ISRs works correctly
* The lock is released before calling handlers, so handlers can emit to other sources

**Queue operations:**

Message queues use Zephyr's ``k_msgq``, which is inherently thread-safe:

* Multiple senders can enqueue to the same queue concurrently
* ``k_msgq_put`` with ``K_NO_WAIT`` is safe from interrupt context
* The processing thread has exclusive access while dequeuing

**Handler execution:**

Where your handler runs depends on the execution mode:

* **Immediate handlers** run in the sender's context. If the sender is a
  high-priority thread or ISR, your handler runs at that priority. Keep it fast.
* **Queued handlers** run in the processing thread's context at that thread's
  priority. They can safely block or take longer.
* Multiple sinks sharing a queue are processed sequentially by the same thread,
  providing natural serialization if needed.

Design Patterns
***************

Shared Processing Queue
=======================

Multiple sinks can share a single message queue. This is useful when you want
messages from different sources to be processed by a single thread, ensuring
sequential execution and avoiding the overhead of multiple threads.

Each event in the queue carries the sink pointer, so the correct handler is
called for each message regardless of which sink it was delivered to.

.. code-block:: c

    WEAVE_MSGQ_DEFINE(shared_queue, 32);

    WEAVE_SINK_DEFINE(sink_a, handler_a, &shared_queue, (void *)1, WV_NO_OPS);
    WEAVE_SINK_DEFINE(sink_b, handler_b, &shared_queue, (void *)2, WV_NO_OPS);
    WEAVE_SINK_DEFINE(sink_c, handler_c, &shared_queue, (void *)3, WV_NO_OPS);

    /* Single thread processes all three sinks */
    void worker(void) {
        while (1) {
            weave_process_messages(&shared_queue, K_FOREVER);
        }
    }

This pattern is common in service modules where multiple method calls or
event types should be handled by the same thread to avoid concurrency issues.

Integration with k_poll
=======================

When a thread needs to listen to multiple event sources simultaneously, use
``k_poll`` to efficiently wait on all of them. This avoids busy-waiting and
allows combining Weave message processing with other Zephyr primitives like
semaphores, FIFOs, timers, or multiple Weave queues.

Common use cases:

* Listening to multiple Weave queues in a single thread
* Combining message processing with shutdown signals
* Integrating with hardware event notifications
* Adding periodic timeouts for housekeeping tasks

.. code-block:: c

    enum { EVENT_QUEUE, EVENT_SHUTDOWN, EVENT_COUNT };

    static struct k_poll_event events[EVENT_COUNT] = {
        K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
                                        K_POLL_MODE_NOTIFY_ONLY,
                                        &my_queue, 0),
        K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
                                        K_POLL_MODE_NOTIFY_ONLY,
                                        &shutdown_sem, 0),
    };

    void worker(void) {
        while (1) {
            k_poll(events, EVENT_COUNT, K_FOREVER);

            ARRAY_FOR_EACH(events, i) {
                if (events[i].state == K_POLL_STATE_NOT_READY) {
                    continue;
                }
                events[i].state = K_POLL_STATE_NOT_READY;

                switch (i) {
                case EVENT_QUEUE:
                    weave_process_messages(&my_queue, K_NO_WAIT);
                    break;
                case EVENT_SHUTDOWN:
                    return;
                }
            }
        }
    }

ISR Usage
=========

Weave Core is designed to work safely from interrupt context, making it suitable
for hardware-driven data flows. The key is using queued sinks: the ISR quickly
enqueues the message and returns, while a thread handles the actual processing.

.. code-block:: c

    void my_isr(void *arg) {
        /* Queue for deferred processing - ISR safe */
        weave_source_emit(&isr_source, ptr, K_NO_WAIT);
    }

Guidelines for ISR usage:

* Always use ``K_NO_WAIT`` for emit/send operations - ISRs cannot block
* Use queued sinks to defer processing to thread context
* If using immediate sinks with ISR sources, keep handlers extremely short
  (just set a flag or copy minimal data)
* All spinlock operations in Core are IRQ-safe (interrupts disabled during critical sections)

----

*This documentation was generated with AI assistance and reviewed by a human.*
