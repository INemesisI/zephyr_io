/*
 * Packet I/O Sample - Common Definitions
 *
 * Shared packet structures used by all modules
 */

#ifndef PACKET_DEFS_H
#define PACKET_DEFS_H

#include <stdint.h>

/* Define a simple header structure */
struct packet_header {
	uint8_t  source_id;
	uint8_t  packet_type;
	uint16_t sequence;
	uint32_t timestamp;
	uint16_t content_length;  /* Length of payload (excluding header) */
	uint16_t reserved;        /* Reserved for future use */
} __packed;

/* Packet types */
#define PACKET_TYPE_DATA	0x01
#define PACKET_TYPE_CONTROL	0x02

/* Source IDs */
#define SOURCE_ID_SENSOR1	1
#define SOURCE_ID_SENSOR2	2

#endif /* PACKET_DEFS_H */