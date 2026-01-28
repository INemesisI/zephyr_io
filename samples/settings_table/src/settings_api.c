/*
 * Copyright (c) 2026 Zephyr Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "settings_api.h"
#include <string.h>
#include <errno.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(table, LOG_LEVEL_INF);

/* Internal iteration macro */
#define SETTINGS_AREA_FOREACH(area) STRUCT_SECTION_FOREACH(setting_area, area)

/*============================================================================
 * State
 *============================================================================*/

static bool settings_initialized;

/*============================================================================
 * Type Helpers
 *============================================================================*/

static const size_t type_sizes[ST_TYPE_COUNT] = {
	[ST_U8] = 1,  [ST_U16] = 2, [ST_U32] = 4, [ST_U64] = 8, [ST_I8] = 1,   [ST_I16] = 2,
	[ST_I32] = 4, [ST_I64] = 8, [ST_F32] = 4, [ST_F64] = 8, [ST_BOOL] = 1,
};

static const char *type_names[ST_TYPE_COUNT] = {
	[ST_U8] = "u8",   [ST_U16] = "u16", [ST_U32] = "u32",   [ST_U64] = "u64",
	[ST_I8] = "i8",   [ST_I16] = "i16", [ST_I32] = "i32",   [ST_I64] = "i64",
	[ST_F32] = "f32", [ST_F64] = "f64", [ST_BOOL] = "bool",
};

size_t register_type_size(enum register_type type)
{
	if (type >= ST_TYPE_COUNT) {
		return 0;
	}
	return type_sizes[type];
}

const char *register_type_name(enum register_type type)
{
	if (type >= ST_TYPE_COUNT) {
		return "?";
	}
	return type_names[type];
}

static const char *flags_str(uint8_t flags)
{
	uint8_t rw = flags & ST_FLAG_RW;
	if (rw == ST_FLAG_RW) {
		return "RW";
	}
	if (rw == ST_FLAG_R) {
		return "R";
	}
	if (rw == ST_FLAG_W) {
		return "W";
	}
	return "-";
}

/*============================================================================
 * Value Comparison Helpers
 *============================================================================*/

static bool value_less_than(enum register_type type, union setting_value a, union setting_value b)
{
	switch (type) {
	case ST_U8:
		return a.u8 < b.u8;
	case ST_U16:
		return a.u16 < b.u16;
	case ST_U32:
		return a.u32 < b.u32;
	case ST_U64:
		return a.u64 < b.u64;
	case ST_I8:
		return a.i8 < b.i8;
	case ST_I16:
		return a.i16 < b.i16;
	case ST_I32:
		return a.i32 < b.i32;
	case ST_I64:
		return a.i64 < b.i64;
	case ST_F32:
		return a.f32 < b.f32;
	case ST_F64:
		return a.f64 < b.f64;
	case ST_BOOL:
		return false;
	default:
		return false;
	}
}

static bool value_greater_than(enum register_type type, union setting_value a,
			       union setting_value b)
{
	switch (type) {
	case ST_U8:
		return a.u8 > b.u8;
	case ST_U16:
		return a.u16 > b.u16;
	case ST_U32:
		return a.u32 > b.u32;
	case ST_U64:
		return a.u64 > b.u64;
	case ST_I8:
		return a.i8 > b.i8;
	case ST_I16:
		return a.i16 > b.i16;
	case ST_I32:
		return a.i32 > b.i32;
	case ST_I64:
		return a.i64 > b.i64;
	case ST_F32:
		return a.f32 > b.f32;
	case ST_F64:
		return a.f64 > b.f64;
	case ST_BOOL:
		return false;
	default:
		return false;
	}
}

/*============================================================================
 * Memory Access Helpers
 *============================================================================*/

