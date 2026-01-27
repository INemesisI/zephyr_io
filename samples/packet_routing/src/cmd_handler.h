/*
 * Packet Routing Sample - Command Handler Module
 *
 * Handles control commands from TCP server (start/stop sampling)
 */

#ifndef CMD_HANDLER_H
#define CMD_HANDLER_H

#include <weave/packet.h>

/* Command definitions */
#define CMD_START_SAMPLING 0x01
#define CMD_STOP_SAMPLING  0x02

/* Declare command sink for external use */
WEAVE_PACKET_SINK_DECLARE(cmd_sink);

#endif /* CMD_HANDLER_H */
