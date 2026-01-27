/*
 * Copyright (c) 2026 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Memslab Core Sample - Custom datatypes through weave core
 *
 * Demonstrates sending custom fixed-size data structures through weave
 * using k_mem_slab for allocation and transfer semantics (single-sink).
 *
 * Key pattern: Per-source ops capture the slab pointer via macro,
 * enabling clean free without embedding metadata in the block.
 */

#include <weave/core.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(memslab_sample, LOG_LEVEL_INF);

/* ========================== Custom Data Type ========================== */

/**
 * @brief Sensor reading message
 *
 * Fixed-size structure allocated from memslab.
 * No weave metadata needed - just your data.
 */
struct sensor_msg {
	int16_t temperature; /* 0.01°C units */
	uint16_t humidity;   /* 0.01% units */
	uint32_t sequence;
	uint32_t timestamp;
};

/* ========================== Memslab Pool ========================== */

#define MSG_COUNT 2

K_MEM_SLAB_DEFINE_STATIC(sensor_slab, sizeof(struct sensor_msg), MSG_COUNT, 4);

/* ========================== Slab Source Macro ========================== */

/**
 * @brief Define a memslab source with transfer semantics
 *
 * Creates a weave source with per-source ops that know how to free
 * blocks back to the specified slab. Single-sink only (no ref counting).
 *
 * @param _name Source variable name
 * @param _slab k_mem_slab to free blocks to
 */
#define SLAB_SOURCE_DEFINE(_name, _slab)                                                           \
	static void _name##_unref(void *ptr)                                                       \
	{                                                                                          \
		k_mem_slab_free(&(_slab), ptr);                                                    \
	}                                                                                          \
	static const struct weave_payload_ops _name##_ops = {                                      \
		.ref = NULL,                                                                       \
		.unref = _name##_unref,                                                            \
	};                                                                                         \
	WEAVE_SOURCE_DEFINE(_name, &_name##_ops)

/* ========================== Source & Sink ========================== */

/* Source: sensor data emitter */
SLAB_SOURCE_DEFINE(sensor_source, sensor_slab);

/* Handler: process sensor data */
static void sensor_handler(void *ptr, void *user_data)
{
	struct sensor_msg *msg = (struct sensor_msg *)ptr;

	LOG_INF("Received: seq=%u temp=%d.%02d°C humidity=%u.%02u%%", msg->sequence,
		msg->temperature / 100, msg->temperature % 100, msg->humidity / 100,
		msg->humidity % 100);

	/* Note: No need to free - weave calls unref after handler returns */
}

/* Sink: immediate mode (runs in sender context) */
WEAVE_SINK_DEFINE(sensor_sink, sensor_handler, WV_IMMEDIATE, NULL);

/* Wire source to sink at compile time */
WEAVE_CONNECT(&sensor_source, &sensor_sink);

/* ========================== Allocation Helper ========================== */

static struct sensor_msg *sensor_msg_alloc(k_timeout_t timeout)
{
	void *block;

	if (k_mem_slab_alloc(&sensor_slab, &block, timeout) != 0) {
		return NULL;
	}
	return (struct sensor_msg *)block;
}

/* ========================== Demo ========================== */

static void simulate_sensor_readings(void)
{
	static uint32_t seq;

	for (int i = 0; i < 5; i++) {
		struct sensor_msg *msg = sensor_msg_alloc(K_MSEC(100));

		if (!msg) {
			LOG_ERR("Failed to allocate message");
			continue;
		}

		/* Fill in sensor data */
		msg->temperature = 2350 + (i * 10); /* 23.50°C + variation */
		msg->humidity = 4500 + (i * 100);   /* 45.00% + variation */
		msg->sequence = seq++;
		msg->timestamp = k_uptime_get_32();

		/* Send through weave - ownership transfers to sink */
		int ret = weave_source_emit(&sensor_source, msg, K_NO_WAIT);

		if (ret < 0) {
			LOG_ERR("Failed to emit: %d", ret);
			/* On failure, we still own it - free manually */
			k_mem_slab_free(&sensor_slab, msg);
		}

		k_msleep(100);
	}
}

int main(void)
{
	LOG_INF("Memslab Core Sample - Custom datatypes through weave");
	LOG_INF("Pool: %u blocks of %zu bytes", MSG_COUNT, sizeof(struct sensor_msg));

	simulate_sensor_readings();

	LOG_INF("Sample completed successfully");
	return 0;
}
