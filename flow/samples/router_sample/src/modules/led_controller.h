/*
 * LED Controller - Toggle Only
 */

#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <zephyr/kernel.h>
#include <zephyr_io/flow/flow.h>

/* LED toggle command packet */
struct led_toggle_cmd {
	uint8_t command;
} __packed;

#define LED_TOGGLE_CMD 0x02
#define LED_ID_STATUS  0

FLOW_SINK_DECLARE(led_controller_sink);

#endif /* LED_CONTROLLER_H */