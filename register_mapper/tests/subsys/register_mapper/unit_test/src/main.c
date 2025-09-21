/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Register mapper unit tests
 *
 * This file includes all the test suites for the register mapper module.
 * The ztest framework will automatically run all defined test suites.
 */

#include <zephyr/ztest.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(register_mapper_test, LOG_LEVEL_INF);