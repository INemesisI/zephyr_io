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

/* Internal helper to add a connection to the source's list */
static int flow_connection_add_internal(struct flow_connection *conn)
{
	struct flow_connection *existing;
	k_spinlock_key_t key;

	if (!conn || !conn->source || !conn->sink) {
		return -EINVAL;
	}

	key = k_spin_lock(&conn->source->lock);

	/* Check if this connection is already in the list or is a duplicate */
	SYS_SLIST_FOR_EACH_CONTAINER(&conn->source->connections, existing, node) {
		if (existing == conn) {
			k_spin_unlock(&conn->source->lock, key);
			return -EBUSY;
		}
		if (existing->sink == conn->sink) {
			k_spin_unlock(&conn->source->lock, key);
			return -EALREADY;
		}
	}

	sys_slist_append(&conn->source->connections, &conn->node);
	k_spin_unlock(&conn->source->lock, key);

	LOG_DBG("Connected source %p to sink %p", conn->source, conn->sink);
	return 0;
}

/* Internal helper to remove a connection from the source's list */
static int flow_connection_remove_internal(struct flow_connection *conn)
{
	k_spinlock_key_t key;

	if (!conn || !conn->source) {
		return -EINVAL;
	}

	key = k_spin_lock(&conn->source->lock);

	if (!sys_slist_find_and_remove(&conn->source->connections, &conn->node)) {
		k_spin_unlock(&conn->source->lock, key);
		return -ENOENT;
	}

	k_spin_unlock(&conn->source->lock, key);

	LOG_DBG("Disconnected source %p from sink %p", conn->source, conn->sink);
	return 0;
}

int flow_runtime_connect(struct flow_source *source, struct flow_sink *sink)
{
	struct flow_runtime_connection *entry = NULL;
	int ret;

	if (!source || !sink) {
		return -EINVAL;
	}

	k_mutex_lock(&pool_mutex, K_FOREVER);

	/* Check if already connected */
	ARRAY_FOR_EACH_PTR(runtime_pool, conn_entry) {
		if (conn_entry->in_use && conn_entry->conn.source == source &&
		    conn_entry->conn.sink == sink) {
			k_mutex_unlock(&pool_mutex);
			return -EALREADY;
		}
	}

	/* Find free slot */
	ARRAY_FOR_EACH_PTR(runtime_pool, conn_entry) {
		if (!conn_entry->in_use) {
			entry = conn_entry;
			entry->in_use = true;
			entry->conn.source = source;
			entry->conn.sink = sink;
			break;
		}
	}

	k_mutex_unlock(&pool_mutex);

	if (!entry) {
		LOG_WRN("Runtime connection pool exhausted (size=%d)",
			CONFIG_FLOW_RUNTIME_CONNECTION_POOL_SIZE);
		return -ENOMEM;
	}

	/* Add using internal function */
	ret = flow_connection_add_internal(&entry->conn);
	if (ret < 0) {
		k_mutex_lock(&pool_mutex, K_FOREVER);
		entry->in_use = false;
		k_mutex_unlock(&pool_mutex);
		return ret;
	}

	return 0;
}

int flow_runtime_disconnect(struct flow_source *source, struct flow_sink *sink)
{
	struct flow_runtime_connection *entry = NULL;
	int ret;

	if (!source || !sink) {
		return -EINVAL;
	}

	k_mutex_lock(&pool_mutex, K_FOREVER);

	/* Find the connection */
	ARRAY_FOR_EACH_PTR(runtime_pool, conn_entry) {
		if (conn_entry->in_use && conn_entry->conn.source == source &&
		    conn_entry->conn.sink == sink) {
			entry = conn_entry;
			break;
		}
	}

	k_mutex_unlock(&pool_mutex);

	if (!entry) {
		return -ENOENT;
	}

	ret = flow_connection_remove_internal(&entry->conn);
	if (ret == 0) {
		k_mutex_lock(&pool_mutex, K_FOREVER);
		entry->in_use = false;
		k_mutex_unlock(&pool_mutex);
	}

	return ret;
}