/*
 * Temperature Sensor Module Implementation
 */

#include "temperature_sensor.h"
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr_io/swift_io/swift_io.h>
#include <zephyr/random/random.h>

LOG_MODULE_REGISTER(temp_sensor, LOG_LEVEL_INF);

/* ========================================================================== */
/* MODULE CONFIGURATION */
/* ========================================================================== */

#define TEMP_SENSOR_THREAD_STACK_SIZE  1024
#define TEMP_SENSOR_THREAD_PRIORITY    10
#define TEMP_SENSOR_SAMPLE_INTERVAL_MS 200    /* 200ms for quick testing */
#define TEMP_SENSOR_ID                 0x0001 /* Unique sensor ID */

/* Simulated sensor ranges */
#define TEMP_MIN_C      (-1000) /* -10.00°C */
#define TEMP_MAX_C      (5000)  /* 50.00°C */
#define HUMIDITY_MIN_RH (2000)  /* 20.00% RH */
#define HUMIDITY_MAX_RH (9500)  /* 95.00% RH */

/* ========================================================================== */
/* MODULE STATE */
/* ========================================================================== */

/* Buffer pool for temperature sensor packets */
NET_BUF_POOL_DEFINE(temp_sensor_pool, 8, sizeof(struct temp_sensor_data), 4, NULL);

/* Packet source for temperature data */
SWIFT_IO_SOURCE_DEFINE(temperature_sensor_source);

/* Current sensor readings */
static int16_t current_temp_c = 2500;      /* 25.00°C */
static int16_t current_humidity_rh = 5000; /* 50.00% RH */
static uint16_t sample_counter = 0;

/* ========================================================================== */
/* SENSOR SIMULATION */
/* ========================================================================== */

/**
 * @brief Simulate temperature sensor reading
 *
 * Generates realistic temperature data with some randomness
 * and slow drift to simulate real sensor behavior.
 */
static void simulate_sensor_reading(int16_t *temp_c, int16_t *humidity_rh)
{
	/* Add random variation */
	int16_t temp_delta = (sys_rand32_get() % 200) - 100;     /* ±1.00°C */
	int16_t humidity_delta = (sys_rand32_get() % 400) - 200; /* ±2.00% RH */

	/* Apply delta with clamping */
	current_temp_c += temp_delta;
	current_humidity_rh += humidity_delta;

	/* Clamp to valid ranges */
	current_temp_c = CLAMP(current_temp_c, TEMP_MIN_C, TEMP_MAX_C);
	current_humidity_rh = CLAMP(current_humidity_rh, HUMIDITY_MIN_RH, HUMIDITY_MAX_RH);

	*temp_c = current_temp_c;
	*humidity_rh = current_humidity_rh;
}

/* ========================================================================== */
/* PACKET GENERATION */
/* ========================================================================== */

/**
 * @brief Create and send temperature sensor packet
 */
static void send_temperature_packet(void)
{
	struct net_buf *buf;
	struct temp_sensor_data *data;
	int16_t temp_c, humidity_rh;

	/* Get sensor reading */
	simulate_sensor_reading(&temp_c, &humidity_rh);

	/* Allocate buffer for sensor data */
	buf = net_buf_alloc(&temp_sensor_pool, K_NO_WAIT);
	if (!buf) {
		LOG_WRN("No buffer available for temperature data");
		return;
	}

	/* Fill sensor data payload */
	data = (struct temp_sensor_data *)net_buf_add(buf, sizeof(*data));
	data->temperature_c = temp_c;
	data->humidity_rh = humidity_rh;
	data->sensor_id = TEMP_SENSOR_ID;
	data->sample_count = ++sample_counter;

	/* Log every 10th sample to reduce spam */
	if (sample_counter % 10 == 1) {
		LOG_INF("Temp: %d.%02d°C, Humidity: %d.%02d%% (sample #%u)", temp_c / 100,
			abs(temp_c % 100), humidity_rh / 100, humidity_rh % 100, sample_counter);
	}

	/* Send packet - router will add IoTSense header with PKT_ID_SENSOR_TEMP */
	swift_io_source_send_consume(&temperature_sensor_source, buf, K_MSEC(100));
}

/* ========================================================================== */
/* SENSOR THREAD */
/* ========================================================================== */

/**
 * @brief Temperature sensor thread function
 *
 * Periodically reads sensor and sends data packets.
 */
static void temperature_sensor_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("Temperature sensor started");

	while (1) {
		send_temperature_packet();
		k_sleep(K_MSEC(TEMP_SENSOR_SAMPLE_INTERVAL_MS));
	}
}

/* Create sensor thread */
K_THREAD_DEFINE(temp_sensor_thread, TEMP_SENSOR_THREAD_STACK_SIZE, temperature_sensor_thread, NULL,
		NULL, NULL, TEMP_SENSOR_THREAD_PRIORITY, 0, 0);
