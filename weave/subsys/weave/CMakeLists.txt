# Copyright (c) 2024 Zephyr IO Project
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(
  weave_core.c
)

zephyr_library_include_directories(
  ${ZEPHYR_BASE}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

# Add linker script for iterable sections
zephyr_linker_sources(
  ROM_SECTIONS
  ${CMAKE_CURRENT_SOURCE_DIR}/sections-rom.ld
)