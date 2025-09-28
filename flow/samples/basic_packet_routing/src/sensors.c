/*
 * Packet I/O Sample - Sensor Module Implementation
 *
 * Self-contained sensor simulation with own buffer pool and thread
 */

#include <zephyr/kernel.h>
#include <zephyr/net/buf.h>
#include <zephyr_io/flow/flow.h>
#include <zephyr/logging/log.h>

#include "sensors.h"
#include "packet_defs.h"

LOG_MODULE_REGISTER(sensors, LOG_LEVEL_INF);

/* Sensor module's own buffer pool - larger for bigger packets */
NET_BUF_POOL_DEFINE(sensor_pool, 8, 512, 4, NULL);

/* Define packet sources */
FLOW_SOURCE_DEFINE(sensor1_source);
FLOW_SOURCE_DEFINE(sensor2_source);

/* Sensor data patterns - simulating larger sensor payloads */
static uint8_t sensor1_data[256]; /* 256 byte payload */
static uint8_t sensor2_data[384]; /* 384 byte payload */

static void init_sensor_data(void)
{
	/* Initialize sensor 1 with pattern starting with 0xA0 */
	sensor1_data[0] = 0xA0;
	for (int i = 1; i < sizeof(sensor1_data); i++) {
		sensor1_data[i] = (uint8_t)(i & 0xFF);
	}

	/* Initialize sensor 2 with pattern starting with 0xB0 */
	sensor2_data[0] = 0xB0;
	for (int i = 1; i < sizeof(sensor2_data); i++) {
		sensor2_data[i] = (uint8_t)((i * 2) & 0xFF);
	}
}

static void sensor_thread_fn(void *p1, void *p2, void *p3)
{
	struct net_buf *buf;
	int ret;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	/* Initialize sensor data patterns */
	init_sensor_data();

	LOG_INF("Sensor module started (256B + 384B payloads)");

	while (1) {
		/* Sensor 1 packet - 256 bytes */
		buf = net_buf_alloc(&sensor_pool, K_NO_WAIT);
		if (buf) {
			net_buf_add_mem(buf, sensor1_data, sizeof(sensor1_data));
			ret = flow_source_send(&sensor1_source, buf, K_NO_WAIT);
			LOG_DBG("Sensor 1 sent %d bytes to %d sinks", sizeof(sensor1_data), ret);
			net_buf_unref(buf);
		}

		k_sleep(K_MSEC(200)); /* Faster data rate */

		/* Sensor 2 packet - 384 bytes */
		buf = net_buf_alloc(&sensor_pool, K_NO_WAIT);
		if (buf) {
			net_buf_add_mem(buf, sensor2_data, sizeof(sensor2_data));
			ret = flow_source_send_consume(&sensor2_source, buf, K_NO_WAIT);
			LOG_DBG("Sensor 2 sent %d bytes to %d sinks", sizeof(sensor2_data), ret);
		}

		k_sleep(K_MSEC(200)); /* Faster data rate */
	}
}

/* Static thread initialization - starts automatically */
K_THREAD_DEFINE(sensor_thread, 1024, sensor_thread_fn, NULL, NULL, NULL, 7, 0, 0);