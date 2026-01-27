/*
 * Copyright (c) 2026 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <weave/observable.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(weave_observable, CONFIG_WEAVE_LOG_LEVEL);

/* ============================ Claim/Finish/Publish API ============================ */

void *weave_observable_claim(struct weave_observable *obs, k_timeout_t timeout)
{
	if (!obs) {
		return NULL;
	}

	if (k_sem_take(&obs->sem, timeout) != 0) {
		return NULL;
	}

	return obs->value;
}

void weave_observable_finish(struct weave_observable *obs)
{
	if (obs) {
		k_sem_give(&obs->sem);
	}
}

int weave_observable_publish(struct weave_observable *obs)
{
	if (!obs) {
		return -EINVAL;
	}

	/* Prevent recursive publish (handler trying to set same observable) */
	if (obs->publishing) {
		LOG_WRN("Recursive publish rejected");
		return -EBUSY;
	}

	obs->publishing = true;
	k_sem_give(&obs->sem);

	/* Notify owner handler (if defined) */
	if (obs->owner_sink.handler) {
		weave_sink_send(&obs->owner_sink, obs, NULL, K_NO_WAIT);
	}

	/* Notify external observers */
	int notified = weave_source_emit(&obs->source, obs, K_NO_WAIT);

	obs->publishing = false;

	LOG_DBG("Published: notified=%d observers", notified);
	return notified;
}

int weave_observable_validate(struct weave_observable *obs, const void *value)
{
	if (!obs) {
		return -EINVAL;
	}

	if (!obs->validator) {
		return 0; /* No validator = always valid */
	}

	return obs->validator(obs, value, obs->owner_sink.user_data);
}

/* ============================ Set/Get API ============================ */

int weave_observable_set_unchecked(struct weave_observable *obs, const void *value)
{
	if (!obs || !value || !obs->value) {
		return -EINVAL;
	}

	/* Check if publish is in progress (recursion guard) */
	if (obs->publishing) {
		return -EBUSY;
	}

	/* Validate before claiming */
	int ret = weave_observable_validate(obs, value);
	if (ret < 0) {
		LOG_DBG("Validation failed: %d", ret);
		return ret;
	}

	void *ptr = weave_observable_claim(obs, K_FOREVER);
	if (!ptr) {
		return -EAGAIN;
	}

	memcpy(ptr, value, obs->size);
	return weave_observable_publish(obs);
}

int weave_observable_get_unchecked(struct weave_observable *obs, void *value)
{
	if (!obs || !value || !obs->value) {
		return -EINVAL;
	}

	void *ptr = weave_observable_claim(obs, K_FOREVER);
	if (!ptr) {
		return -EAGAIN;
	}

	memcpy(value, ptr, obs->size);
	weave_observable_finish(obs);

	LOG_DBG("Get: size=%zu", obs->size);
	return 0;
}
