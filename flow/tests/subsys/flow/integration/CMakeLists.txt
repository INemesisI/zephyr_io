# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Add our module to the build
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(flow_integration)

target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE ${ZEPHYR_BASE}/subsys/testsuite/include)