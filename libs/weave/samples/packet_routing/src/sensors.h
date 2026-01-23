/*
 * Packet Routing Sample - Sensor Module
 *
 * Simulates sensor data sources generating packets
 */

#ifndef SENSORS_H
#define SENSORS_H

#include <weave/packet.h>

/* Source IDs for sensors */
#define SOURCE_ID_SENSOR1 1
#define SOURCE_ID_SENSOR2 2

/* Declare packet sources for external use */
WEAVE_PACKET_SOURCE_DECLARE(sensor1_source);
WEAVE_PACKET_SOURCE_DECLARE(sensor2_source);

/* Control functions */
void sensor_start_sampling(void);
void sensor_stop_sampling(void);

#endif /* SENSORS_H */
