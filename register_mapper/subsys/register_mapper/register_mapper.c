/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr_io/register_mapper/register_mapper.h>
#include <zephyr_io/register_mapper/register_types.h>
#include <zephyr_io/register_mapper/register_channel.h>
#include <zephyr/init.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(register_mapper, CONFIG_REGISTER_MAPPER_LOG_LEVEL);

/* Binary search implementation for sorted register mappings
 * The linker automatically sorts the section by name, so we get O(log n) lookup
 */
static const struct reg_mapping *_find_register(uint16_t addr)
{
	/* Get the section boundaries */
	extern struct reg_mapping _reg_mapping_list_start[];
	extern struct reg_mapping _reg_mapping_list_end[];
	const struct reg_mapping *start = _reg_mapping_list_start;
	const struct reg_mapping *end = _reg_mapping_list_end;

	if (start >= end) {
		return NULL;
	}

	/* Binary search on pre-sorted section */
	while (start < end) {
		const struct reg_mapping *mid = start + (end - start) / 2;

		if (mid->address == addr) {
			return mid;
		}

		if (mid->address < addr) {
			start = mid + 1;
		} else {
			end = mid;
		}
	}

	return NULL;
}

/* Runtime type size lookup */
size_t reg_type_size(enum reg_type type)
{
	static const uint8_t size_lut[REG_TYPE_COUNT] = {
		[REG_TYPE_U8] = 1, [REG_TYPE_U16] = 2, [REG_TYPE_U32] = 4, [REG_TYPE_U64] = 8,
		[REG_TYPE_I8] = 1, [REG_TYPE_I16] = 2, [REG_TYPE_I32] = 4, [REG_TYPE_I64] = 8,
	};

	if (type < REG_TYPE_COUNT) {
		return size_lut[type];
	}
	return 0;
}

/* Core register read implementation */
int reg_read_value(uint16_t addr, struct reg_value *value)
{
	int ret;

	/* Validate input */
	if (!value) {
		return -EINVAL;
	}

	const struct reg_mapping *map = _find_register(addr);
	if (!map) {
		LOG_WRN("Register 0x%04x not found", addr);
		return -ENOENT;
	}

	if (!map->flags.readable) {
		LOG_WRN("Register 0x%04x is write-only", addr);
		return -EACCES;
	}

	/* Validate channel */
	__ASSERT(map->channel != NULL, "Invalid channel in mapping");

	/* Claim channel for direct access - reading shouldn't block */
	ret = zbus_chan_claim(map->channel, K_NO_WAIT);
	if (ret != 0) {
		LOG_ERR("Failed to claim channel for read: %d", ret);
		return ret;
	}

	/* Direct read from message buffer */
	const uint8_t *msg = zbus_chan_const_msg(map->channel);
	size_t size = reg_type_size(map->type);

	__ASSERT(size > 0, "Invalid register type");
	__ASSERT(map->offset + size <= map->channel->message_size,
		 "Register offset exceeds channel message size");

	/* Set type and copy value */
	value->type = map->type;
	memcpy(&value->val, msg + map->offset, size);

	/* Release channel */
	zbus_chan_finish(map->channel);

	LOG_DBG("Read 0x%04x: type=%d size=%zu", addr, map->type, size);
	return 0;
}

/* Common write implementation - used by both reg_write_value and reg_block_write_register */
static int _reg_write_common(uint16_t addr, struct reg_value value, bool notify,
			     k_timeout_t timeout)
{
	const struct reg_mapping *map = _find_register(addr);
	if (!map) {
		LOG_WRN("Register 0x%04x not found", addr);
		return -EINVAL;
	}

	/* Check type compatibility */
	if (map->type != value.type) {
		LOG_WRN("Type mismatch for 0x%04x: expected %d, got %d", addr, map->type,
			value.type);
		return -EINVAL;
	}

	if (!map->flags.writable) {
		LOG_WRN("Register 0x%04x is read-only", addr);
		return -EACCES;
	}

	/* Claim channel and write */
	int ret = zbus_chan_claim(map->channel, K_NO_WAIT);
	if (ret != 0) {
		LOG_ERR("Failed to claim channel for write: %d", ret);
		return ret;
	}

	/* Modify in-place based on type */
	uint8_t *msg = zbus_chan_msg(map->channel);
	size_t size = reg_type_size(value.type);
	memcpy(msg + map->offset, &value.val, size);

	/* Release channel */
	zbus_chan_finish(map->channel);

	/* Handle notification */
	struct channel_state *state = (struct channel_state *)map->channel->user_data;
	if (notify) {
		/* Clear any pending flag and notify immediately */
		if (state) {
			state->update_pending = false;
		}

		/* Send notification */
		ret = zbus_chan_notify(map->channel, timeout);
		if (ret != 0) {
			LOG_WRN("Failed to notify channel: %d", ret);
			return ret;
		}
	} else {
		/* Mark this channel as having pending updates */
		if (state) {
			state->update_pending = true;
		}
	}

	LOG_DBG("%s 0x%04x: type=%d size=%zu", notify ? "Wrote" : "Block write", addr, value.type,
		size);
	return 0;
}

