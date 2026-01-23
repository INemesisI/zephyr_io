/*
 * Copyright (c) 2026 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <weave/observable.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(weave_observable, CONFIG_WEAVE_LOG_LEVEL);

int weave_observable_set_unchecked(struct weave_observable *obs, const void *value)
{
	if (!obs || !value || !obs->value) {
		return -EINVAL;
	}

	void *user_data = obs->owner_sink.user_data;

	/* 1. Validate (if defined) */
	if (obs->validator) {
		int ret = obs->validator(obs, value, user_data);
		if (ret < 0) {
			LOG_DBG("Validation failed: %d", ret);
			return ret;
		}
	}

	/* 2. Copy value */
	k_spinlock_key_t key = k_spin_lock(&obs->lock);
	memcpy(obs->value, value, obs->size);
	k_spin_unlock(&obs->lock, key);

	/* 3. Call owner handler (if defined) - via sink mechanism */
	if (obs->owner_sink.handler) {
		weave_sink_send(&obs->owner_sink, obs, K_NO_WAIT);
	}

	/* 4. Notify external observers */
	int notified = weave_source_emit(&obs->source, obs, K_NO_WAIT);

	LOG_DBG("Set: size=%zu, notified=%d observers", obs->size, notified);
	return notified;
}

int weave_observable_get_unchecked(struct weave_observable *obs, void *value)
{
	if (!obs || !value || !obs->value) {
		return -EINVAL;
	}

	k_spinlock_key_t key = k_spin_lock(&obs->lock);
	memcpy(value, obs->value, obs->size);
	k_spin_unlock(&obs->lock, key);

	LOG_DBG("Get: size=%zu", obs->size);
	return 0;
}
