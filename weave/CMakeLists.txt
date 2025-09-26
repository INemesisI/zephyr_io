# Copyright (c) 2024 Zephyr IO Project
# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(include)

if(CONFIG_WEAVE)
  add_subdirectory(subsys/weave)
endif()