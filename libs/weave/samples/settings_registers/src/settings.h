/*
 * Copyright (c) 2026 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SETTINGS_H_
#define SETTINGS_H_

#include <weave/observable.h>

/* Field access flags */
#define SETTING_FLAG_R  0x01
#define SETTING_FLAG_W  0x02
#define SETTING_FLAG_RW (SETTING_FLAG_R | SETTING_FLAG_W)

/* Field types */
enum setting_type {
	SETTING_U8,
	SETTING_U16,
	SETTING_U32,
	SETTING_U64,
	SETTING_I8,
	SETTING_I16,
	SETTING_I32,
	SETTING_I64,
	SETTING_F32,
	SETTING_F64,
	SETTING_BOOL,
};

/* Field descriptor */
struct setting_field {
	const char *name;
	uint8_t reg;     /* Offset from base */
	uint16_t offset; /* offsetof() in struct */
	enum setting_type type;
	uint8_t flags;
};

/* Setting group descriptor */
struct setting_group {
	const char *name;
	uint16_t base_reg;
	uint16_t size;
	const struct setting_field *fields;
	uint8_t field_count;
};

/* Type helpers */
uint8_t setting_type_size(enum setting_type type);
const char *setting_type_name(enum setting_type type);

/* Generic field definition (manual) */
#define SETTING_FIELD(_name, _reg, _offset, _setting_type, _flags)                                 \
	{                                                                                          \
		_name, _reg, _offset, _setting_type, _flags                                        \
	}

/* Auto-detect type from struct member using _Generic */
#define SETTING_TYPE_DETECT(_type, _member)                                                        \
	_Generic(((_type *)0)->_member,                                                            \
		uint8_t: SETTING_U8,                                                               \
		uint16_t: SETTING_U16,                                                             \
		uint32_t: SETTING_U32,                                                             \
		uint64_t: SETTING_U64,                                                             \
		int8_t: SETTING_I8,                                                                \
		int16_t: SETTING_I16,                                                              \
		int32_t: SETTING_I32,                                                              \
		int64_t: SETTING_I64,                                                              \
		float: SETTING_F32,                                                                \
		double: SETTING_F64,                                                               \
		_Bool: SETTING_BOOL,                                                               \
		default: SETTING_U8)

/* Field with auto-detected type (name stringified from member) */
#define SETTING_FIELD_FROM_TYPE(_reg, _type, _member, _flags)                                      \
	SETTING_FIELD(#_member, _reg, offsetof(_type, _member),                                    \
		      SETTING_TYPE_DETECT(_type, _member), _flags)

/* Define a settings observable - passes through to WEAVE_OBSERVABLE_DEFINE */
#define SETTING_OBSERVABLE_DEFINE WEAVE_OBSERVABLE_DEFINE

/* Registry API */
int settings_register(struct weave_observable *obs);
int settings_get_count(void);
struct weave_observable *settings_get_at(int index);

/* Lookup API */
int settings_find_reg(uint16_t reg, struct weave_observable **obs_out,
		      const struct setting_field **field_out);
const struct setting_field *settings_find_field(uint16_t reg);

/* Register API - generic read/write by address */
int settings_read(uint16_t reg, void *buf, size_t len);
int settings_write(uint16_t reg, const void *buf, size_t len);

/* Field API - direct access via field pointer */
int settings_field_get(struct weave_observable *obs, const struct setting_field *field, void *buf);
int settings_field_set(struct weave_observable *obs, const struct setting_field *field,
		       const void *buf);

/* Get group from observable */
static inline const struct setting_group *settings_get_group(struct weave_observable *obs)
{
	return (const struct setting_group *)obs->owner_sink.user_data;
}

#endif /* SETTINGS_H_ */