static void read_value_from_mem(enum register_type type, const void *mem, union setting_value *out)
{
	switch (type) {
	case ST_U8:
		out->u8 = *(const uint8_t *)mem;
		break;
	case ST_U16:
		memcpy(&out->u16, mem, sizeof(uint16_t));
		break;
	case ST_U32:
		memcpy(&out->u32, mem, sizeof(uint32_t));
		break;
	case ST_U64:
		memcpy(&out->u64, mem, sizeof(uint64_t));
		break;
	case ST_I8:
		out->i8 = *(const int8_t *)mem;
		break;
	case ST_I16:
		memcpy(&out->i16, mem, sizeof(int16_t));
		break;
	case ST_I32:
		memcpy(&out->i32, mem, sizeof(int32_t));
		break;
	case ST_I64:
		memcpy(&out->i64, mem, sizeof(int64_t));
		break;
	case ST_F32:
		memcpy(&out->f32, mem, sizeof(float));
		break;
	case ST_F64:
		memcpy(&out->f64, mem, sizeof(double));
		break;
	case ST_BOOL:
		out->b = *(const bool *)mem;
		break;
	default:
		break;
	}
}

static void write_value_to_mem(enum register_type type, void *mem, union setting_value val)
{
	switch (type) {
	case ST_U8:
		*(uint8_t *)mem = val.u8;
		break;
	case ST_U16:
		memcpy(mem, &val.u16, sizeof(uint16_t));
		break;
	case ST_U32:
		memcpy(mem, &val.u32, sizeof(uint32_t));
		break;
	case ST_U64:
		memcpy(mem, &val.u64, sizeof(uint64_t));
		break;
	case ST_I8:
		*(int8_t *)mem = val.i8;
		break;
	case ST_I16:
		memcpy(mem, &val.i16, sizeof(int16_t));
		break;
	case ST_I32:
		memcpy(mem, &val.i32, sizeof(int32_t));
		break;
	case ST_I64:
		memcpy(mem, &val.i64, sizeof(int64_t));
		break;
	case ST_F32:
		memcpy(mem, &val.f32, sizeof(float));
		break;
	case ST_F64:
		memcpy(mem, &val.f64, sizeof(double));
		break;
	case ST_BOOL:
		*(bool *)mem = val.b;
		break;
	default:
		break;
	}
}

/*============================================================================
 * Validation
 *============================================================================*/

bool settings_reg_validate_range(const struct setting_reg *reg, union setting_value value)
{
	if (reg->flags & ST_FLAG_NO_RANGE) {
		return true;
	}

	if (value_less_than(reg->type, value, reg->min)) {
		return false;
	}
	if (value_greater_than(reg->type, value, reg->max)) {
		return false;
	}
	return true;
}

void settings_reg_get_default(const struct setting_reg *reg, union setting_value *value)
{
	*value = reg->dflt;
}

/*============================================================================
 * Range Helpers
 *============================================================================*/

static bool ranges_overlap(uint16_t a_start, uint16_t a_end, uint16_t b_start, uint16_t b_end)
{
	return (a_start < b_end) && (b_start < a_end);
}

/*============================================================================
 * Initialization Validation
 *============================================================================*/

/* Validate no areas overlap in address space */
static int validate_area_addresses(void)
{
	SETTINGS_AREA_FOREACH(ai)
	{
		uint16_t ai_end = ai->base + ai->size;

		SETTINGS_AREA_FOREACH(aj)
		{
			if (ai >= aj) {
				continue;
			}
			uint16_t aj_end = aj->base + aj->size;

			if (ranges_overlap(ai->base, ai_end, aj->base, aj_end)) {
				return -EINVAL;
			}
		}
	}
	return 0;
}

