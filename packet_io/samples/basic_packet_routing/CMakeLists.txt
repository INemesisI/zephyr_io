# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(packet_io_basic_sample)

target_sources(app PRIVATE
    src/main.c
    src/sensors.c
    src/processor.c
    src/network.c
    src/validator.c
)