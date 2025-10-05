/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr_io/flow/flow.h>

LOG_MODULE_DECLARE(flow, CONFIG_FLOW_LOG_LEVEL);

/* Runtime connection pool entry */
struct flow_runtime_connection {
	struct flow_connection conn;
	bool in_use;
};

/* Static pool of runtime connections */
static struct flow_runtime_connection runtime_pool[CONFIG_FLOW_RUNTIME_CONNECTION_POOL_SIZE];
static struct k_mutex pool_mutex = Z_MUTEX_INITIALIZER(pool_mutex);

int _find_connection(struct flow_source *source, struct flow_sink *sink,
		     struct flow_runtime_connection **out_conn)
{
	if (!source || !sink) {
		return -EINVAL;
	}

	/* Find the connection */
	ARRAY_FOR_EACH_PTR(runtime_pool, conn_entry) {
		if (conn_entry->in_use && conn_entry->conn.source == source &&
		    conn_entry->conn.sink == sink) {
			if (out_conn) {
				*out_conn = conn_entry;
			}
			return 0;
		}
	}

	return -ENOENT;
}

int flow_runtime_connect(struct flow_source *source, struct flow_sink *sink)
{
	k_spinlock_key_t key;
	int ret = -ENOENT;

	if (!source || !sink) {
		return -EINVAL;
	}

	k_mutex_lock(&pool_mutex, K_FOREVER);

	ret = _find_connection(source, sink, NULL);
	if (ret == 0) {
		k_mutex_unlock(&pool_mutex);
		return -EALREADY;
	}

	/* Find free slot */
	ARRAY_FOR_EACH_PTR(runtime_pool, entry) {
		if (entry->in_use) {
			continue;
		}

		entry->in_use = true;
		entry->conn.source = source;
		entry->conn.sink = sink;
		/* Add using internal function */
		key = k_spin_lock(&entry->conn.source->lock);
		sys_slist_append(&entry->conn.source->connections, &entry->conn.node);
		k_spin_unlock(&entry->conn.source->lock, key);
		ret = 0;
		break;
	}

	if (ret != 0) {
		LOG_WRN("Runtime connection pool exhausted (size=%d)",
			CONFIG_FLOW_RUNTIME_CONNECTION_POOL_SIZE);
		ret = -ENOMEM;
	}

	k_mutex_unlock(&pool_mutex);

	return ret;
}

int flow_runtime_disconnect(struct flow_source *source, struct flow_sink *sink)
{
	struct flow_runtime_connection *entry = NULL;
	k_spinlock_key_t key;
	int ret;

	if (!source || !sink) {
		return -EINVAL;
	}

	k_mutex_lock(&pool_mutex, K_FOREVER);

	ret = _find_connection(source, sink, &entry);
	if (ret < 0 || !entry) {
		k_mutex_unlock(&pool_mutex);
		return -ENOENT;
	}

	key = k_spin_lock(&entry->conn.source->lock);
	if (!sys_slist_find_and_remove(&entry->conn.source->connections, &entry->conn.node)) {
		ret = -ENOENT;
		/* no return here to ensure cleanup */
	}
	k_spin_unlock(&entry->conn.source->lock, key);

	if (ret == 0) {
		entry->in_use = false;
		entry->conn.source = NULL;
		entry->conn.sink = NULL;
	}
	k_mutex_unlock(&pool_mutex);

	return ret;
}
