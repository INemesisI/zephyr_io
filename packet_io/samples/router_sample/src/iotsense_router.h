/*
 * IoTSense Router Header
 *
 * Defines the IoTSense protocol packet format and router instance.
 */

#ifndef IOTSENSE_ROUTER_H
#define IOTSENSE_ROUTER_H

#include "lib/packet_router.h"
#include <stdbool.h>
#include <stdint.h>

/* ========================================================================== */
/* IOTSENSE PACKET HEADER FORMAT */
/* ========================================================================== */

/**
 * @brief Simplified IoTSense Protocol Header
 *
 * Wire format (all fields little-endian):
 * +--------+--------+
 * |  VER   | PKT_ID |
 * +--------+--------+
 * |   PAYLOAD_LEN   |
 * +-----------------+
 * |    PAYLOAD...   |
 * +-----------------+
 *
 * Total header size: 4 bytes
 */
struct iotsense_header {
  uint8_t version;      /** Protocol version (0x01) */
  uint8_t packet_id;    /** Packet type identifier */
  uint16_t payload_len; /** Length of payload data */
} __packed;

/* ========================================================================== */
/* ROUTER INSTANCE */
/* ========================================================================== */

ROUTER_DECLARE(iotsense_router);

/* ========================================================================== */
/* IOTSENSE PROTOCOL PACKET IDs */
/* ========================================================================== */

/*
 * Simplified IoT Sensor Protocol
 *
 * Packet ID space allocation (8-bit IDs):
 * 0x00        - Reserved (invalid/error)
 * 0x01-0x0F   - Sensor data packets
 * 0x10-0x1F   - Actuator commands
 * 0x20-0x2F   - System/Control packets
 * 0x30-0x3F   - Status/monitoring
 * 0x40-0xFF   - Application specific
 */

/* Invalid Packet */
#define PKT_ID_INVALID 0x00 /** Invalid/error packet */

/* Used packet IDs in this sample */
#define PKT_ID_SENSOR_TEMP 0x01   /** Temperature sensor data */
#define PKT_ID_ACTUATOR_LED 0x02  /** LED control commands */
#define PKT_ID_SYSTEM_CONFIG 0x09 /** System configuration */

#endif /* IOTSENSE_ROUTER_H */