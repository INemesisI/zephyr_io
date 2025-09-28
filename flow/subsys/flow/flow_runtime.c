/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr_io/flow/flow.h>
#include <zephyr/logging/log.h>
#include <errno.h>

LOG_MODULE_DECLARE(flow, CONFIG_FLOW_LOG_LEVEL);

int flow_connection_add(struct flow_connection *conn)
{
	struct flow_connection *existing;
	k_spinlock_key_t key;

	if (!conn || !conn->source || !conn->sink) {
		return -EINVAL;
	}

#ifdef CONFIG_FLOW_RUNTIME_STACK_CHECK
	/* Try to detect if connection might be stack-allocated.
	 * This is a heuristic check - compare pointer against current thread's stack bounds.
	 * Note: This won't catch all cases but can help during development.
	 */
	struct k_thread *current = k_current_get();
	if (current && current->stack_info.start) {
		uintptr_t stack_start = (uintptr_t)current->stack_info.start;
		uintptr_t stack_end = stack_start + current->stack_info.size;
		uintptr_t conn_addr = (uintptr_t)conn;

		if (conn_addr >= stack_start && conn_addr < stack_end) {
			LOG_ERR("Connection at %p appears to be stack-allocated! "
				"This is unsafe if connection outlives the function. "
				"Use static or dynamic allocation instead.",
				conn);
/* Allow in tests for testing purposes */
#ifndef CONFIG_ZTEST
			return -EINVAL;
#endif
		}
	}
#endif

	key = k_spin_lock(&conn->source->lock);

	/* Check if this connection is already in the list or is a duplicate */
	SYS_SLIST_FOR_EACH_CONTAINER(&conn->source->sinks, existing, node) {
		if (existing == conn) {
			k_spin_unlock(&conn->source->lock, key);
			return -EBUSY;
		}
		if (existing->sink == conn->sink) {
			k_spin_unlock(&conn->source->lock, key);
			return -EALREADY;
		}
	}

	sys_slist_append(&conn->source->sinks, &conn->node);
	k_spin_unlock(&conn->source->lock, key);

	LOG_DBG("Connected source %p to sink %p", conn->source, conn->sink);
	return 0;
}

int flow_connection_remove(struct flow_connection *conn)
{
	k_spinlock_key_t key;

	if (!conn || !conn->source) {
		return -EINVAL;
	}

	key = k_spin_lock(&conn->source->lock);

	if (!sys_slist_find_and_remove(&conn->source->sinks, &conn->node)) {
		k_spin_unlock(&conn->source->lock, key);
		return -ENOENT;
	}

	k_spin_unlock(&conn->source->lock, key);

	LOG_DBG("Disconnected source %p from sink %p", conn->source, conn->sink);
	return 0;
}