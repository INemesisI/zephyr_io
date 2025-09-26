.. _register_mapper:

Register Mapper System
######################

The :dfn:`Register Mapper System` is a compile-time register mapping framework for Zephyr RTOS
that bridges external register-based protocols (UART, Modbus, SPI, I2C) with internal ZBUS channels,
enabling legacy interfaces to interact seamlessly with modern event-driven architectures.

.. contents::
    :local:
    :depth: 2

Concepts
********

The Register Mapper system provides a type-safe mechanism for mapping register addresses to
specific fields within ZBUS channel message structures. External protocols can read and write
registers that automatically synchronize with ZBUS channels, triggering notifications to
observers when values change.

The mapping is defined at compile-time using iterable sections, ensuring zero runtime overhead
for lookups while maintaining type safety through BUILD_ASSERT validation. The system directly
accesses ZBUS message buffers, avoiding data copies and providing efficient register operations.

.. figure:: images/register_mapper_overview.svg
    :alt: Register Mapper usage overview
    :width: 75%

    A typical Register Mapper application architecture.

The system comprises:

* **Register Mappings**: Compile-time definitions linking addresses to channel fields
* **ZBUS Channels**: Message structures containing the actual data
* **External Interfaces**: Protocol handlers for UART, Modbus, SPI, etc.
* **Internal Modules**: Event-driven components observing channel changes
* **Block Writes**: Atomic multi-register update transactions

.. figure:: images/register_mapper_anatomy.svg
    :alt: Register Mapper anatomy
    :width: 70%

    Register Mapper system anatomy.

Key Design Principles
=====================

Zero-Copy Architecture
----------------------

The Register Mapper achieves zero-copy operation by directly accessing ZBUS channel message buffers:

1. Register read operations claim the channel and read directly from the message buffer
2. Register write operations modify the buffer in-place and trigger notifications
3. No intermediate buffers or data copies are required
4. Type information is preserved through tagged unions

This eliminates memory copies entirely, making it ideal for resource-constrained systems
and high-frequency register access patterns.

Type-Safe Mapping
-----------------

All register mappings are validated at compile time:

* **Field existence**: BUILD_ASSERT verifies the field exists in the message structure
* **Size compatibility**: Ensures register type size matches field size exactly
* **Address validation**: Optional runtime checking for overlapping addresses
* **Type preservation**: Register values carry type information for safe access

Static Configuration
--------------------

Register mappings are stored in ROM using iterable sections:

* **Memory efficient**: No RAM required for mapping tables
* **Fast iteration**: Direct section traversal without hash tables
* **Compile-time wiring**: All mappings known at build time
* **Predictable behavior**: System topology fixed at deployment

Event-Driven Integration
------------------------

ZBUS integration provides automatic event distribution:

* **Automatic notifications**: Register writes trigger channel observers
* **Deferred updates**: Block writes batch notifications for atomic updates
* **Decoupled modules**: Components communicate via channels, not direct calls
* **Thread-safe**: ZBUS handles all synchronization

Register Access Process
=======================

When reading a register, the following sequence occurs:

1. **Address lookup**: Find mapping by iterating through section
2. **Permission check**: Verify register is readable
3. **Channel claim**: Lock channel for exclusive access
4. **Direct read**: Copy data from message buffer at field offset
5. **Channel release**: Unlock channel for other operations
6. **Type tagging**: Return value with type information

When writing a register:

1. **Address lookup**: Find mapping by iterating through section
2. **Permission check**: Verify register is writable
3. **Type validation**: Ensure value type matches register type
4. **Channel claim**: Lock channel for exclusive access
5. **In-place update**: Modify message buffer directly
6. **Channel release**: Unlock channel
7. **Notification**: Trigger ZBUS observers (immediate or deferred)

.. note::
   Block writes defer notifications until commit, allowing atomic updates
   of multiple related registers with a single observer notification.

Usage
*****

Basic API
=========

The Register Mapper system provides macros for compile-time mapping and runtime access:

