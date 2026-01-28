# Copyright (c) 2026 Zephyr Project
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(settings_table)

# Linker fragment for setting_area iterable section
zephyr_linker_sources(SECTIONS sections-rom.ld)

target_sources(app PRIVATE
    src/main.c
    src/settings_api.c
    src/motor_settings.c
    src/sensor_settings.c
    src/settings_shell.c
)
