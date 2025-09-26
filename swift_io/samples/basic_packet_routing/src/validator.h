/*
 * Packet I/O Sample - Validator Module
 *
 * Validates packet integrity and content correctness
 */

#ifndef VALIDATOR_H
#define VALIDATOR_H

#include <zephyr_io/swift_io/swift_io.h>

/* Declare packet sink for external use */
SWIFT_IO_SINK_DECLARE(validator_sink);

#endif /* VALIDATOR_H */