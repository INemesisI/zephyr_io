# SPDX-License-Identifier: Apache-2.0

# Add include directory for the module
zephyr_include_directories(include)

if(CONFIG_SWIFT_IO)
  add_subdirectory(subsys/swift_io)
endif()

if(CONFIG_SWIFT_ROUTER)
  add_subdirectory(subsys/swift_router)
endif()