.. code-block:: c

    /* Define ZBUS channel with message structure */
    struct sensor_config {
        uint16_t sample_rate;
        uint16_t filter_cutoff;
        uint8_t  gain;
        uint8_t  mode;
    } __packed;

    ZBUS_CHAN_DEFINE(sensor_config_chan, struct sensor_config,
                     NULL, NULL, ZBUS_OBSERVERS_EMPTY,
                     ZBUS_MSG_INIT(.sample_rate = 100,
                                  .filter_cutoff = 50,
                                  .gain = 1,
                                  .mode = 0));

    /* Map registers to channel fields with compile-time validation */
    REG_MAPPING_DEFINE(reg_sample_rate, 0x1000, &sensor_config_chan,
                       struct sensor_config, sample_rate,
                       REG_TYPE_U16, REG_FLAGS_RW);

    REG_MAPPING_DEFINE(reg_filter_cutoff, 0x1002, &sensor_config_chan,
                       struct sensor_config, filter_cutoff,
                       REG_TYPE_U16, REG_FLAGS_RW);

    /* Read register at runtime */
    struct reg_value value;
    reg_read_value(0x1000, &value);

    /* Write register with notification */
    value.type = REG_TYPE_U16;
    value.val.u16 = 200;
    reg_write_value(0x1000, value, K_MSEC(100));

Defining ZBUS Channels
======================

Channels represent the internal data structures accessed via registers:

.. code-block:: c

    #include <zephyr/zbus/zbus.h>
    #include <zephyr_io/register_mapper/register_mapper.h>

    /* Configuration structure */
    struct motor_config {
        uint16_t target_speed;     /* RPM */
        uint16_t acceleration;     /* RPM/s */
        uint8_t  direction;        /* 0=CW, 1=CCW */
        uint8_t  enable;           /* 0=Off, 1=On */
    } __packed;

    /* Status structure */
    struct motor_status {
        uint16_t current_speed;    /* Actual RPM */
        uint16_t current_draw;     /* mA */
        uint8_t  temperature;      /* Celsius */
        uint8_t  fault_flags;      /* Error bits */
    } __packed;

    /* Define channels */
    ZBUS_CHAN_DEFINE(motor_config_chan, struct motor_config,
                     NULL, NULL, ZBUS_OBSERVERS_EMPTY,
                     ZBUS_MSG_INIT(.target_speed = 0,
                                  .acceleration = 100,
                                  .direction = 0,
                                  .enable = 0));

    ZBUS_CHAN_DEFINE(motor_status_chan, struct motor_status,
                     NULL, NULL, ZBUS_OBSERVERS_EMPTY,
                     ZBUS_MSG_INIT(0));

    /* For block writes, attach state tracking */
    static struct channel_state config_state = {0};
    static struct channel_state status_state = {0};

    static int attach_channel_state(void)
    {
        motor_config_chan.user_data = &config_state;
        motor_status_chan.user_data = &status_state;
        return 0;
    }
    SYS_INIT(attach_channel_state, APPLICATION, 90);

Creating Register Mappings
==========================

Register mappings link addresses to channel fields with type safety:

.. important::
   The :c:macro:`REG_MAPPING_DEFINE` macro performs compile-time validation:

   * Verifies the field exists in the message structure
   * Ensures register type size matches field size exactly
   * Creates mapping in ROM via iterable section

**Basic Mapping**:

.. code-block:: c

    #include <zephyr_io/register_mapper/register_mapper.h>

    /* Map register address to channel field */
    REG_MAPPING_DEFINE(motor_speed_reg,      /* Mapping name */
                       0x1000,                /* Register address */
                       &motor_config_chan,    /* ZBUS channel */
                       struct motor_config,   /* Message type */
                       target_speed,          /* Field name */
                       REG_TYPE_U16,          /* Register type */
                       REG_FLAGS_RW);         /* Read/Write */

**Permission Flags**:

.. code-block:: c

    /* Read-only status register */
    REG_MAPPING_DEFINE(temp_reg, 0x2000, &motor_status_chan,
                       struct motor_status, temperature,
                       REG_TYPE_U8, REG_FLAGS_RO);

    /* Write-only command register */
    REG_MAPPING_DEFINE(cmd_reg, 0x3000, &motor_cmd_chan,
                       struct motor_cmd, command,
                       REG_TYPE_U32, REG_FLAGS_WO);

    /* Read-write configuration */
    REG_MAPPING_DEFINE(config_reg, 0x1000, &config_chan,
                       struct config, parameter,
                       REG_TYPE_U16, REG_FLAGS_RW);

**Conditional Mapping**:

.. code-block:: c

    /* Only create mapping if feature enabled */
    REG_MAPPING_DEFINE_COND(CONFIG_ADVANCED_FEATURES,
                            0x4000, &advanced_chan,
                            struct advanced_config, special_param,
                            REG_TYPE_U32, REG_FLAGS_RW);

Register Access Operations
==========================

**Reading Registers**:

.. code-block:: c

    /* Generic read with type tag */
    struct reg_value value;
    int ret = reg_read_value(0x1000, &value);
    if (ret == 0) {
        switch (value.type) {
        case REG_TYPE_U16:
            LOG_INF("Value: %u", value.val.u16);
            break;
        case REG_TYPE_U32:
            LOG_INF("Value: %u", value.val.u32);
            break;
        /* Handle other types */
        }
    }

    /* Type-specific helper macros */
    uint16_t speed;
    REG_READ_U16(0x1000, &speed);

    uint8_t temp;
    REG_READ_U8(0x2000, &temp);

**Writing Registers**:

.. code-block:: c

    /* Generic write with type tag */
    struct reg_value value;
    value.type = REG_TYPE_U16;
    value.val.u16 = 1500;
    int ret = reg_write_value(0x1000, value, K_MSEC(100));

    /* Type-specific helper macros */
    REG_WRITE_U16(0x1000, 1500);
    REG_WRITE_U8(0x1004, 128);
    REG_WRITE_U32(0x2000, 0x12345678);

Block Write Transactions
========================

Block writes enable atomic updates of multiple registers with deferred notification:

.. code-block:: c

    /* Begin transaction - acquires mutex */
    int ret = reg_block_write_begin(K_MSEC(100));
    if (ret != 0) {
        return ret;  /* Timeout or error */
    }

    /* Update multiple registers without notifications */
    REG_WRITE_U16(0x1000, 3000);     /* Target speed */
    REG_WRITE_U16(0x1002, 500);      /* Acceleration */
    REG_WRITE_U8(0x1004, 1);         /* Direction */
    REG_WRITE_U8(0x1005, 1);         /* Enable */

    /* Commit - releases mutex and sends notifications */
    ret = reg_block_write_commit(K_MSEC(100));

    /* Observers receive single notification with all changes */

.. note::
   Block writes are essential for maintaining consistency when multiple
   related registers must change together (e.g., PID controller parameters).

Advanced Usage
==============

Register Discovery
------------------

Iterate through all defined registers for discovery or documentation:

.. code-block:: c

    /* Callback for each register */
    int discover_callback(const struct reg_mapping *map, void *user_data)
    {
        LOG_INF("Register 0x%04x:", map->address);
        LOG_INF("  Type: %s", reg_type_name(map->type));
        LOG_INF("  Access: %s%s",
                map->flags.readable ? "R" : "",
                map->flags.writable ? "W" : "");
        #ifdef CONFIG_REGISTER_MAPPER_NAMES
        LOG_INF("  Name: %s", map->name);
        #endif
        return 0;  /* Continue iteration */
    }

    /* Discover all registers */
    int count = reg_foreach(discover_callback, NULL);
    LOG_INF("Total registers: %d", count);

    /* Find specific register */
    struct reg_mapping *map = find_register(0x1000);
    if (map) {
        LOG_INF("Found register at 0x%04x", map->address);
    }

ZBUS Observer Integration
-------------------------

Modules react to register changes via ZBUS observers:

