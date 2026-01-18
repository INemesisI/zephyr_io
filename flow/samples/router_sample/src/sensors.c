/*
 * Packet I/O Sample - Sensor Module Implementation
 *
 * Self-contained sensor simulation with own buffer pool and thread
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/buf.h>
#include <zephyr_io/flow/flow.h>

#include "sensors.h"

LOG_MODULE_REGISTER(sensors, LOG_LEVEL_INF);

/* Buffer pool for sensor packets */
FLOW_BUF_POOL_DEFINE(sensor_pool, 8, 512, NULL);

/* Define packet sources */
FLOW_SOURCE_DEFINE(sensor1_source);
FLOW_SOURCE_DEFINE(sensor2_source);

/* Sensor data patterns - simulating larger sensor payloads */
static uint8_t sensor1_data[256]; /* 256 byte payload */
static uint8_t sensor2_data[384]; /* 384 byte payload */

/* Sampling control - using semaphore for thread-safe start/stop */
K_SEM_DEFINE(sampling_sem, 0, 1); /* Start with sampling DISABLED */

static void init_sensor_data(void)
{
	/* Sensor 1: Repeating pattern 0xA1 throughout */
	for (int i = 0; i < sizeof(sensor1_data); i++) {
		sensor1_data[i] = 0xA1;
	}

	/* Sensor 2: Repeating pattern 0xB2 throughout */
	for (int i = 0; i < sizeof(sensor2_data); i++) {
		sensor2_data[i] = 0xB2;
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

#ifdef CONFIG_ROUTER_AUTO_START_SAMPLING
	/* Automatically start sampling if configured */
	sensor_start_sampling();
	LOG_INF("Sampling started automatically");
#endif

	while (1) {
		/* Check if sampling is enabled (non-blocking) */
		if (k_sem_count_get(&sampling_sem) > 0) {
			/* Sensor 1 packet - 256 bytes */
			buf = flow_buf_alloc_with_id(&sensor_pool, SOURCE_ID_SENSOR1, K_NO_WAIT);
			if (buf) {
				net_buf_add_mem(buf, sensor1_data, sizeof(sensor1_data));
				ret = flow_source_send(&sensor1_source, buf, K_NO_WAIT);
				LOG_DBG("Sensor 1 sent %d bytes to %d sinks", sizeof(sensor1_data),
					ret);
			}

			/* Sensor 2 packet - 384 bytes */
			buf = flow_buf_alloc_with_id(&sensor_pool, SOURCE_ID_SENSOR2, K_NO_WAIT);
			if (buf) {
				net_buf_add_mem(buf, sensor2_data, sizeof(sensor2_data));
				ret = flow_source_send(&sensor2_source, buf, K_NO_WAIT);
				LOG_DBG("Sensor 2 sent %d bytes to %d sinks", sizeof(sensor2_data),
					ret);
			}

			/* Sleep between packet generations */
			k_sleep(K_MSEC(100)); /* Sample every 100ms for faster testing */
		} else {
			/* When sampling is disabled, sleep until enabled */
			k_sleep(K_MSEC(100));
		}
	}
}

/* Control functions */
void sensor_start_sampling(void)
{
	k_sem_give(&sampling_sem);
}

void sensor_stop_sampling(void)
{
	k_sem_reset(&sampling_sem);
}

/* Static thread initialization - starts automatically */
K_THREAD_DEFINE(sensor_thread, 1024, sensor_thread_fn, NULL, NULL, NULL, 7, 0, 0);