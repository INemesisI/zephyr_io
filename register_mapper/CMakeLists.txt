# SPDX-License-Identifier: Apache-2.0

# Add include directories
zephyr_include_directories(include)

if(CONFIG_REGISTER_MAPPER)
  add_subdirectory(subsys/register_mapper)
endif()