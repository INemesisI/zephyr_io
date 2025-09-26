/*
 * System Control Module
 *
 * Bidirectional module that handles system control commands from network
 * and sends system status responses back. This module demonstrates:
 * - Inbound packet processing (commands)
 * - Outbound packet generation (responses)
 * - Command-response pattern
 * - System state management
 */

#ifndef SYSTEM_CONTROL_H
#define SYSTEM_CONTROL_H

#include <zephyr/kernel.h>
#include <zephyr_io/swift_io/swift_io.h>

/* Ping command packet */
struct ping_cmd {
	uint8_t command;
	uint16_t seq_num;
} __packed;

/* Ping response packet */
struct ping_resp {
	uint8_t command;
	uint16_t seq_num;
	uint32_t timestamp;
} __packed;

#define PING_CMD 0x01

SWIFT_IO_SINK_DECLARE(system_control_sink);
SWIFT_IO_SOURCE_DECLARE(system_control_source);

#endif /* SYSTEM_CONTROL_H */