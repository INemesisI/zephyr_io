/*
 * Copyright (c) 2026 Zephyr Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * Settings Area API
 *
 * A layered settings system for embedded applications:
 *
 * Layer 1: Generic Areas
 *   - Areas with read/write callbacks
 *   - Registered via iterable sections
 *   - Base address, size, name
 *
 * Layer 2: Register Areas (built on Layer 1)
 *   - Struct-backed areas with register descriptors
 *   - Per-register min/max/default validation
 *   - Cross-register struct validation
 *   - Change notifications
 *
 * Custom areas (I/O mapped, proxied, virtual) can implement their own
 * read/write callbacks while coexisting with register-backed areas.
 */

#ifndef SETTINGS_API_H_
#define SETTINGS_API_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <zephyr/sys/iterable_sections.h>
#include <weave/observable.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Configuration
 *============================================================================*/

#ifndef CONFIG_SETTINGS_REG_NAMES
#define CONFIG_SETTINGS_REG_NAMES 1
#endif

#ifndef CONFIG_SETTINGS_INIT_PRIORITY
#define CONFIG_SETTINGS_INIT_PRIORITY 90
#endif

/*============================================================================
 * Error Codes (standard errno)
 *============================================================================
 *
 * Return values from read/write callbacks:
 *   >= 0     Number of bytes read/written
 *   -ERANGE  Value out of valid range
 *   -ENOENT  Address not mapped within area
 *   -EROFS   Write to read-only area/register
 *   -EINVAL  Invalid argument or validation failed
 *   -EIO     I/O error
 *
 * Additional errors from settings_read/write:
 *   -ENXIO   System not initialized
 *   -EFAULT  Access spans unmapped region (hole between areas)
 */

/*============================================================================
 * Forward Declarations
 *============================================================================*/

struct setting_area;
struct setting_reg;
struct setting_reg_ctx;

/*============================================================================
 * Generic Area Interface (Layer 1)
 *============================================================================*/

/**
 * Area read callback
 *
 * @param area   Pointer to area descriptor
 * @param offset Offset from area base (0 = first byte of area)
 * @param buf    Output buffer
 * @param len    Number of bytes to read
 * @return Number of bytes read (>= 0) or negative errno
 */
typedef int (*setting_area_read_t)(const struct setting_area *area, uint16_t offset, void *buf,
				   size_t len);

/**
 * Area write callback
 *
 * @param area   Pointer to area descriptor
 * @param offset Offset from area base (0 = first byte of area)
 * @param buf    Input buffer
 * @param len    Number of bytes to write
 * @return Number of bytes written (>= 0) or negative errno
 */
typedef int (*setting_area_write_t)(const struct setting_area *area, uint16_t offset,
				    const void *buf, size_t len);

/**
 * Generic area descriptor
 *
 * Base type for all settings areas. Custom areas implement their own
 * read/write callbacks. Register areas use the provided helpers.
 */
struct setting_area {
	const char *name;           /* Area name */
	uint16_t base;              /* Base address in settings space */
	uint16_t size;              /* Size in bytes */
	setting_area_read_t read;   /* Read callback */
	setting_area_write_t write; /* Write callback */
	void *ctx;                  /* Context for callbacks */
};

/*============================================================================
 * Register Area Interface (Layer 2)
 *============================================================================*/

/**
 * Register data types
 */
enum register_type {
	ST_U8 = 0,
	ST_U16,
	ST_U32,
	ST_U64,
	ST_I8,
	ST_I16,
	ST_I32,
	ST_I64,
	ST_F32,
	ST_F64,
	ST_BOOL,
	ST_TYPE_COUNT
};

/**
 * Type-safe value union
 */
union setting_value {
	uint8_t u8;
	uint16_t u16;
	uint32_t u32;
	uint64_t u64;
	int8_t i8;
	int16_t i16;
	int32_t i32;
	int64_t i64;
	float f32;
	double f64;
	bool b;
};

/**
 * Access flags for registers
 */
enum setting_flags {
	ST_FLAG_R = 0x01,       /* Readable */
	ST_FLAG_W = 0x02,       /* Writable */
	ST_FLAG_RW = 0x03,      /* Read-write */
	ST_FLAG_NO_RANGE = 0x04 /* Skip min/max validation */
};

/**
 * Register descriptor
 */
struct setting_reg {
#if CONFIG_SETTINGS_REG_NAMES
	const char *name; /* Register name for debugging */
#endif
	uint16_t addr;            /* Offset from area base */
	uint16_t offset;          /* Byte offset within backing struct */
	enum register_type type;  /* Data type */
	uint8_t flags;            /* Access flags (ST_FLAG_*) */
	union setting_value min;  /* Minimum valid value */
	union setting_value max;  /* Maximum valid value */
	union setting_value dflt; /* Default value */
};

