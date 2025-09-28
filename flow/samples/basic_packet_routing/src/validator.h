/*
 * Packet I/O Sample - Validator Module
 *
 * Validates packet integrity and content correctness
 */

#ifndef VALIDATOR_H
#define VALIDATOR_H

#include <zephyr_io/flow/flow.h>

/* Declare routed validator sinks for external use */
FLOW_SINK_DECLARE(validator1_sink); /* For sensor 1 packets */
FLOW_SINK_DECLARE(validator2_sink); /* For sensor 2 packets */

#endif /* VALIDATOR_H */