.. code-block:: c

    /* Configuration change handler */
    static void motor_config_handler(const struct zbus_channel *chan)
    {
        struct motor_config config;
        zbus_chan_read(chan, &config, K_FOREVER);

        LOG_INF("Motor config changed:");
        LOG_INF("  Speed: %u RPM", config.target_speed);
        LOG_INF("  Accel: %u RPM/s", config.acceleration);

        /* Apply new configuration */
        motor_set_parameters(&config);
    }

    /* Register as ZBUS listener */
    ZBUS_LISTENER_DEFINE(motor_config_listener, motor_config_handler);
    ZBUS_CHAN_ADD_OBS(motor_config_chan, motor_config_listener, 1);

Protocol Handlers
-----------------

Example UART command processor for register access:

.. code-block:: c

    /* UART register protocol handler */
    void uart_process_command(uint8_t cmd, uint16_t addr, uint8_t *data, size_t len)
    {
        struct reg_value value;
        int ret;

        switch (cmd) {
        case CMD_READ_REGISTER:
            ret = reg_read_value(addr, &value);
            if (ret == 0) {
                uart_send_response(addr, &value);
            } else {
                uart_send_error(ret);
            }
            break;

        case CMD_WRITE_REGISTER:
            /* Parse value based on length */
            value.type = len_to_type(len);
            memcpy(&value.val, data, len);

            ret = reg_write_value(addr, value, K_MSEC(100));
            if (ret == 0) {
                uart_send_ack();
            } else {
                uart_send_error(ret);
            }
            break;

        case CMD_BLOCK_BEGIN:
            reg_block_write_begin(K_MSEC(100));
            break;

        case CMD_BLOCK_COMMIT:
            reg_block_write_commit(K_MSEC(100));
            break;
        }
    }

Design Patterns
***************

Modbus Integration
==================

A common pattern for Modbus RTU/TCP register mapping:

.. code-block:: c

    /* Modbus holding registers (40001-49999) */
    REG_MAPPING_DEFINE(mb_hr_40001, 40001, &config_chan,
                       struct config, param1, REG_TYPE_U16, REG_FLAGS_RW);
    REG_MAPPING_DEFINE(mb_hr_40002, 40002, &config_chan,
                       struct config, param2, REG_TYPE_U16, REG_FLAGS_RW);

    /* Modbus input registers (30001-39999) */
    REG_MAPPING_DEFINE(mb_ir_30001, 30001, &status_chan,
                       struct status, value1, REG_TYPE_U16, REG_FLAGS_RO);
    REG_MAPPING_DEFINE(mb_ir_30002, 30002, &status_chan,
                       struct status, value2, REG_TYPE_U16, REG_FLAGS_RO);

    /* Modbus handler */
    void modbus_read_holding_registers(uint16_t addr, uint16_t count, uint16_t *buf)
    {
        for (int i = 0; i < count; i++) {
            struct reg_value value;
            if (reg_read_value(addr + i, &value) == 0) {
                buf[i] = value.val.u16;  /* Modbus is 16-bit oriented */
            } else {
                buf[i] = 0xFFFF;  /* Invalid register */
            }
        }
    }

    void modbus_write_holding_registers(uint16_t addr, uint16_t count, uint16_t *buf)
    {
        /* Use block write for atomic update */
        reg_block_write_begin(K_MSEC(100));

        for (int i = 0; i < count; i++) {
            struct reg_value value = {
                .type = REG_TYPE_U16,
                .val.u16 = buf[i]
            };
            reg_block_write_register(addr + i, value);
        }

        reg_block_write_commit(K_MSEC(100));
    }

Device Configuration Pattern
============================

Using registers for device configuration with validation:

.. code-block:: c

    /* Configuration limits */
    #define SPEED_MIN 100
    #define SPEED_MAX 5000
    #define ACCEL_MIN 10
    #define ACCEL_MAX 1000

    /* Validated configuration handler */
    static void config_change_handler(const struct zbus_channel *chan)
    {
        struct motor_config config;
        zbus_chan_read(chan, &config, K_FOREVER);

        /* Validate parameters */
        if (config.target_speed < SPEED_MIN || config.target_speed > SPEED_MAX) {
            LOG_WRN("Invalid speed: %u", config.target_speed);
            config.target_speed = CLAMP(config.target_speed, SPEED_MIN, SPEED_MAX);

            /* Write back corrected value */
            zbus_chan_pub(chan, &config, K_NO_WAIT);
            return;
        }

        if (config.acceleration < ACCEL_MIN || config.acceleration > ACCEL_MAX) {
            LOG_WRN("Invalid acceleration: %u", config.acceleration);
            config.acceleration = CLAMP(config.acceleration, ACCEL_MIN, ACCEL_MAX);

            /* Write back corrected value */
            zbus_chan_pub(chan, &config, K_NO_WAIT);
            return;
        }

        /* Apply validated configuration */
        apply_motor_config(&config);
    }