/* Validate registers within a register-backed area */
static int validate_reg_area(const struct setting_area *area, const struct setting_reg_ctx *ctx)
{
	for (int i = 0; i < ctx->reg_count; i++) {
		const struct setting_reg *ri = &ctx->regs[i];
		size_t ri_size = register_type_size(ri->type);

		/* Check register fits in struct */
		if (ri->offset + ri_size > area->size) {
			return -EINVAL;
		}

		/* Check register fits in address space */
		if (ri->addr + ri_size > area->size) {
			return -EINVAL;
		}

		/* Check no overlap with other registers */
		for (int j = i + 1; j < ctx->reg_count; j++) {
			const struct setting_reg *rj = &ctx->regs[j];
			size_t rj_size = register_type_size(rj->type);

			if (ranges_overlap(ri->addr, ri->addr + ri_size, rj->addr,
					   rj->addr + rj_size)) {
				return -EINVAL;
			}

			if (ranges_overlap(ri->offset, ri->offset + ri_size, rj->offset,
					   rj->offset + rj_size)) {
				return -EINVAL;
			}
		}

		/* Check default value is valid */
		if (!settings_reg_validate_range(ri, ri->dflt)) {
			return -ERANGE;
		}
	}
	return 0;
}

/* Apply default values to all registers in a register-backed area */
static void apply_reg_defaults(struct setting_reg_ctx *ctx)
{
	struct weave_observable *obs = ctx->observable;
	void *data = weave_observable_claim(obs, K_FOREVER);

	for (int i = 0; i < ctx->reg_count; i++) {
		const struct setting_reg *reg = &ctx->regs[i];
		void *mem = (uint8_t *)data + reg->offset;
		write_value_to_mem(reg->type, mem, reg->dflt);
	}

	weave_observable_publish(obs);
}

/*============================================================================
 * SYS_INIT
 *============================================================================*/

static int settings_sys_init(void)
{
	/* Validate area addresses don't overlap */
	int ret = validate_area_addresses();
	if (ret < 0) {
		return ret;
	}

	/* Validate register areas and apply defaults */
	SETTINGS_AREA_FOREACH(area)
	{
		struct setting_reg_ctx *ctx = setting_area_get_reg_ctx(area);
		if (ctx) {
			ret = validate_reg_area(area, ctx);
			if (ret < 0) {
				return ret;
			}
			apply_reg_defaults(ctx);
		}
	}

	settings_initialized = true;
	return 0;
}

SYS_INIT(settings_sys_init, APPLICATION, CONFIG_SETTINGS_INIT_PRIORITY);

/*============================================================================
 * Status API
 *============================================================================*/

bool settings_is_initialized(void)
{
	return settings_initialized;
}

size_t settings_area_count(void)
{
	size_t count = 0;
	SETTINGS_AREA_FOREACH(area)
	{
		(void)area;
		count++;
	}
	return count;
}

/*============================================================================
 * Lookup
 *============================================================================*/

const struct setting_area *settings_find_area(uint16_t addr)
{
	SETTINGS_AREA_FOREACH(area)
	{
		if (addr >= area->base && addr < area->base + area->size) {
			return area;
		}
	}
	return NULL;
}

const struct setting_reg *settings_find_reg(uint16_t addr)
{
	const struct setting_area *area = settings_find_area(addr);
	if (!area) {
		return NULL;
	}

	struct setting_reg_ctx *ctx = setting_area_get_reg_ctx(area);
	if (!ctx) {
		return NULL;
	}

	uint16_t offset = addr - area->base;
	for (int i = 0; i < ctx->reg_count; i++) {
		const struct setting_reg *r = &ctx->regs[i];
		if (r->addr == offset) {
			return r;
		}
	}
	return NULL;
}

/*============================================================================
 * Register Area Read/Write Callbacks
 *============================================================================*/

/* Check if a write touches any read-only register */
static int check_reg_write_access(const struct setting_reg_ctx *ctx, uint16_t offset, size_t len)
{
	uint16_t write_end = offset + len;

	for (int i = 0; i < ctx->reg_count; i++) {
		const struct setting_reg *r = &ctx->regs[i];
		size_t r_size = register_type_size(r->type);
		uint16_t r_end = r->addr + r_size;

		if (ranges_overlap(offset, write_end, r->addr, r_end)) {
			if (!(r->flags & ST_FLAG_W)) {
				return -EROFS;
			}
		}
	}
	return 0;
}

