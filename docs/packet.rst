.. _weave_packet:

Weave Packet
############

Weave Packet provides **zero-copy** packet distribution using Zephyr's ``net_buf``.
Data flows through your system without ever being copied - handlers receive pointers
to the same buffer memory. Reference counting manages the buffer lifecycle automatically.

.. contents::
    :local:
    :depth: 2

Introduction
************

Why Packet?
===========

Weave Packet is **zero-copy by design**. When you send a buffer through the system,
the data never gets copied - handlers receive pointers to the same memory. This is
critical for high-throughput embedded systems where copying megabytes per second
would saturate your CPU.

The zero-copy guarantee comes from Zephyr's ``net_buf`` reference counting.

Additionally, ``net_buf`` fragment chains let you add headers without copying payloads.
Each processing stage allocates a small header buffer and chains it to the existing
data - the original payload is never touched:

.. code-block:: text

    [header2] → [header1] → [sensor data]
       ↑           ↑            ↑
    16 bytes    8 bytes     1500 bytes (never copied)

Weave Packet wraps this with clean routing. Sources connect to sinks, handlers process
buffers, and reference counting ensures buffers are freed when all consumers are done.

When to Use Packet
==================

Packet is ideal for:

* **High-throughput data** - Sensor streams, audio buffers, network frames - all zero-copy
* **Fan-out patterns** - One producer, multiple consumers without copying
* **Pipeline processing** - Chain of stages that add headers without copying payloads
* **Network protocols** - Build packet headers layer by layer using fragment chains

For request/response operations, consider :doc:`method` instead. For state that
persists between updates, consider :doc:`observable`.

Concepts
********

Zero-Copy Distribution
======================

The core principle: **data is never copied**. When you send a buffer to multiple
sinks, each handler receives a pointer to the same memory. This is achieved through
``net_buf`` reference counting:

1. Source allocates a buffer from a pool
2. When sending to N sinks, reference count is incremented N times
3. Each sink's handler receives a borrowed reference
4. Framework releases reference after handler completes

The buffer payload is never copied - all sinks read from the same memory. This is
safe because handlers receive **borrowed** references and must not modify the buffer
(or must coordinate if they do).

.. code-block:: text

    Allocation:     buf->ref = 1 (caller owns it)
    Send to 3 sinks: buf->ref = 4 (1 caller + 3 sinks)
    After handlers:  buf->ref = 1 (sinks released their refs)
    Caller unref:    buf->ref = 0 (buffer returned to pool)

This lifecycle is automatic - you allocate, fill, and send. The framework handles
all reference counting.

Fragment Chains
===============

Beyond fan-out, ``net_buf`` fragment chains enable zero-copy header addition. Each
buffer has a ``frags`` pointer that links to another buffer. When you chain buffers,
they form a single logical packet that network APIs process as one unit.

To add a header without copying:

1. Allocate a small buffer for the header
2. Fill the header with protocol data
3. Take a reference to the payload buffer (you don't own it in a handler)
4. Chain the payload as a fragment: ``net_buf_frag_add(header, payload)``
5. Send the header buffer - the chain travels together

.. code-block:: c

    void add_header(struct net_buf *payload_ref, void *user_data) {
        struct net_buf *header = weave_packet_alloc(&header_pool, K_NO_WAIT);
        if (!header) return;

        /* Build 8-byte header */
        net_buf_add_le32(header, MAGIC);
        net_buf_add_le16(header, payload_ref->len);
        net_buf_add_le16(header, checksum(payload_ref));

        /* Take ownership ref since handler only borrows payload_ref */
        struct net_buf *payload_owned = net_buf_ref(payload_ref);
        net_buf_frag_add(header, payload_owned);

        /* Send - header carries payload with it */
        weave_packet_send(&output, header, K_NO_WAIT);
    }

The payload data is never copied. The header buffer (8 bytes) points to the payload
buffer (possibly 1500 bytes), and both travel through the system as one packet.

Packet Metadata
===============

Each buffer carries metadata in its user data area. This metadata is useful for
routing, sequencing, and timing analysis:

* ``packet_id``: 8-bit ID for filtering (255 = ANY)
* ``client_id``: 8-bit ID for reply routing
* ``counter``: 16-bit auto-incrementing sequence number
* ``timestamp``: System time at allocation (ticks or cycles)

Metadata is automatically initialized during allocation.

Handler Ownership
=================

.. important::

   Handlers receive **borrowed** buffer references. The framework manages
   all reference counting.

   **NEVER** call ``net_buf_unref()`` in a handler.

The framework takes a reference before calling your handler and releases it after
your handler returns. Your handler can read and process the buffer freely, but
must not release it.

If you need to keep the buffer beyond handler lifetime (e.g., chain it to another
buffer for forwarding), take your own reference first:

.. code-block:: c

    static inline void processor_handler(struct net_buf *buf_ref, void *user_data) {
        struct net_buf *header = weave_packet_alloc(&header_pool, K_NO_WAIT);
        if (!header) return;

        /* Add header data */
        net_buf_add_le32(header, MAGIC_NUMBER);
        net_buf_add_le32(header, buf_ref->len);

        /* Take ownership ref since handler only borrows buf_ref */
        struct net_buf *buf_owned = net_buf_ref(buf_ref);
        net_buf_frag_add(header, buf_owned);

        /* Forward the complete packet */
        weave_packet_send(&output_source, header, K_NO_WAIT);
    }

The ``_ref`` suffix on the parameter name reminds you it's borrowed. After
``net_buf_ref()``, you own ``buf_owned`` and are responsible for it (here,
``weave_packet_send`` consumes it).

ID-Based Filtering
==================

Not every sink needs every packet. Weave Packet supports filtering by packet ID,
allowing specialized handlers to receive only the packets they care about. This is
more efficient than receiving all packets and discarding unwanted ones in the handler.

Sinks can filter packets by ID:

.. code-block:: c

    /* Only receives packets with ID 0x01 */
    WEAVE_PACKET_SINK_DEFINE(type1_sink, handler, WV_IMMEDIATE, 0x01, NULL);

    /* Receives all packets */
    WEAVE_PACKET_SINK_DEFINE(all_sink, handler, WV_IMMEDIATE, WV_NO_FILTER, NULL);

Filtering happens in the ref callback. Non-matching packets are skipped without
taking a reference, avoiding unnecessary overhead.

Usage
*****

Buffer Pools
============

Weave Packet uses Zephyr's ``net_buf`` pools for buffer management. Pools are
statically allocated - no runtime allocation, no fragmentation. Each pool has a
fixed number of buffers of a fixed size.

When you allocate from a pool, Weave automatically initializes the packet metadata
(sequence counter, timestamp, packet ID). Define pools with:

.. code-block:: c

    /* 16 buffers, 128 bytes each */
    WEAVE_PACKET_POOL_DEFINE(sensor_pool, 16, 128, NULL);

    /* Allocate with default ID (ANY) */
    struct net_buf *buf = weave_packet_alloc(&sensor_pool, K_NO_WAIT);

    /* Allocate with specific ID */
    struct net_buf *buf = weave_packet_alloc_with_id(&sensor_pool, 0x01, K_NO_WAIT);

Each allocation:

* Increments the pool's sequence counter
* Sets timestamp to current time
* Initializes packet_id (default or specified)

Sources and Sinks
=================

Sources emit packets; sinks receive them. A source can connect to multiple sinks
(fan-out), and a sink can connect to multiple sources (fan-in). Connections are
established at compile time with ``WEAVE_CONNECT``.

**Defining a source:**

.. code-block:: c

    WEAVE_PACKET_SOURCE_DEFINE(sensor_source);

**Defining sinks:**

.. code-block:: c

    static inline void data_handler(struct net_buf *buf, void *user_data) {
        LOG_INF("Received %d bytes", buf->len);
        process_data(buf->data, buf->len);
        /* Do NOT call net_buf_unref() - framework handles it */
    }

    /* Immediate execution, accept all packets */
    WEAVE_PACKET_SINK_DEFINE(logger_sink, data_handler,
                             WV_IMMEDIATE, WV_NO_FILTER, NULL);

    /* Queued execution, filter by ID */
    WEAVE_MSGQ_DEFINE(proc_queue, 32);
    WEAVE_PACKET_SINK_DEFINE(processor_sink, data_handler,
                             &proc_queue, 0x01, NULL);

**Wiring connections:**

.. code-block:: c

    WEAVE_CONNECT(&sensor_source, &logger_sink);
    WEAVE_CONNECT(&sensor_source, &processor_sink);

Sending Packets
===============

After filling a buffer with data, send it through a source. The source distributes
the packet to all connected sinks, handling reference counting automatically.

Two send variants are available:

**Consuming send** (releases caller's reference):

.. code-block:: c

    struct net_buf *buf = weave_packet_alloc(&pool, K_NO_WAIT);
    net_buf_add_mem(buf, data, len);

    /* Consumes the buffer - do not use buf after this */
    int delivered = weave_packet_send(&sensor_source, buf, K_NO_WAIT);

**Preserving send** (keeps caller's reference):

.. code-block:: c

    struct net_buf *buf = weave_packet_alloc(&pool, K_NO_WAIT);
    net_buf_add_mem(buf, data, len);

    /* Preserves reference - caller can continue using buf */
    int delivered = weave_packet_send_ref(&sensor_source, buf, K_NO_WAIT);

    /* Must unref when done */
    net_buf_unref(buf);

The consuming variant is more efficient (avoids extra ref/unref) and is the
recommended default.

Metadata Access
===============

Read and write metadata using accessor functions:

.. code-block:: c

    /* In handler */
    void handler(struct net_buf *buf, void *user_data) {
        uint8_t packet_id;
        uint16_t counter;
        uint32_t timestamp;

        weave_packet_get_id(buf, &packet_id);
        weave_packet_get_counter(buf, &counter);
        weave_packet_get_timestamp_ticks(buf, &timestamp);

        LOG_INF("Packet ID=0x%02x, seq=%u, age=%u ms",
                packet_id, counter,
                k_ticks_to_ms_floor32(k_uptime_ticks() - timestamp));
    }

    /* Before sending */
    weave_packet_set_id(buf, 0x42);
    weave_packet_set_client_id(buf, client_id);

Header Declarations
===================

For modular code, declare sources and sinks in headers. This allows modules to be
developed independently - each module exposes its sources and sinks, while the
application decides how to wire them together:

.. code-block:: c

    /* sensor.h */
    #include <weave/packet.h>

    WEAVE_PACKET_SOURCE_DECLARE(sensor_source);

    /* processor.h */
    #include <weave/packet.h>

    WEAVE_PACKET_SINK_DECLARE(processor_sink);

    /* main.c - wires them together */
    #include "sensor.h"
    #include "processor.h"

    WEAVE_CONNECT(&sensor_source, &processor_sink);

Design Patterns
***************

Processing Pipeline
===================

A powerful pattern is chaining processing stages. Each stage receives a packet, does
some work (possibly adding headers), and forwards to the next stage. The key insight
is that you can add headers **without copying the payload** by using ``net_buf``
fragment chains.

Chain processing stages with header addition:

.. code-block:: text

    sensor_source → processor_sink/source → protocol_sink/source → network_sink

.. code-block:: c

    /* Processor: receives raw data, adds header, forwards */
    WEAVE_PACKET_POOL_DEFINE(header_pool, 16, 16, NULL);
    WEAVE_PACKET_SOURCE_DEFINE(processor_output);

    static inline void processor_handler(struct net_buf *data_ref, void *user_data) {
        struct net_buf *header = weave_packet_alloc(&header_pool, K_NO_WAIT);
        if (!header) return;

        /* Build header */
        uint8_t packet_id;
        uint16_t counter;
        weave_packet_get_id(data_ref, &packet_id);
        weave_packet_get_counter(data_ref, &counter);

        net_buf_add_u8(header, packet_id);
        net_buf_add_le16(header, counter);
        net_buf_add_le16(header, data_ref->len);

        /* Take ownership ref - handler only borrows data_ref */
        struct net_buf *data_owned = net_buf_ref(data_ref);
        net_buf_frag_add(header, data_owned);

        weave_packet_send(&processor_output, header, K_NO_WAIT);
    }

    WEAVE_PACKET_SINK_DEFINE(processor_input, processor_handler, WV_IMMEDIATE, WV_NO_FILTER, NULL);

    /* Wiring */
    WEAVE_CONNECT(&sensor_source, &processor_input);
    WEAVE_CONNECT(&processor_output, &network_sink);

Protocol Parser with ID Routing
===============================

A common pattern is parsing incoming protocol data and routing by message type. The
parser reads the header, extracts the type, sets the packet ID, and forwards. Handlers
filter by ID to receive only their message types:

.. code-block:: c

    /* Parser receives raw frames, extracts type, forwards payload */
    WEAVE_PACKET_SOURCE_DEFINE(parsed_output);

    static inline void parser_handler(struct net_buf *raw_ref, void *user_data) {
        /* Parse header (first byte is message type) */
        uint8_t msg_type = net_buf_pull_u8(raw_ref);

        /* Set packet ID to message type for routing */
        weave_packet_set_id(raw_ref, msg_type);

        /* Forward - send_ref takes its own reference */
        weave_packet_send_ref(&parsed_output, raw_ref, K_NO_WAIT);
    }

    WEAVE_PACKET_SINK_DEFINE(parser_input, parser_handler, WV_IMMEDIATE, WV_NO_FILTER, NULL);

    /* Type-specific handlers filter by ID */
    WEAVE_PACKET_SINK_DEFINE(cmd_handler, process_cmd, WV_IMMEDIATE, 0x01, NULL);
    WEAVE_PACKET_SINK_DEFINE(data_handler, process_data, WV_IMMEDIATE, 0x02, NULL);
    WEAVE_PACKET_SINK_DEFINE(status_handler, process_status, WV_IMMEDIATE, 0x03, NULL);

    /* Wire parser output to all handlers - each receives only matching IDs */
    WEAVE_CONNECT(&parsed_output, &cmd_handler);
    WEAVE_CONNECT(&parsed_output, &data_handler);
    WEAVE_CONNECT(&parsed_output, &status_handler);

    /* Wire raw input to parser */
    WEAVE_CONNECT(&serial_source, &parser_input);

The parser is generic - it doesn't know about specific message types. Handlers subscribe
to the parsed output and filter by ID. Adding a new message type requires only a new
handler, not changes to the parser.

Multiplexing with Header Addition
==================================

The inverse of parsing: multiple sources feed a single sink that adds headers based
on source ID. Each sensor allocates with its own ID, and the framing sink uses that
ID to build the appropriate protocol header:

.. code-block:: c

    /* Sensors allocate with their own IDs */
    #define SENSOR_ACCEL  0x01
    #define SENSOR_GYRO   0x02
    #define SENSOR_MAG    0x03

    WEAVE_PACKET_POOL_DEFINE(sensor_pool, 16, 64, NULL);
    WEAVE_PACKET_SOURCE_DEFINE(accel_source);
    WEAVE_PACKET_SOURCE_DEFINE(gyro_source);
    WEAVE_PACKET_SOURCE_DEFINE(mag_source);

    /* Framing sink adds header based on packet ID */
    WEAVE_PACKET_POOL_DEFINE(frame_pool, 16, 8, NULL);
    WEAVE_PACKET_SOURCE_DEFINE(framed_output);

    static inline void framing_handler(struct net_buf *data_ref, void *user_data) {
        struct net_buf *header = weave_packet_alloc(&frame_pool, K_NO_WAIT);
        if (!header) return;

        /* Build header using packet ID as sensor type */
        uint8_t sensor_id;
        weave_packet_get_id(data_ref, &sensor_id);
        net_buf_add_u8(header, sensor_id);
        net_buf_add_le16(header, data_ref->len);

        /* Chain payload */
        struct net_buf *data_owned = net_buf_ref(data_ref);
        net_buf_frag_add(header, data_owned);

        weave_packet_send(&framed_output, header, K_NO_WAIT);
    }

    WEAVE_PACKET_SINK_DEFINE(framing_sink, framing_handler, WV_IMMEDIATE, WV_NO_FILTER, NULL);

    /* All sensors connect to the same framing sink */
    WEAVE_CONNECT(&accel_source, &framing_sink);
    WEAVE_CONNECT(&gyro_source, &framing_sink);
    WEAVE_CONNECT(&mag_source, &framing_sink);

    /* Sensor threads send with their IDs */
    void accel_thread(void) {
        struct net_buf *buf = weave_packet_alloc_with_id(&sensor_pool, SENSOR_ACCEL, K_NO_WAIT);
        /* ... fill data ... */
        weave_packet_send(&accel_source, buf, K_NO_WAIT);
    }


Performance Considerations
**************************

Packet is designed for high throughput with minimal overhead. Understanding how to
size pools and queues helps you achieve optimal performance.

Buffer Pool Sizing
==================

Pools must be large enough for worst-case concurrent usage. If the pool is exhausted,
allocation fails and data is lost. Calculate based on:

.. code-block:: c

    /* Worst case: 1 (in-flight) + N (immediate sinks) + M (queued events) */
    /* pool_size >= max_concurrent_sends * (1 + immediate_sinks + max_queue_depth) */

    /* Example: 2 concurrent sends, 1 immediate sink, queue depth 16 */
    /* pool_size >= 2 * (1 + 1 + 16) = 36 */
    WEAVE_PACKET_POOL_DEFINE(pool, 40, 128, NULL);

Queue Sizing
============

Queues buffer packets between producer and consumer threads. If the queue fills up,
new packets are dropped. Size based on expected burst traffic:

.. code-block:: c

    /* depth >= peak_arrival_rate * processing_time */
    /* Example: 1000 pkt/s arrival, 50ms processing = 50 events */
    WEAVE_MSGQ_DEFINE(queue, 64);

ISR Usage
=========

A common embedded pattern is capturing data from hardware interrupts. Weave Packet
is designed to work safely from ISR context, allowing you to allocate buffers, fill
them with hardware data, and send them to processing threads - all without blocking.

The key is using ``K_NO_WAIT`` for all operations and queued sinks for processing.
The ISR quickly captures data and queues it; a thread handles the actual processing:

.. code-block:: c

    WEAVE_PACKET_POOL_DEFINE(isr_pool, 32, 64, NULL);
    WEAVE_PACKET_SOURCE_DEFINE(isr_source);

    void my_isr(void *arg) {
        struct net_buf *buf = weave_packet_alloc(&isr_pool, K_NO_WAIT);
        if (!buf) return;  /* Pool exhausted */

        /* Fill buffer */
        uint8_t *data = net_buf_add(buf, 64);
        read_hardware_fifo(data, 64);

        /* Send - consumes buffer */
        weave_packet_send(&isr_source, buf, K_NO_WAIT);
    }

**Guidelines for ISR usage:**

* Always use ``K_NO_WAIT`` for allocation and send operations - ISRs cannot block
* Use queued sinks to defer processing to thread context
* Keep immediate handlers extremely short if the source is used from ISR

Thread Safety
*************

Weave Packet is designed for safe concurrent use from multiple threads and ISRs.
Understanding the synchronization model helps you design correct systems.

**Source operations:**

* Multiple threads can send through different sources concurrently
* Sending through the same source from multiple threads is safe (serialized via spinlock)
* Spinlock critical sections are very short (microseconds) to minimize latency
* Source operations are IRQ-safe, so sending from ISRs works correctly

**Buffer operations:**

* ``net_buf`` reference counting is atomic - multiple threads can ref/unref safely
* Buffer pools use Zephyr's built-in thread-safe allocation
* The same buffer can be accessed by multiple handlers concurrently (read-only)

**Handler execution:**

Where handlers run depends on the sink's execution mode:

* **Immediate handlers** run in the sender's context. If multiple threads send
  to the same source, their immediate handlers may run concurrently. Keep
  handlers short and thread-safe.
* **Queued handlers** run in the processing thread's context. Multiple sinks
  sharing a queue are processed sequentially, providing natural serialization.

**Best practice:** Use queued sinks when handlers access shared state. The
processing thread provides natural serialization, making handlers simpler
and safer. Use immediate sinks only for very fast, stateless operations.

Configuration
*************

Enable Weave Packet in ``prj.conf``:

.. code-block:: kconfig

    CONFIG_WEAVE=y
    CONFIG_WEAVE_PACKET=y

``CONFIG_NET_BUF`` is automatically selected as a dependency.

**Optional settings:**

* ``CONFIG_WEAVE_PACKET_TIMESTAMP_HIRES``: Use CPU cycles instead of kernel ticks
  for timestamps. This provides higher resolution timing (sub-microsecond on fast
  MCUs) at the cost of platform-specific cycle counter access. Useful for precise
  latency measurements and timing analysis.

----

*This documentation was generated with AI assistance and reviewed by a human.*