Status Monitoring Pattern
========================

Periodic status updates accessible via registers:

.. code-block:: c

    /* Status update timer */
    static void status_update_timer(struct k_timer *timer)
    {
        struct sensor_status status = {
            .temperature = read_temperature(),
            .voltage = read_voltage(),
            .current = read_current(),
            .error_flags = get_error_flags()
        };

        /* Publish to ZBUS - automatically updates registers */
        zbus_chan_pub(&sensor_status_chan, &status, K_NO_WAIT);
    }

    K_TIMER_DEFINE(status_timer, status_update_timer, NULL);

    /* Initialize status monitoring */
    static int status_monitor_init(void)
    {
        /* Update status at 10Hz */
        k_timer_start(&status_timer, K_MSEC(100), K_MSEC(100));
        return 0;
    }
    SYS_INIT(status_monitor_init, APPLICATION, 95);

    /* Status registers automatically reflect current values */
    REG_MAPPING_DEFINE(reg_temp, 0x2000, &sensor_status_chan,
                       struct sensor_status, temperature, REG_TYPE_U16, REG_FLAGS_RO);
    REG_MAPPING_DEFINE(reg_voltage, 0x2002, &sensor_status_chan,
                       struct sensor_status, voltage, REG_TYPE_U16, REG_FLAGS_RO);

Performance Considerations
**************************

Memory Usage
============

Register mappings are stored efficiently in ROM:

.. code-block:: c

    /* Memory per mapping (typical) */
    struct reg_mapping {
        uint16_t address;                      /* 2 bytes */
        const struct zbus_channel *channel;    /* 4-8 bytes (pointer) */
        uint16_t offset;                       /* 2 bytes */
        enum reg_type type;                   /* 1 byte */
        struct reg_flags flags;                /* 1 byte */
        const char *name;                      /* 4-8 bytes if enabled */
    };  /* Total: 14-20 bytes per mapping in ROM */

    /* Channel state (RAM) */
    struct channel_state {
        bool update_pending;                   /* 1 byte per channel */
    };

Lookup Performance
==================

Register lookup is O(n) linear search through iterable section:

.. code-block:: c

    /* For small register sets (<100), linear search is efficient */
    /* Consider sorting if >100 registers for binary search */

    /* Example: Binary search wrapper (if needed) */
    static int compare_address(const void *a, const void *b)
    {
        const struct reg_mapping *map_a = a;
        const struct reg_mapping *map_b = b;
        return (int)map_a->address - (int)map_b->address;
    }

    const struct reg_mapping *find_register_binary(uint16_t addr)
    {
        /* Build sorted array on first call (cached) */
        static struct reg_mapping *sorted_maps = NULL;
        static size_t map_count = 0;

        if (!sorted_maps) {
            /* Count and sort mappings once */
            /* ... implementation ... */
        }

        /* Binary search */
        struct reg_mapping key = { .address = addr };
        return bsearch(&key, sorted_maps, map_count,
                      sizeof(struct reg_mapping), compare_address);
    }

ISR Usage
=========

The Register Mapper can be used from ISRs with restrictions:

