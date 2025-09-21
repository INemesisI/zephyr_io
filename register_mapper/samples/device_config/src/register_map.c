/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Application-defined register map
 *
 * This file defines how register addresses map to ZBUS channels.
 * The application has complete control over the register layout,
 * while modules only define their configuration structures.
 */

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/register_mapper/register_mapper.h>
#include "sensor_module.h"
#include "motor_module.h"

/* Declare channels from modules */
ZBUS_CHAN_DECLARE(sensor_status_chan);
ZBUS_CHAN_DECLARE(sensor_config_chan);
ZBUS_CHAN_DECLARE(sensor_command_chan);
ZBUS_CHAN_DECLARE(motor_chan);

/*
 * Register Layout:
 *
 * Status Registers (0x1000-0x1FFF) - Read-only runtime status
 * Configuration Registers (0x2000-0x2FFF) - Read-write configuration
 * Data Registers (0x3000-0x3FFF) - Data values
 * Command Registers (0x4000-0x4FFF) - Write-only commands/control
 *
 * This demonstrates grouped layout, but any layout is possible
 */

/* Status registers group at 0x1000 */
REG_MAPPING_DEFINE(reg_sensor_status, 0x1000, &sensor_status_chan,
		   struct sensor_status, status, REG_TYPE_U8, REG_FLAGS_RO);

REG_MAPPING_DEFINE(reg_motor_status, 0x1004, &motor_chan,
		   struct motor_config, status, REG_TYPE_U8, REG_FLAGS_RO);

REG_MAPPING_DEFINE(reg_motor_current, 0x1006, &motor_chan,
		   struct motor_config, current, REG_TYPE_U16, REG_FLAGS_RO);

/* Configuration registers group at 0x2000 */
/* Sensor config: 0x2000-0x200F */
REG_MAPPING_DEFINE(reg_sensor_threshold, 0x2000, &sensor_config_chan,
		   struct sensor_config, threshold, REG_TYPE_U32, REG_FLAGS_RW);

/* Motor config: 0x2010-0x201F */
REG_MAPPING_DEFINE(reg_motor_direction, 0x2010, &motor_chan,
		   struct motor_config, direction, REG_TYPE_U8, REG_FLAGS_RW);

REG_MAPPING_DEFINE(reg_motor_speed, 0x2012, &motor_chan,
		   struct motor_config, speed, REG_TYPE_U16, REG_FLAGS_RW);

REG_MAPPING_DEFINE(reg_motor_acceleration, 0x2014, &motor_chan,
		   struct motor_config, acceleration, REG_TYPE_I16, REG_FLAGS_RW);

/* Data registers group at 0x3000 */
REG_MAPPING_DEFINE(reg_sensor_data, 0x3000, &sensor_status_chan,
		   struct sensor_status, data, REG_TYPE_U32, REG_FLAGS_RO);

/* Command registers group at 0x4000 */
REG_MAPPING_DEFINE(reg_sensor_control, 0x4000, &sensor_command_chan,
		   struct sensor_command, control, REG_TYPE_U8, REG_FLAGS_WO);

/*
 * Register Map Summary:
 * 0x1000: Sensor Status (U8, RO)
 * 0x1004: Motor Status (U8, RO)
 * 0x1006: Motor Current (U16, RO)
 * 0x2000: Sensor Threshold (U32, RW)
 * 0x2010: Motor Direction (U8, RW)
 * 0x2012: Motor Speed (U16, RW)
 * 0x2014: Motor Acceleration (I16, RW)
 * 0x3000: Sensor Data (U32, RO)
 * 0x4000: Sensor Control Commands (U8, WO)
 */