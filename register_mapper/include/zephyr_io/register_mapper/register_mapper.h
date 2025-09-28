/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ZBUS-based register mapping subsystem
 *
 * This subsystem provides a bridge between external register-based interfaces
 * and internal ZBUS channels, allowing legacy protocols to interact with
 * modern event-driven architectures.
 */

#ifndef ZEPHYR_INCLUDE_REGISTER_MAPPER_H_
#define ZEPHYR_INCLUDE_REGISTER_MAPPER_H_

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr_io/register_mapper/register_types.h>
#include <zephyr_io/register_mapper/register_channel.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Register flags as bitfield struct (8 bits total) */
struct reg_flags {
	uint8_t readable: 1; /* Register can be read */
	uint8_t writable: 1; /* Register can be written */
	uint8_t reserved: 6; /* Reserved for future use */
};

/* Convenience macros for common flag combinations */
#define REG_FLAGS_RO ((struct reg_flags){.readable = 1, .writable = 0})
#define REG_FLAGS_WO ((struct reg_flags){.readable = 0, .writable = 1})
#define REG_FLAGS_RW ((struct reg_flags){.readable = 1, .writable = 1})

/* Register mapping entry - IMMUTABLE (goes in ROM via iterable section) */
struct reg_mapping {
	uint16_t address;                   /* Register address */
	const struct zbus_channel *channel; /* Associated channel */
	uint16_t offset;                    /* Offset in config struct */
	enum reg_type type;                 /* Data type */
	struct reg_flags flags;             /* Register flags (IMMUTABLE in ROM) */
#ifdef CONFIG_REGISTER_MAPPER_NAMES
	const char *name; /* Optional register name */
#endif
};

/**
 * @brief Encode address in variable name for automatic linker sorting
 *
 * The linker sorts sections by name, enabling O(log n) binary search.
 * LIMITATION: Requires consistent address notation (all hex or all decimal).
 * Recommended: Use hex (0x0000-0xFFFF) throughout your project.
 */
#define REG_MAP_SORTED_NAME(_addr, _name) CONCAT(CONCAT(regmap_, _addr), CONCAT(_, _name))

/**
 * @brief Define a register mapping (unsafe variant)
 *
 * This macro creates a register mapping in the iterable section without
 * compile-time validation. Use REG_MAPPING_DEFINE for the safe version.
 * When CONFIG_REGISTER_MAPPER_NAMES is enabled, the variable name is used
 * as the human-readable register name.
 *
 * The mapping is automatically sorted by address due to the naming convention.
 *
 * @param _name Mapping variable name (also used as register name if names enabled)
 * @param _addr Register address
 * @param _chan Pointer to ZBUS channel
 * @param _off Offset in channel message
 * @param _typ Register type
 * @param _flg Register flags
 */
#ifdef CONFIG_REGISTER_MAPPER_NAMES
#define REG_MAPPING_DEFINE_UNSAFE(_name, _addr, _chan, _off, _typ, _flg)                           \
	static STRUCT_SECTION_ITERABLE(reg_mapping, REG_MAP_SORTED_NAME(_addr, _name)) = {         \
		.address = _addr,                                                                  \
		.channel = _chan,                                                                  \
		.offset = _off,                                                                    \
		.type = _typ,                                                                      \
		.flags = _flg,                                                                     \
		.name = STRINGIFY(_name) }
#else
#define REG_MAPPING_DEFINE_UNSAFE(_name, _addr, _chan, _off, _typ, _flg)                           \
	static STRUCT_SECTION_ITERABLE(reg_mapping, REG_MAP_SORTED_NAME(_addr, _name)) = {         \
		.address = _addr, .channel = _chan, .offset = _off, .type = _typ, .flags = _flg}
#endif

/**
 * @brief Define a register mapping with compile-time validation
 *
 * Creates a register mapping with automatic sorting for O(log n) lookup.
 *
 * IMPORTANT: Use consistent address notation throughout your project:
 * - Recommended: hex (0x0100, 0x1000, 0x2000)
 * - Alternative: decimal (256, 4096, 8192)
 * Mixing formats breaks sorting and binary search!
 *
 * @param _name Mapping variable name
 * @param _addr Register address (16-bit, use consistent notation)
 * @param _chan Pointer to ZBUS channel
 * @param _msg_type Message type of the ZBUS channel
 * @param _field Field name in channel message structure
 * @param _typ Register type (REG_TYPE_*)
 * @param _flg Register flags (REG_FLAGS_*)
 */