/* Validate all registers affected by a write */
static int validate_reg_write(const struct setting_reg_ctx *ctx, uint16_t offset, size_t len,
			      const uint8_t *proposed_data)
{
	uint16_t write_end = offset + len;

	for (int i = 0; i < ctx->reg_count; i++) {
		const struct setting_reg *r = &ctx->regs[i];
		size_t r_size = register_type_size(r->type);
		uint16_t r_end = r->addr + r_size;

		if (ranges_overlap(offset, write_end, r->addr, r_end)) {
			union setting_value val;
			read_value_from_mem(r->type, proposed_data + r->offset, &val);

			if (!settings_reg_validate_range(r, val)) {
				return -ERANGE;
			}
		}
	}
	return 0;
}

int setting_reg_read(const struct setting_area *area, uint16_t offset, void *buf, size_t len)
{
	struct setting_reg_ctx *ctx = (struct setting_reg_ctx *)area->ctx;
	struct weave_observable *obs = ctx->observable;

	if (offset + len > area->size) {
		len = area->size - offset;
	}

	/* Thread-safe read from observable */
	k_sem_take(&obs->sem, K_FOREVER);
	memcpy(buf, (uint8_t *)obs->value + offset, len);
	k_sem_give(&obs->sem);

	return (int)len;
}

int setting_reg_write(const struct setting_area *area, uint16_t offset, const void *buf, size_t len)
{
	struct setting_reg_ctx *ctx = (struct setting_reg_ctx *)area->ctx;
	struct weave_observable *obs = ctx->observable;

	if (offset + len > area->size) {
		len = area->size - offset;
	}

	/* Check write access (read-only registers) */
	int ret = check_reg_write_access(ctx, offset, len);
	if (ret < 0) {
		return ret;
	}

	/* Create temporary copy with proposed changes for validation */
	uint8_t temp[area->size];
	k_sem_take(&obs->sem, K_FOREVER);
	memcpy(temp, obs->value, area->size);
	k_sem_give(&obs->sem);
	memcpy(temp + offset, buf, len);

	/* Validate affected registers (per-register min/max) */
	ret = validate_reg_write(ctx, offset, len, temp);
	if (ret < 0) {
		return ret;
	}

	/* Validate via observable's validator (cross-register validation) */
	if (obs->validator) {
		ret = obs->validator(obs, temp, obs->owner_sink.user_data);
		if (ret < 0) {
			return ret;
		}
	}

	/* Claim, apply changes, and publish to all observers */
	void *data = weave_observable_claim(obs, K_FOREVER);
	memcpy((uint8_t *)data + offset, buf, len);
	weave_observable_publish(obs);

	return (int)len;
}

/*============================================================================
 * Read/Write API
 *============================================================================*/

int settings_read(uint16_t addr, void *buf, size_t len)
{
	if (!buf) {
		return -EINVAL;
	}

	if (!settings_initialized) {
		return -ENXIO;
	}

	const struct setting_area *area = settings_find_area(addr);
	if (!area) {
		return -ENOENT;
	}

	uint16_t offset = addr - area->base;
	return area->read(area, offset, buf, len);
}

int settings_write(uint16_t addr, const void *buf, size_t len)
{
	if (!buf) {
		return -EINVAL;
	}

	if (!settings_initialized) {
		return -ENXIO;
	}

	const struct setting_area *area = settings_find_area(addr);
	if (!area) {
		return -ENOENT;
	}

	uint16_t offset = addr - area->base;
	return area->write(area, offset, buf, len);
}

/*============================================================================
 * Register API
 *============================================================================*/

int settings_reg_get(const struct setting_area *area, const struct setting_reg *reg,
		     union setting_value *value)
{
	if (!area || !reg || !value) {
		return -EINVAL;
	}

