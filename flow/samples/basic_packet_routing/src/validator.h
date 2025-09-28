/*
 * Packet I/O Sample - Validator Module
 *
 * Validates packet integrity and content correctness
 */

#ifndef VALIDATOR_H
#define VALIDATOR_H

#include <zephyr_io/flow/flow.h>

/* Declare packet sink for external use */
FLOW_SINK_DECLARE(validator_sink);

#endif /* VALIDATOR_H */