/* Core register write implementation with immediate notification */
int reg_write_value(uint16_t addr, struct reg_value value, k_timeout_t timeout)
{
	return _reg_write_common(addr, value, true, timeout);
}

#ifdef CONFIG_REGISTER_MAPPER_BLOCK_WRITE

/* Semaphore for block write operations - binary semaphore (max count 1) */
static K_SEM_DEFINE(block_sem, 1, 1);

/* Begin a block write transaction */
int reg_block_write_begin(k_timeout_t timeout)
{
	int ret = k_sem_take(&block_sem, timeout);
	if (ret != 0) {
		LOG_WRN("Failed to begin block write: %d", ret);
		return ret;
	}
	LOG_DBG("Block write transaction started");
	return 0;
}

/* Write a register within a block transaction (no notification) */
int reg_block_write_register(uint16_t addr, struct reg_value value)
{
	return _reg_write_common(addr, value, false, K_NO_WAIT);
}

/* Commit pending notifications */
int reg_block_write_commit(k_timeout_t timeout)
{
	int ret = 0;
	int notify_count = 0;

	/* Release semaphore before notifications (in case handlers do more writes) */
	k_sem_give(&block_sem);

	/* Iterate through all channels via register mappings */
	const struct zbus_channel *last_channel = NULL;

	STRUCT_SECTION_FOREACH(reg_mapping, map) {
		/* Skip if we've already processed this channel */
		if (map->channel == last_channel) {
			continue;
		}

		/* Check if this channel has pending updates */
		struct channel_state *state = (struct channel_state *)map->channel->user_data;
		if (state && state->update_pending) {
			/* Clear the pending flag */
			state->update_pending = false;

			/* Notify the channel */
			int notify_ret = zbus_chan_notify(map->channel, timeout);
			if (notify_ret != 0) {
				LOG_WRN("Failed to notify channel %p: %d", map->channel,
					notify_ret);
				if (ret == 0) {
					ret = notify_ret; /* Record first error */
				}
			} else {
				notify_count++;
			}
		}

		last_channel = map->channel;
	}

	LOG_DBG("Block write committed: %d channels notified", notify_count);
	return ret;
}

#endif /* CONFIG_REGISTER_MAPPER_BLOCK_WRITE */

#ifdef CONFIG_REGISTER_MAPPER_VALIDATION

/* Validate that no registers overlap - internal function also used by tests */
int _reg_validate_no_overlaps(void)
{
	int overlap_count = 0;

	STRUCT_SECTION_FOREACH(reg_mapping, map1) {
		size_t size1 = reg_type_size(map1->type);
		uint16_t end1 = map1->address + size1 - 1;

		STRUCT_SECTION_FOREACH(reg_mapping, map2) {
			/* Don't compare with self */
			if (map1 == map2) {
				continue;
			}

			/* Only check each pair once (map1 < map2) */
			if (map1 >= map2) {
				continue;
			}

			size_t size2 = reg_type_size(map2->type);
			uint16_t end2 = map2->address + size2 - 1;

			/* Check for overlap: start1 <= end2 && start2 <= end1 */
			if (map1->address <= end2 && map2->address <= end1) {
				LOG_ERR("Register overlap detected:");
				LOG_ERR("  0x%04x-0x%04x (size %zu)", map1->address, end1, size1);
				LOG_ERR("  0x%04x-0x%04x (size %zu)", map2->address, end2, size2);
				overlap_count++;
			}
		}
	}

	if (overlap_count > 0) {
		LOG_ERR("Found %d overlapping register pairs", overlap_count);
		return -EINVAL;
	}

	LOG_INF("Register validation passed - no overlaps detected");
	return 0;
}

/* Removed reg_list_all() - moved to application code */

#endif /* CONFIG_REGISTER_MAPPER_VALIDATION */

/* Iterate over all register mappings with a callback */
int reg_foreach(reg_foreach_cb_t cb, void *user_data)
{
	int count = 0;
	int ret;

	/* Validate callback */
	if (!cb) {
		LOG_ERR("Invalid callback function");
		return -EINVAL;
	}

	/* Iterate through all register mappings */
	STRUCT_SECTION_FOREACH(reg_mapping, map) {
		/* Call the user callback */
		ret = cb(map, user_data);
		count++;

		/* Stop iteration if callback returns non-zero */
		if (ret != 0) {
			LOG_DBG("Iteration stopped by callback at register 0x%04x (returned %d)",
				map->address, ret);
			break;
		}
	}

	LOG_DBG("Processed %d register mappings", count);
	return count;
}

#ifdef CONFIG_REGISTER_MAPPER_VALIDATION
/* Initialize register system and validate configuration */
static int register_mapper_init(void)
{
	/* Validate no overlapping registers */
	if (_reg_validate_no_overlaps() != 0) {
		LOG_ERR("Register mapper initialization failed - overlapping registers!");
		return -EINVAL;
	}

	/* Removed automatic listing - now done in application code */

	LOG_INF("Register mapper initialized successfully");
	return 0;
}

SYS_INIT(register_mapper_init, APPLICATION, CONFIG_REGISTER_MAPPER_INIT_PRIORITY);
#endif /* CONFIG_REGISTER_MAPPER_VALIDATION */