	if (!settings_initialized) {
		return -ENXIO;
	}

	struct setting_reg_ctx *ctx = setting_area_get_reg_ctx(area);
	if (!ctx) {
		return -EINVAL;
	}

	if (!(reg->flags & ST_FLAG_R)) {
		return -EROFS;
	}

	struct weave_observable *obs = ctx->observable;
	k_sem_take(&obs->sem, K_FOREVER);
	void *mem = (uint8_t *)obs->value + reg->offset;
	read_value_from_mem(reg->type, mem, value);
	k_sem_give(&obs->sem);

	return 0;
}

int settings_reg_set(const struct setting_area *area, const struct setting_reg *reg,
		     union setting_value value)
{
	if (!area || !reg) {
		return -EINVAL;
	}

	if (!settings_initialized) {
		return -ENXIO;
	}

	struct setting_reg_ctx *ctx = setting_area_get_reg_ctx(area);
	if (!ctx) {
		return -EINVAL;
	}

	if (!(reg->flags & ST_FLAG_W)) {
		return -EROFS;
	}

	/* Validate range */
	if (!settings_reg_validate_range(reg, value)) {
		return -ERANGE;
	}

	struct weave_observable *obs = ctx->observable;

	/* Create temporary copy for struct validation */
	if (obs->validator) {
		uint8_t temp[area->size];
		k_sem_take(&obs->sem, K_FOREVER);
		memcpy(temp, obs->value, area->size);
		k_sem_give(&obs->sem);
		write_value_to_mem(reg->type, temp + reg->offset, value);

		int ret = obs->validator(obs, temp, obs->owner_sink.user_data);
		if (ret < 0) {
			return ret;
		}
	}

	/* Claim, apply change, and publish to all observers */
	void *data = weave_observable_claim(obs, K_FOREVER);
	void *mem = (uint8_t *)data + reg->offset;
	write_value_to_mem(reg->type, mem, value);
	weave_observable_publish(obs);

	return 0;
}

/*============================================================================
 * Reset to Defaults
 *============================================================================*/

int settings_reset_defaults(const struct setting_area *area)
{
	if (!settings_initialized) {
		return -ENXIO;
	}

	if (area) {
		/* Reset single area */
		struct setting_reg_ctx *ctx = setting_area_get_reg_ctx(area);
		if (!ctx) {
			return -EINVAL; /* Not a register area */
		}
		apply_reg_defaults(ctx);
	} else {
		/* Reset all register areas */
		SETTINGS_AREA_FOREACH(a)
		{
			struct setting_reg_ctx *ctx = setting_area_get_reg_ctx(a);
			if (ctx) {
				apply_reg_defaults(ctx);
			}
		}
	}

	return 0;
}

/*============================================================================
 * Debug
 *============================================================================*/

void settings_dump(void)
{
	LOG_INF("Settings Table: %zu areas, %s", settings_area_count(),
		settings_initialized ? "initialized" : "NOT initialized");

	SETTINGS_AREA_FOREACH(area)
	{
		struct setting_reg_ctx *ctx = setting_area_get_reg_ctx(area);

		if (ctx) {
			LOG_INF("  Area '%s' @ 0x%04x, size=%u, regs=%u", area->name, area->base,
				area->size, ctx->reg_count);

			for (int j = 0; j < ctx->reg_count; j++) {
				const struct setting_reg *r = &ctx->regs[j];
#if CONFIG_SETTINGS_REG_NAMES
				const char *name = r->name ? r->name : "?";
#else
				const char *name = "?";
#endif
				LOG_INF("    [0x%04x] %-16s %-4s %s", area->base + r->addr, name,
					register_type_name(r->type), flags_str(r->flags));
			}
		} else {
			LOG_INF("  Area '%s' @ 0x%04x, size=%u (custom)", area->name, area->base,
				area->size);
		}
	}
}
