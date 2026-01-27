/*
 * Copyright (c) 2026 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "settings.h"
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(settings, LOG_LEVEL_INF);

/* ============================ Type Helpers ============================ */

/* U8, U16, U32, U64, I8, I16, I32, I64, F32, F64, BOOL */
static const uint8_t type_sizes[] = {1, 2, 4, 8, 1, 2, 4, 8, 4, 8, 1};
static const char *type_names[] = {"u8",  "u16", "u32", "u64", "i8",  "i16",
				   "i32", "i64", "f32", "f64", "bool"};

uint8_t setting_type_size(enum setting_type type)
{
	if (type >= ARRAY_SIZE(type_sizes)) {
		return 0;
	}
	return type_sizes[type];
}

const char *setting_type_name(enum setting_type type)
{
	if (type >= ARRAY_SIZE(type_names)) {
		return "?";
	}
	return type_names[type];
}

/* ============================ Registry ============================ */

#define SETTINGS_MAX_OBSERVABLES 8

static struct weave_observable *registry[SETTINGS_MAX_OBSERVABLES];
static uint8_t registry_count;

int settings_register(struct weave_observable *obs)
{
	if (!obs) {
		return -EINVAL;
	}

	if (registry_count >= SETTINGS_MAX_OBSERVABLES) {
		return -ENOMEM;
	}

	registry[registry_count++] = obs;
	LOG_DBG("Registered settings observable: %p", obs);
	return 0;
}

int settings_get_count(void)
{
	return registry_count;
}

struct weave_observable *settings_get_at(int index)
{
	if (index < 0 || index >= registry_count) {
		return NULL;
	}
	return registry[index];
}

/* ============================ Register Lookup ============================ */

int settings_find_reg(uint16_t reg, struct weave_observable **obs_out,
		      const struct setting_field **field_out)
{
	for (int i = 0; i < registry_count; i++) {
		struct weave_observable *obs = registry[i];
		const struct setting_group *grp = settings_get_group(obs);

		if (!grp) {
			continue;
		}

		/* Check if register is within this group's range */
		for (int j = 0; j < grp->field_count; j++) {
			uint16_t field_reg = grp->base_reg + grp->fields[j].reg;
			if (reg == field_reg) {
				*obs_out = obs;
				*field_out = &grp->fields[j];
				return 0;
			}
		}
	}
	return -ENOENT;
}

const struct setting_field *settings_find_field(uint16_t reg)
{
	struct weave_observable *obs;
	const struct setting_field *field;

	if (settings_find_reg(reg, &obs, &field) == 0) {
		return field;
	}
	return NULL;
}

/* ============================ Helper ============================ */

/**
 * Find observable containing register and calculate byte offset.
 * Returns observable size available from offset.
 */
static int settings_find_obs(uint16_t reg, struct weave_observable **obs_out, size_t *offset_out)
{
	for (int i = 0; i < registry_count; i++) {
		struct weave_observable *obs = registry[i];
		const struct setting_group *grp = settings_get_group(obs);

		if (!grp) {
			continue;
		}

		/* Check if register is within this group's declared range */
		if (reg >= grp->base_reg && reg < grp->base_reg + grp->size) {
			*obs_out = obs;
			*offset_out = reg - grp->base_reg;
			return 0;
		}
	}
	return -ENOENT;
}

/* ============================ Bulk API ============================ */

int settings_read(uint16_t reg, void *buf, size_t len)
{
	struct weave_observable *obs;
	size_t offset;

	int ret = settings_find_obs(reg, &obs, &offset);
	if (ret < 0) {
		return ret;
	}

	const struct setting_group *grp = settings_get_group(obs);

	/* Limit to declared range and actual struct size */
	size_t range_avail = grp->size - offset;
	size_t struct_avail = obs->size - offset;
	size_t avail = (range_avail < struct_avail) ? range_avail : struct_avail;
	size_t to_read = (len < avail) ? len : avail;

	void *value = weave_observable_claim(obs, K_FOREVER);
	if (!value) {
		return -EAGAIN;
	}

	memcpy(buf, (uint8_t *)value + offset, to_read);
	weave_observable_finish(obs);

	return to_read;
}

int settings_write(uint16_t reg, const void *buf, size_t len)
{
	struct weave_observable *obs;
	size_t offset;

	int ret = settings_find_obs(reg, &obs, &offset);
	if (ret < 0) {
		return ret;
	}

	const struct setting_group *grp = settings_get_group(obs);

	/* Limit to declared range and actual struct size */
	size_t range_avail = grp->size - offset;
	size_t struct_avail = obs->size - offset;
	size_t avail = (range_avail < struct_avail) ? range_avail : struct_avail;
	size_t to_write = (len < avail) ? len : avail;

	void *value = weave_observable_claim(obs, K_FOREVER);
	if (!value) {
		return -EAGAIN;
	}

	memcpy((uint8_t *)value + offset, buf, to_write);
	weave_observable_publish(obs);
	return to_write;
}

/* ============================ Field API ============================ */

int settings_field_get(struct weave_observable *obs, const struct setting_field *field, void *buf)
{
	if (!obs || !field || !buf) {
		return -EINVAL;
	}

	if (!(field->flags & SETTING_FLAG_R)) {
		return -EACCES;
	}

	void *value = weave_observable_claim(obs, K_FOREVER);
	if (!value) {
		return -EAGAIN;
	}

	uint8_t size = setting_type_size(field->type);
	memcpy(buf, (uint8_t *)value + field->offset, size);
	weave_observable_finish(obs);

	return size;
}

int settings_field_set(struct weave_observable *obs, const struct setting_field *field,
		       const void *buf)
{
	if (!obs || !field || !buf) {
		return -EINVAL;
	}

	if (!(field->flags & SETTING_FLAG_W)) {
		return -EACCES;
	}

	void *value = weave_observable_claim(obs, K_FOREVER);
	if (!value) {
		return -EAGAIN;
	}

	/* If validator exists, create temp copy with new field value and validate */
	if (obs->validator) {
		uint8_t temp[obs->size];
		memcpy(temp, value, obs->size);
		memcpy(temp + field->offset, buf, setting_type_size(field->type));

		int ret = weave_observable_validate(obs, temp);
		if (ret < 0) {
			weave_observable_finish(obs);
			return ret;
		}
	}

	memcpy((uint8_t *)value + field->offset, buf, setting_type_size(field->type));
	return weave_observable_publish(obs);
}
