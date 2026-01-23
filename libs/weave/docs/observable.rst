.. _weave_observable:

Weave Observable
################

Weave Observable provides stateful publish/subscribe. Observables hold values and
automatically notify connected observers when the value changes. It's a lightweight
alternative to ZBUS channels for simple state distribution.

.. contents::
    :local:
    :depth: 2

Introduction
************

Why Observable?
===============

Many embedded systems have state that multiple components need to track. Consider
device configuration: the sensor module needs the sample rate, the display needs
the units setting, and the network module needs the reporting interval. When
configuration changes, all of them need to know.

Traditional approaches have problems:

* **Polling** wastes CPU cycles checking for changes that rarely happen
* **Direct callbacks** create tight coupling - the configuration module must know
  about every consumer
* **Global variables** require careful synchronization and make it hard to know
  when values change

Observable solves this by combining state storage with change notifications. The
configuration module just calls ``SET`` when values change, and all interested
parties are notified automatically.

When to Use Observable
======================

Observable is ideal for:

* **Configuration** - Settings that multiple modules need to track
* **Status** - Device state, connection status, error conditions
* **Shared state** - Values that need synchronized access across threads
* **Event-driven updates** - When you want to react to changes, not poll for them

For streaming data (sensor readings, network packets), consider :doc:`packet` instead.
For request/response operations, consider :doc:`method`.

Owner Handler vs External Observers
===================================

Observable supports two kinds of notification handlers:

**Owner handler** - A single handler defined with the observable itself. It is
notified before external observers. Use it when the module that owns the observable
needs to react to changes.

* With ``WV_IMMEDIATE``: The handler runs synchronously before ``SET`` returns.
  This guarantees the owner has processed the change before the caller continues.
* With a queue: The handler is queued before external observers, but runs later
  when the processing thread gets to it. No timing guarantees - ``SET`` returns
  before the handler executes.

**External observers** - Any number of observers can connect to an observable from
other modules. They are notified after the owner handler. Use them when other parts
of the system need to know about changes.

This separation lets you:

* Keep the owner's reaction logic close to the observable definition
* Allow external modules to subscribe without modifying the owner
* Control notification order (owner is always notified first)

Concepts
********

State + Notifications
=====================

An observable combines several concepts:

1. **State**: A typed value stored in the observable
2. **Validation**: Optional validator can reject changes before they're applied
3. **Owner Handler**: Optional handler called before external observers
4. **External Observers**: Automatic broadcast to connected observers

.. code-block:: text

    WEAVE_OBSERVABLE_SET(config, &new_value)
           │
           ├──→ [validate (if validator defined)]
           │         └──→ reject? return error, no change
           │
           ├──→ [copy value to observable]
           │
           ├──→ [call owner handler (if defined)]
           │
           └──→ [notify external observers]
                     │
                     ├──→ observer1 handler
                     ├──→ observer2 handler
                     └──→ observer3 handler

Zero Allocation
===============

Observables use only static memory, making them predictable and real-time friendly:

* **Value storage** is statically allocated alongside the observable - no malloc
* **Observer list** uses compile-time wiring (iterable sections) - no dynamic registration
* **Notifications** pass pointers to the observable, not copies of the value

This means you can use observables in memory-constrained systems and safety-critical
code without worrying about allocation failures or heap fragmentation.

Type Safety
===========

Macros provide compile-time type checking to catch bugs early:

* ``WEAVE_OBSERVABLE_SET`` verifies your pointer type matches the observable's type
* ``WEAVE_OBSERVABLE_GET`` verifies your pointer type matches the observable's type
* Type mismatches cause compiler errors, not runtime bugs

This is especially valuable when observables are shared across modules - the compiler
ensures everyone agrees on the data type.

Usage
*****

Defining Observables
====================

An observable is defined with ``WEAVE_OBSERVABLE_DEFINE``, which takes six parameters:

1. **Name** - The observable's identifier (used with GET/SET macros)
2. **Type** - The value type (usually a struct)
3. **Owner handler** - Called on every change, or ``WV_NO_HANDLER`` for none
4. **Execution mode** - ``WV_IMMEDIATE`` or a queue pointer for the owner handler
5. **User data** - Context pointer passed to owner handler and validator
6. **Validator** - Can reject invalid values, or ``WV_NO_VALID`` for none

Here are the two common patterns:

**Status pattern** - The module broadcasts state but doesn't react to changes itself:

.. code-block:: c

    /* Module broadcasts its status for others to observe */
    struct device_status {
        bool connected;
        int error_code;
        uint32_t uptime_ms;
    };

    WEAVE_OBSERVABLE_DEFINE(device_status, struct device_status,
                            WV_NO_HANDLER, WV_IMMEDIATE, NULL,
                            WV_NO_VALID);

