/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Register type definitions
 *
 * Defines the type system for register values including tagged unions
 * and helper macros for type-safe register access.
 */

#ifndef ZEPHYR_INCLUDE_REGISTER_TYPES_H_
#define ZEPHYR_INCLUDE_REGISTER_TYPES_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Register data types */
enum reg_type {
	REG_TYPE_U8,
	REG_TYPE_U16,
	REG_TYPE_U32,
	REG_TYPE_U64,
	REG_TYPE_I8,
	REG_TYPE_I16,
	REG_TYPE_I32,
	REG_TYPE_I64,
	REG_TYPE_COUNT /* Must be last - used for bounds checking */
};

/* Tagged union for register values */
struct reg_value {
	enum reg_type type;
	union {
		uint8_t u8;
		uint16_t u16;
		uint32_t u32;
		uint64_t u64;
		int8_t i8;
		int16_t i16;
		int32_t i32;
		int64_t i64;
	} val;
};

/* Helper macros for creating tagged values */
#define REG_VALUE_U8(v)  ((struct reg_value){.type = REG_TYPE_U8, .val.u8 = (v)})
#define REG_VALUE_U16(v) ((struct reg_value){.type = REG_TYPE_U16, .val.u16 = (v)})
#define REG_VALUE_U32(v) ((struct reg_value){.type = REG_TYPE_U32, .val.u32 = (v)})
#define REG_VALUE_U64(v) ((struct reg_value){.type = REG_TYPE_U64, .val.u64 = (v)})
#define REG_VALUE_I8(v)  ((struct reg_value){.type = REG_TYPE_I8, .val.i8 = (v)})
#define REG_VALUE_I16(v) ((struct reg_value){.type = REG_TYPE_I16, .val.i16 = (v)})
#define REG_VALUE_I32(v) ((struct reg_value){.type = REG_TYPE_I32, .val.i32 = (v)})
#define REG_VALUE_I64(v) ((struct reg_value){.type = REG_TYPE_I64, .val.i64 = (v)})

/* Compile-time size calculation for each type */
#define REG_TYPE_SIZE_CONST(type)                                                                  \
	((type) == REG_TYPE_U8    ? 1                                                              \
	 : (type) == REG_TYPE_U16 ? 2                                                              \
	 : (type) == REG_TYPE_U32 ? 4                                                              \
	 : (type) == REG_TYPE_U64 ? 8                                                              \
	 : (type) == REG_TYPE_I8  ? 1                                                              \
	 : (type) == REG_TYPE_I16 ? 2                                                              \
	 : (type) == REG_TYPE_I32 ? 4                                                              \
	 : (type) == REG_TYPE_I64 ? 8                                                              \
				  : 0)

/* Helper to get size from type at runtime */
size_t reg_type_size(enum reg_type type);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_REGISTER_TYPES_H_ */