/**
 * Register area context
 *
 * Contains all register-specific data. Passed as ctx to generic area.
 * Uses weave_observable for data storage and pub/sub notifications.
 */
struct setting_reg_ctx {
	struct weave_observable *observable; /* Observable holding the data */
	const struct setting_reg *regs;      /* Register descriptors */
	uint8_t reg_count;                   /* Number of registers */
};

/*============================================================================
 * Register Area Callbacks (provided by API)
 *============================================================================*/

/**
 * Read callback for register-backed areas
 *
 * Reads from the backing struct, respecting read permissions.
 */
int setting_reg_read(const struct setting_area *area, uint16_t offset, void *buf, size_t len);

/**
 * Write callback for register-backed areas
 *
 * Validates values, calls struct validator, updates backing struct,
 * calls change callback.
 */
int setting_reg_write(const struct setting_area *area, uint16_t offset, const void *buf,
		      size_t len);

/*============================================================================
 * Type Utilities
 *============================================================================*/

/**
 * Get size of a setting type in bytes
 */
size_t register_type_size(enum register_type type);

/**
 * Get name of a setting type
 */
const char *register_type_name(enum register_type type);

/*============================================================================
 * Register Definition Macros
 *============================================================================*/

#if CONFIG_SETTINGS_REG_NAMES
#define _ST_NAME(member) .name = #member,
#else
#define _ST_NAME(member)
#endif

/* Auto-detect type from struct member */
#define ST_TYPE_DETECT(_type, _member)                                                             \
	_Generic(((_type *)0)->_member,                                                            \
		uint8_t: ST_U8,                                                                    \
		uint16_t: ST_U16,                                                                  \
		uint32_t: ST_U32,                                                                  \
		uint64_t: ST_U64,                                                                  \
		int8_t: ST_I8,                                                                     \
		int16_t: ST_I16,                                                                   \
		int32_t: ST_I32,                                                                   \
		int64_t: ST_I64,                                                                   \
		float: ST_F32,                                                                     \
		double: ST_F64,                                                                    \
		bool: ST_BOOL,                                                                     \
		default: ST_U8)

/* Initialize value union with correct member */
#define ST_VALUE(_type, _member, _val)                                                             \
	_Generic(((_type *)0)->_member,                                                            \
		uint8_t: (union setting_value){.u8 = (_val)},                                      \
		uint16_t: (union setting_value){.u16 = (_val)},                                    \
		uint32_t: (union setting_value){.u32 = (_val)},                                    \
		uint64_t: (union setting_value){.u64 = (_val)},                                    \
		int8_t: (union setting_value){.i8 = (_val)},                                       \
		int16_t: (union setting_value){.i16 = (_val)},                                     \
		int32_t: (union setting_value){.i32 = (_val)},                                     \
		int64_t: (union setting_value){.i64 = (_val)},                                     \
		float: (union setting_value){.f32 = (_val)},                                       \
		double: (union setting_value){.f64 = (_val)},                                      \
		bool: (union setting_value){.b = (_val)},                                          \
		default: (union setting_value){.u8 = (_val)})

/**
 * Define a register descriptor
 *
 * @param _addr    Offset from area base
 * @param _type    Struct type
 * @param _member  Struct member name
 * @param _min     Minimum valid value
 * @param _max     Maximum valid value
 * @param _dflt    Default value
 * @param _flags   Access flags (ST_FLAG_*)
 */
#define REGISTER(_addr, _type, _member, _min, _max, _dflt, _flags)                                 \
	{                                                                                          \
		_ST_NAME(_member).addr = (_addr), .offset = offsetof(_type, _member),              \
		.type = ST_TYPE_DETECT(_type, _member), .flags = (_flags),                         \
		.min = ST_VALUE(_type, _member, _min), .max = ST_VALUE(_type, _member, _max),      \
		.dflt = ST_VALUE(_type, _member, _dflt)                                            \
	}

/**
 * Define a bool register (no min/max)
 */
#define REGISTER_BOOL(_addr, _type, _member, _dflt, _flags)                                        \
	{                                                                                          \
		_ST_NAME(_member).addr = (_addr), .offset = offsetof(_type, _member),              \
		.type = ST_BOOL, .flags = (_flags) | ST_FLAG_NO_RANGE, .dflt.b = (_dflt)           \
	}

/*============================================================================
 * Area Definition Macros
 *============================================================================*/

/**
 * Define a custom area with user-provided read/write callbacks
 *
 * @param _name      Area name (C identifier)
 * @param _base      Base address
 * @param _size      Size in bytes
 * @param _read      Read callback
 * @param _write     Write callback
 * @param _ctx       Context pointer for callbacks
 */