**Settings pattern** - The module owns configuration, validates input, and reacts to changes:

.. code-block:: c

    struct sensor_settings {
        uint32_t sample_rate_ms;
        uint16_t threshold;
    };

    /* Validator rejects invalid values */
    int validate_settings(struct weave_observable *obs, const void *new_value,
                          void *user_data) {
        const struct sensor_settings *cfg = new_value;
        if (cfg->sample_rate_ms < 10 || cfg->sample_rate_ms > 10000) {
            return -EINVAL;
        }
        return 0;
    }

    /* Owner handler applies validated settings */
    void on_settings_change(struct weave_observable *obs, void *user_data) {
        struct sensor_settings cfg;
        WEAVE_OBSERVABLE_GET(sensor_settings, &cfg);
        apply_sensor_config(&cfg);
    }

    WEAVE_OBSERVABLE_DEFINE(sensor_settings, struct sensor_settings,
                            on_settings_change, WV_IMMEDIATE, NULL,
                            validate_settings);

Defining Observers
==================

External observers subscribe to an observable and are notified whenever the value
changes. Each observer has:

* **Handler function** - Called when the observable changes
* **Execution mode** - ``WV_IMMEDIATE`` (runs in setter's thread) or a queue pointer
* **User data** - Optional context passed to the handler

The handler receives a pointer to the observable, which you use to read the current
value:

.. code-block:: c

    void on_settings_change(struct weave_observable *obs, void *user_data) {
        struct sensor_settings cfg;
        WEAVE_OBSERVABLE_GET(settings, &cfg);

        LOG_INF("Settings changed: rate=%u, threshold=%u",
                cfg.sample_rate_ms, cfg.threshold);

        apply_settings(&cfg);
    }

    /* Immediate execution - runs in setter's context */
    WEAVE_OBSERVER_DEFINE(settings_observer, on_settings_change, WV_IMMEDIATE, NULL);

    /* Queued execution - runs in processing thread */
    WEAVE_MSGQ_DEFINE(observer_queue, 8);
    WEAVE_OBSERVER_DEFINE(deferred_observer, on_settings_change, &observer_queue, NULL);

Connecting Observers
====================

Observers are connected to observables at compile time using ``WEAVE_OBSERVER_CONNECT``.
This creates a static link - no runtime registration, no memory allocation, and the
connection is visible in your code:

.. code-block:: c

    WEAVE_OBSERVER_CONNECT(settings, settings_observer);
    WEAVE_OBSERVER_CONNECT(settings, deferred_observer);

Setting Values
==============

``WEAVE_OBSERVABLE_SET`` updates the value and notifies all handlers. The sequence is:

1. **Validate** - If a validator is defined, it runs first and can reject the change
2. **Copy** - The new value is copied into the observable (under spinlock)
3. **Owner handler** - If defined, runs before external observers
4. **External observers** - All connected observers are notified

The return value indicates success or failure:

* ``0`` or positive: Success, value was updated
* Negative errno: Validation failed, value unchanged (e.g., ``-EINVAL``)

Example:

.. code-block:: c

    struct sensor_settings new_settings = {
        .sample_rate_ms = 100,
        .threshold = 50,
        .enabled = true,
    };

    /* Type-safe set - notifies all observers */
    WEAVE_OBSERVABLE_SET(settings, &new_settings);

Getting Values
==============

``WEAVE_OBSERVABLE_GET`` reads the current value at any time, from any thread. The
read is atomic - you always get a consistent snapshot, even if another thread is
updating the value concurrently:

.. code-block:: c

    struct sensor_settings current;
    WEAVE_OBSERVABLE_GET(settings, &current);

    LOG_INF("Current rate: %u ms", current.sample_rate_ms);

In handlers, prefer using ``WEAVE_OBSERVABLE_GET`` with the name for type safety.
The ``weave_observable_get_unchecked()`` function exists for generic code that
works with arbitrary observable pointers.

Processing Queued Observers
===========================

Queued observers (those using a queue instead of ``WV_IMMEDIATE``) don't run immediately
when the observable changes. Instead, a notification is placed in the queue, and a
processing thread must drain it. This is useful when:

* The observer handler takes significant time (don't block the setter)
* The observer needs to run in a specific thread context
* You want to batch notifications and process them together

The processing thread is simple:

.. code-block:: c

    void observer_thread(void) {
        while (1) {
            weave_process_messages(&observer_queue, K_FOREVER);
        }
    }

Header Declarations
===================

For modular code, declare observables in headers so other modules can use ``GET``/``SET``
and connect observers. The declaration includes the value type for type checking:

.. code-block:: c

    /* device_status.h */
    #include <weave/observable.h>

    struct device_status {
        bool connected;
        int error_code;
    };

    WEAVE_OBSERVABLE_DECLARE(device_status, struct device_status);

    /* device_status.c */
    #include "device_status.h"

    WEAVE_OBSERVABLE_DEFINE(device_status, struct device_status,
                            WV_NO_HANDLER, WV_IMMEDIATE, NULL,
                            WV_NO_VALID);

This is the status pattern - the module broadcasts its state but doesn't need to
react to changes. External observers subscribe to track the device status.

Design Patterns
***************

Configuration Management
========================

A common pattern is centralizing application configuration in an observable. This gives you:

* **Single source of truth** - One place where configuration lives
* **Validation** - Reject invalid values before they take effect
* **Automatic propagation** - All interested modules are notified on change
* **Easy persistence** - Read/write the observable to implement settings storage

Here's a complete example with validation and multiple observers:

.. code-block:: c

    /* Define configuration type */
    struct app_config {
        uint32_t log_level;
        uint32_t timeout_ms;
        bool debug_mode;
    };

    /* Optional: validate configuration changes */
    int validate_config(struct weave_observable *obs, const void *new_value, void *user_data) {
        const struct app_config *cfg = new_value;
        if (cfg->log_level > 4 || cfg->timeout_ms > 60000) {
            return -EINVAL;
        }
        return 0;
    }

    /* Define observable with validator */
    WEAVE_OBSERVABLE_DEFINE(app_config, struct app_config,
                            WV_NO_HANDLER, WV_IMMEDIATE, NULL,
                            validate_config);

    /* Logger observes config changes */
    void logger_config_handler(struct weave_observable *obs, void *user_data) {
        struct app_config cfg;
        weave_observable_get_unchecked(obs, &cfg);
        set_log_level(cfg.log_level);
    }
    WEAVE_OBSERVER_DEFINE(logger_observer, logger_config_handler, WV_IMMEDIATE, NULL);
    WEAVE_OBSERVER_CONNECT(app_config, logger_observer);

    /* Network observes config changes */
    void network_config_handler(struct weave_observable *obs, void *user_data) {
        struct app_config cfg;
        weave_observable_get_unchecked(obs, &cfg);
        set_timeout(cfg.timeout_ms);
    }
    WEAVE_OBSERVER_DEFINE(network_observer, network_config_handler, WV_IMMEDIATE, NULL);
    WEAVE_OBSERVER_CONNECT(app_config, network_observer);

    /* Update config from shell/serial/etc */
    int cmd_set_timeout(uint32_t ms) {
        struct app_config cfg;
        WEAVE_OBSERVABLE_GET(app_config, &cfg);
        cfg.timeout_ms = ms;
        int ret = WEAVE_OBSERVABLE_SET(app_config, &cfg);
        if (ret < 0) {
            return ret;  /* Validation failed */
        }
        return 0;  /* Both observers notified */
    }

Sensor Status Broadcasting
==========================

When sensor data needs to go to multiple consumers (display, logger, network), an
observable provides clean decoupling. The sensor just updates the observable; it
doesn't need to know who's listening.

Note the mix of immediate and queued observers - the display updates immediately
for responsiveness, while logging is queued to avoid blocking the sensor:

.. code-block:: c

    struct sensor_reading {
        int32_t temperature;
        int32_t humidity;
        uint32_t timestamp;
    };

    WEAVE_OBSERVABLE_DEFINE(sensor_data, struct sensor_reading,
                            WV_NO_HANDLER, WV_IMMEDIATE, NULL,
                            WV_NO_VALID);

    /* Display observer */
    void display_handler(struct weave_observable *obs, void *user_data) {
        struct sensor_reading data;
        weave_observable_get_unchecked(obs, &data);
        update_display(data.temperature, data.humidity);
    }
    WEAVE_OBSERVER_DEFINE(display_observer, display_handler, WV_IMMEDIATE, NULL);
    WEAVE_OBSERVER_CONNECT(sensor_data, display_observer);

    /* Logging observer (queued to avoid blocking sensor) */
    WEAVE_MSGQ_DEFINE(log_queue, 16);
    void log_handler(struct weave_observable *obs, void *user_data) {
        struct sensor_reading data;
        weave_observable_get_unchecked(obs, &data);
        LOG_INF("T=%d H=%d", data.temperature, data.humidity);
    }
    WEAVE_OBSERVER_DEFINE(log_observer, log_handler, &log_queue, NULL);
    WEAVE_OBSERVER_CONNECT(sensor_data, log_observer);

    /* Sensor thread updates periodically */
    void sensor_thread(void) {
        while (1) {
            struct sensor_reading reading = {
                .temperature = read_temp(),
                .humidity = read_humidity(),
                .timestamp = k_uptime_get_32(),
            };
            WEAVE_OBSERVABLE_SET(sensor_data, &reading);
            k_sleep(K_SECONDS(1));
        }
    }

State Machine Status
====================

Observables work well for tracking state machine status. Using ``WV_IMMEDIATE`` for
the owner handler guarantees the LED is updated before ``SET`` returns - the state
and LED are always in sync, regardless of who triggers the transition.

External observers handle secondary concerns (logging, network reporting) and can
be queued if they take significant time:

.. code-block:: c

    enum device_state { STATE_INIT, STATE_IDLE, STATE_RUNNING, STATE_ERROR };

    struct state_info {
        enum device_state state;
        int error_code;
        uint32_t entered_at;
    };

    /* Owner handler for immediate LED update */
    void update_led(struct weave_observable *obs, void *user_data) {
        struct state_info info;
        weave_observable_get_unchecked(obs, &info);

        switch (info.state) {
        case STATE_RUNNING: set_led_green(); break;
        case STATE_ERROR:   set_led_red(); break;
        default:            set_led_off(); break;
        }
    }

    WEAVE_OBSERVABLE_DEFINE(device_state, struct state_info,
                            update_led, WV_IMMEDIATE, NULL,
                            WV_NO_VALID);

    void transition_to(enum device_state new_state, int error) {
        struct state_info info = {
            .state = new_state,
            .error_code = error,
            .entered_at = k_uptime_get_32(),
        };
        WEAVE_OBSERVABLE_SET(device_state, &info);
        /* LED is updated immediately by owner handler */
    }

    /* Additional observers for logging, network reporting, etc. */
    void log_state_change(struct weave_observable *obs, void *user_data) {
        struct state_info info;
        weave_observable_get_unchecked(obs, &info);
        LOG_INF("State: %d, error: %d", info.state, info.error_code);
    }
    WEAVE_OBSERVER_DEFINE(state_logger, log_state_change, WV_IMMEDIATE, NULL);
    WEAVE_OBSERVER_CONNECT(device_state, state_logger);

Multiple Observers with User Data
=================================

When you need multiple observers that share the same handler but behave differently,
use user_data to pass context. This is more memory-efficient than writing separate
handler functions:

.. code-block:: c

    struct channel_context {
        uint8_t channel_id;
        int32_t *output;
    };

    void channel_handler(struct weave_observable *obs, void *user_data) {
        struct channel_context *ctx = user_data;
        struct sensor_reading data;
        weave_observable_get_unchecked(obs, &data);

        /* Each observer processes differently based on context */
        *ctx->output = data.temperature + (ctx->channel_id * 10);
    }

    static struct channel_context ch0_ctx = {.channel_id = 0, .output = &ch0_value};
    static struct channel_context ch1_ctx = {.channel_id = 1, .output = &ch1_value};

    WEAVE_OBSERVER_DEFINE(ch0_observer, channel_handler, WV_IMMEDIATE, &ch0_ctx);
    WEAVE_OBSERVER_DEFINE(ch1_observer, channel_handler, WV_IMMEDIATE, &ch1_ctx);

    WEAVE_OBSERVER_CONNECT(sensor_data, ch0_observer);
    WEAVE_OBSERVER_CONNECT(sensor_data, ch1_observer);

Thread Safety
*************

Weave Observable is designed for safe concurrent access from multiple threads:

**Value access:**

* ``WEAVE_OBSERVABLE_GET`` is atomic - you always get a consistent snapshot
* ``WEAVE_OBSERVABLE_SET`` copies the value atomically under a spinlock
* Multiple threads can call GET/SET concurrently without corruption

**Notification execution:**

* The spinlock is released before handlers run, so handlers can call GET safely
* Immediate handlers run in the caller's thread - if multiple threads call SET
  concurrently, their immediate handlers may run concurrently
* Queued handlers run in the processing thread, naturally serialized

**What you need to handle:**

* If immediate handlers access shared state (via user_data or globals), you must
  synchronize that access yourself
* If you need handlers to run sequentially, use queued execution with a single
  processing thread

**Best practice:** For simple cases, use immediate handlers with no shared state,
or use queued handlers to get natural serialization through the processing thread.

Configuration
*************

Enable observable support in ``prj.conf``:

.. code-block:: kconfig

    CONFIG_WEAVE=y
    CONFIG_WEAVE_OBSERVABLE=y

----

*This documentation was generated with AI assistance and reviewed by a human.*