.. code-block:: c

    /* ISR-safe register read (if channel not locked) */
    void my_isr(void *arg)
    {
        struct reg_value value;

        /* Try non-blocking read */
        int ret = reg_read_value(0x2000, &value);  /* Uses K_NO_WAIT internally */
        if (ret == 0) {
            /* Process value */
            if (value.val.u16 > THRESHOLD) {
                trigger_alarm();
            }
        }
    }

    /* ISR-triggered register write (deferred) */
    K_MSGQ_DEFINE(isr_write_queue, sizeof(struct reg_write_req), 32, 4);

    struct reg_write_req {
        uint16_t addr;
        struct reg_value value;
    };

    void my_isr_deferred(void *arg)
    {
        struct reg_write_req req = {
            .addr = 0x3000,
            .value = { .type = REG_TYPE_U32, .val.u32 = get_timestamp() }
        };

        /* Queue write request for thread context */
        k_msgq_put(&isr_write_queue, &req, K_NO_WAIT);
    }

.. note::
   Register operations use ZBUS channel claim/finish which may block.
   For ISR usage, ensure channels are not held for extended periods
   and consider deferring writes to thread context.

Comparison with Direct Access
******************************

The Register Mapper provides advantages over direct register arrays:

.. list-table:: Register Mapper vs Direct Arrays
   :header-rows: 1
   :widths: 30 35 35

   * - Aspect
     - Register Mapper
     - Direct Arrays
   * - **Type Safety**
     - Compile-time validation
     - Runtime checks only
   * - **Memory Model**
     - Mappings in ROM, data in ZBUS
     - All in RAM
   * - **Event Support**
     - Automatic ZBUS notifications
     - Manual callbacks needed
   * - **Access Control**
     - Per-register R/W permissions
     - Global or complex checks
   * - **Atomic Updates**
     - Block write transactions
     - Manual locking required
   * - **Protocol Support**
     - Protocol-agnostic design
     - Protocol-specific code
   * - **Best For**
     - Event-driven systems
     - Simple register banks

Choose Register Mapper when:

* Integrating with ZBUS architecture
* Multiple protocols access same data
* Type safety is important
* Event notifications needed
* Atomic updates required

Choose direct arrays when:

* Extremely simple register set
* No event handling needed
* Memory constraints severe
* Custom protocol requirements

Configuration Options
*********************

To enable the Register Mapper system, set :kconfig:option:`CONFIG_REGISTER_MAPPER`.

Related configuration options:

* :kconfig:option:`CONFIG_REGISTER_MAPPER` - Enable the Register Mapper subsystem
* :kconfig:option:`CONFIG_REGISTER_MAPPER_BLOCK_WRITE` - Enable block write transactions
* :kconfig:option:`CONFIG_REGISTER_MAPPER_VALIDATION` - Enable address overlap validation
* :kconfig:option:`CONFIG_REGISTER_MAPPER_NAMES` - Include register names for debugging
* :kconfig:option:`CONFIG_REGISTER_MAPPER_LOG_LEVEL` - Set logging level (0-4)
* :kconfig:option:`CONFIG_REGISTER_MAPPER_INIT_PRIORITY` - System initialization priority (default 90)

Required dependencies:

* :kconfig:option:`CONFIG_ZBUS` - ZBUS message bus support (required)

Example configuration:

.. code-block:: kconfig

    # Enable Register Mapper with all features
    CONFIG_REGISTER_MAPPER=y
    CONFIG_REGISTER_MAPPER_BLOCK_WRITE=y
    CONFIG_REGISTER_MAPPER_VALIDATION=y
    CONFIG_REGISTER_MAPPER_LOG_LEVEL=3

    # Required dependencies
    CONFIG_ZBUS=y

    # Recommended for debugging
    CONFIG_LOG=y
    CONFIG_ASSERT=y
    CONFIG_REGISTER_MAPPER_NAMES=y  # Debug names

Samples
*******

The following samples demonstrate Register Mapper usage:

* **Device Configuration** (:file:`register_mapper/samples/device_config`) - Shows a complete
  device configuration system with sensor and motor modules accessed via UART registers

Testing
*******

The Register Mapper system includes comprehensive test coverage:

* **Unit Tests** (:file:`register_mapper/tests/subsys/register_mapper/unit_test`) - API validation,
  type checking, permission enforcement, and block write verification

* **Integration Tests** (:file:`register_mapper/tests/subsys/register_mapper/integration`) - Multi-module
  scenarios, protocol handlers, and performance validation

API Reference
*************

.. doxygengroup:: register_mapper_apis