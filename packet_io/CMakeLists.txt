# SPDX-License-Identifier: Apache-2.0

# Add include directory for the module
zephyr_include_directories(include)

if(CONFIG_PACKET_IO)
  add_subdirectory(subsys/packet_io)
endif()