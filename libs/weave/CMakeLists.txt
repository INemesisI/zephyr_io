# Copyright (c) 2026 Zephyr Project
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_WEAVE)
  zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR}/include)

  zephyr_library()
  zephyr_library_sources(${CMAKE_CURRENT_LIST_DIR}/src/core.c)

  zephyr_linker_sources(SECTIONS ${CMAKE_CURRENT_LIST_DIR}/sections-rom.ld)

  # Packet - net_buf packet routing
  zephyr_library_sources_ifdef(CONFIG_WEAVE_PACKET ${CMAKE_CURRENT_LIST_DIR}/src/packet.c)

  # Method - RPC framework
  zephyr_library_sources_ifdef(CONFIG_WEAVE_METHOD ${CMAKE_CURRENT_LIST_DIR}/src/method.c)

  # Observable - stateful pub/sub
  zephyr_library_sources_ifdef(CONFIG_WEAVE_OBSERVABLE ${CMAKE_CURRENT_LIST_DIR}/src/observable.c)
endif()
