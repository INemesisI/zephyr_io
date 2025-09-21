# Copyright (c) 2024 Zephyr Project
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Find Zephyr package
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# Define the project
project(register_mapper_unit_test)

# Include register_mapper headers
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include
)

# Add test source files
target_sources(app PRIVATE
    src/main.c
    src/test_basic.c
    src/test_concurrency.c
    src/test_advanced.c
)