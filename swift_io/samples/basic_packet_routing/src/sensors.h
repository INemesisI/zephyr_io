/*
 * Packet I/O Sample - Sensor Module
 *
 * Simulates sensor data sources generating packets
 */

#ifndef SENSORS_H
#define SENSORS_H

#include <zephyr_io/swift_io/swift_io.h>

/* Declare packet sources for external use */
SWIFT_IO_SOURCE_DECLARE(sensor1_source);
SWIFT_IO_SOURCE_DECLARE(sensor2_source);

#endif /* SENSORS_H */