#define SETTING_AREA_DEFINE(_name, _base, _size, _read, _write, _ctx)                              \
	STRUCT_SECTION_ITERABLE(setting_area, _name##_area) = {                                    \
		.name = #_name,                                                                    \
		.base = (_base),                                                                   \
		.size = (_size),                                                                   \
		.read = (_read),                                                                   \
		.write = (_write),                                                                 \
		.ctx = (_ctx),                                                                     \
	}

/**
 * Define a complete register-backed settings area
 *
 * Creates the observable, register context, and area descriptor in one macro.
 * Auto-registered via iterable section.
 *
 * @param _name       Area name (C identifier)
 * @param _base       Base address
 * @param _type       Struct type for the settings data
 * @param _regs       Array of register descriptors
 * @param _reg_count  Number of registers
 * @param _handler    Owner handler (WV_NO_HANDLER if none)
 * @param _validator  Validator function (WV_NO_VALID if none)
 */
#define SETTING_REG_AREA_DEFINE(_name, _base, _type, _regs, _reg_count, _handler, _validator)      \
	WEAVE_OBSERVABLE_DEFINE(_name##_obs, _type, _handler, WV_IMMEDIATE, NULL, _validator);     \
	static struct setting_reg_ctx _name##_reg_ctx = {                                          \
		.observable = &_name##_obs,                                                        \
		.regs = (_regs),                                                                   \
		.reg_count = (_reg_count),                                                         \
	};                                                                                         \
	STRUCT_SECTION_ITERABLE(setting_area, _name##_area) = {                                    \
		.name = #_name,                                                                    \
		.base = (_base),                                                                   \
		.size = sizeof(_type),                                                             \
		.read = setting_reg_read,                                                          \
		.write = setting_reg_write,                                                        \
		.ctx = &_name##_reg_ctx,                                                           \
	}

/*============================================================================
 * Initialization API
 *============================================================================*/

/**
 * Check if settings system is initialized
 */
bool settings_is_initialized(void);

/**
 * Get number of registered areas
 */
size_t settings_area_count(void);

/*============================================================================
 * Access API
 *============================================================================*/

/**
 * Read from settings address space
 *
 * @param addr  Address to read from
 * @param buf   Output buffer
 * @param len   Number of bytes to read
 * @return Bytes read (>= 0) or negative errno
 */
int settings_read(uint16_t addr, void *buf, size_t len);

/**
 * Write to settings address space
 *
 * @param addr  Address to write to
 * @param buf   Input buffer
 * @param len   Number of bytes to write
 * @return Bytes written (>= 0) or negative errno
 */
int settings_write(uint16_t addr, const void *buf, size_t len);

/*============================================================================
 * Register-Specific API (for register-backed areas)
 *============================================================================*/

/**
 * Get register context from area (NULL if not a register area)
 */
static inline struct setting_reg_ctx *setting_area_get_reg_ctx(const struct setting_area *area)
{
	if (area->read == setting_reg_read) {
		return (struct setting_reg_ctx *)area->ctx;
	}
	return NULL;
}

/**
 * Read a register value
 *
 * @param area  Area containing the register
 * @param reg   Register descriptor
 * @param value Output value
 * @return 0 on success, negative errno on failure
 */
int settings_reg_get(const struct setting_area *area, const struct setting_reg *reg,
		     union setting_value *value);

/**
 * Write a register value
 *
 * @param area  Area containing the register
 * @param reg   Register descriptor
 * @param value Value to write
 * @return 0 on success, negative errno on failure
 */
int settings_reg_set(const struct setting_area *area, const struct setting_reg *reg,
		     union setting_value value);

/**
 * Reset area to default values (register areas only)
 *
 * @param area  Area to reset (NULL = all register areas)
 * @return 0 on success, negative errno on failure
 */
int settings_reset_defaults(const struct setting_area *area);

/*============================================================================
 * Lookup API
 *============================================================================*/

/**
 * Find area containing an address
 */
const struct setting_area *settings_find_area(uint16_t addr);

/**
 * Find register at address (register areas only)
 */
const struct setting_reg *settings_find_reg(uint16_t addr);

/*============================================================================
 * Utility API
 *============================================================================*/

/**
 * Get default value for a register
 */
void settings_reg_get_default(const struct setting_reg *reg, union setting_value *value);

/**
 * Check if value is within register's min/max range
 */
bool settings_reg_validate_range(const struct setting_reg *reg, union setting_value value);

/**
 * Print table structure for debugging
 */
void settings_dump(void);

#ifdef __cplusplus
}
#endif

#endif /* SETTINGS_API_H_ */
