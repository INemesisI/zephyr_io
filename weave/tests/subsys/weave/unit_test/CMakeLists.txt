# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Add our module to the build
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(weave_unit_test)

target_sources(app PRIVATE
    src/test_common.c
    src/test_init.c
    src/test_method_calls.c
    src/test_signals.c
    src/test_memory.c
    src/test_message_processing.c
    src/test_concurrency.c
    src/test_timeouts.c
    src/test_error_paths.c
    src/test_stress.c
)

target_include_directories(app PRIVATE ${ZEPHYR_BASE}/subsys/testsuite/include)