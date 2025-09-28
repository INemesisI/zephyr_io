# SPDX-License-Identifier: Apache-2.0

# Add include directory for the module
zephyr_include_directories(include)

if(CONFIG_FLOW)
  add_subdirectory(subsys/flow)
endif()

if(CONFIG_FLOW_ROUTER)
  add_subdirectory(subsys/flow_router)
endif()