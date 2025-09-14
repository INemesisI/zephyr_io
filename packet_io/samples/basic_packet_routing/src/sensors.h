/*
 * Packet I/O Sample - Sensor Module
 *
 * Simulates sensor data sources generating packets
 */

#ifndef SENSORS_H
#define SENSORS_H

#include <zephyr/packet_io/packet_io.h>

/* Declare packet sources for external use */
PACKET_SOURCE_DECLARE(sensor1_source);
PACKET_SOURCE_DECLARE(sensor2_source);

#endif /* SENSORS_H */