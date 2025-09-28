/*
 * Packet I/O Sample - Sensor Module
 *
 * Simulates sensor data sources generating packets
 */

#ifndef SENSORS_H
#define SENSORS_H

#include <zephyr_io/flow/flow.h>

/* Declare packet sources for external use */
FLOW_SOURCE_DECLARE(sensor1_source);
FLOW_SOURCE_DECLARE(sensor2_source);

#endif /* SENSORS_H */