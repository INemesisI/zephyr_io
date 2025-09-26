/*
 * Temperature Sensor Module
 *
 * Producer module that generates temperature sensor data packets.
 * This module demonstrates:
 * - Outbound-only packet generation
 * - Periodic data transmission
 * - Simple sensor data format
 */

#ifndef TEMPERATURE_SENSOR_H
#define TEMPERATURE_SENSOR_H

#include <zephyr/kernel.h>
#include <zephyr/packet_io/packet_io.h>

/* ========================================================================== */
/* TEMPERATURE SENSOR DATA FORMAT */
/* ========================================================================== */

/**
 * @brief Temperature sensor payload format
 */
struct temp_sensor_data {
	int16_t temperature_c; /** Temperature in Celsius * 100 (e.g., 2537 = 25.37°C) */
	int16_t humidity_rh;   /** Relative humidity * 100 (e.g., 4532 = 45.32% RH) */
	uint16_t sensor_id;    /** Sensor identifier */
	uint16_t sample_count; /** Number of samples averaged */
} __packed;

/* ========================================================================== */
/* TEMPERATURE SENSOR MODULE INTERFACE */
/* ========================================================================== */

/**
 * @brief Temperature sensor packet source
 *
 * Other modules can connect to this source to receive temperature data.
 * The router will connect this source to add IoTSense headers.
 */
PACKET_SOURCE_DECLARE(temperature_sensor_source);

#endif /* TEMPERATURE_SENSOR_H */