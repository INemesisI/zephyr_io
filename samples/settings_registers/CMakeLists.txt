# Copyright (c) 2026 Zephyr Project
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(settings_registers)

target_sources(app PRIVATE
    src/main.c
    src/settings.c
    src/motor_settings.c
    src/network_settings.c
)