#define REG_MAPPING_DEFINE(_name, _addr, _chan, _msg_type, _field, _typ, _flg)                     \
	/* Verify field exists in structure */                                                     \
	BUILD_ASSERT(offsetof(_msg_type, _field) >= 0, "Field '" #_field "' does not exist");      \
	/* Verify register type size matches field size */                                         \
	BUILD_ASSERT(sizeof(((_msg_type *)0)->_field) == REG_TYPE_SIZE_CONST(_typ),                \
		     "Register type size mismatch for field '" #_field "'");                       \
	/* Verify address is in valid range for 16-bit addresses */                                \
	BUILD_ASSERT((_addr) <= 0xFFFF, "Register address exceeds 16-bit range");                  \
	/* Create the mapping */                                                                   \
	REG_MAPPING_DEFINE_UNSAFE(_name, _addr, _chan, offsetof(_msg_type, _field), _typ, _flg)

/**
 * @brief Conditionally define a register mapping
 *
 * This macro creates a register mapping only if the condition is true.
 * Useful for conditional compilation based on Kconfig.
 *
 * @param _cond Condition to evaluate
 * @param _addr Register address
 * @param _chan Pointer to ZBUS channel
 * @param _msg_type Message type of the ZBUS channel
 * @param _field Field name in channel message structure
 * @param _typ Register type
 * @param _flg Register flags
 */
#define REG_MAPPING_DEFINE_COND(_cond, _addr, _chan, _msg_type, _field, _typ, _flg)                \
	COND_CODE_1(_cond,                                                                         \
		    (REG_MAPPING_DEFINE(reg_##_chan##_##_field, _addr, _chan, _msg_type, _field,   \
					_typ, _flg)),                                              \
		    (/* empty if condition is false */))

/**
 * @brief Begin a block write transaction
 *
 * Acquires the block write mutex to ensure atomic updates across
 * multiple register writes.
 *
 * @param timeout Maximum time to wait for mutex
 * @return 0 on success, negative errno on error
 */
int reg_block_write_begin(k_timeout_t timeout);

/**
 * @brief Write a register within a block transaction
 *
 * Writes a register value without triggering notifications.
 * Must be called between reg_block_write_begin() and reg_block_write_commit().
 *
 * @param addr Register address
 * @param value Register value with type tag
 * @return 0 on success, negative errno on error
 */
int reg_block_write_register(uint16_t addr, struct reg_value value);

/**
 * @brief Commit a block write transaction
 *
 * Releases the block write mutex and sends notifications for all
 * channels that were modified during the transaction.
 *
 * @param timeout Maximum time to wait for notifications
 * @return 0 on success, negative errno on error
 */
int reg_block_write_commit(k_timeout_t timeout);

/**
 * @brief Read a register value
 *
 * @param addr Register address
 * @param value Pointer to store the value with type tag
 * @return 0 on success, negative errno on error
 */
int reg_read_value(uint16_t addr, struct reg_value *value);

/**
 * @brief Write a register value with notification
 *
 * @param addr Register address
 * @param value Register value with type tag
 * @param timeout Maximum time to wait for notification
 * @return 0 on success, negative errno on error
 */
int reg_write_value(uint16_t addr, struct reg_value value, k_timeout_t timeout);

/**
 * @brief Register iteration callback function
 *
 * @param mapping Pointer to the current register mapping
 * @param user_data User-provided context data
 * @return 0 to continue iteration, non-zero to stop
 */
typedef int (*reg_foreach_cb_t)(const struct reg_mapping *mapping, void *user_data);

/**
 * @brief Iterate over all defined registers
 *
 * Calls the provided callback function for each register mapping.
 * Iteration stops if the callback returns a non-zero value.
 *
 * @param cb Callback function to call for each register
 * @param user_data User data to pass to the callback
 * @return Number of registers processed, or negative errno on error
 */
int reg_foreach(reg_foreach_cb_t cb, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_REGISTER_MAPPER_H_ */