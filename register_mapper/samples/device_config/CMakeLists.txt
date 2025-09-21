# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(register_mapper_sample)

target_sources(app PRIVATE
    src/main.c
    src/sensor_module.c
    src/motor_module.c
    src/register_map.c
    src/uart_handler.c
)