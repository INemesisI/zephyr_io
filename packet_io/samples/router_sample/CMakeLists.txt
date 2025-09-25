# Packet Router Sample

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(packet_router_sample)

# Include directories for the router library and application
target_include_directories(app PRIVATE
  src
)

# Source files
target_sources(app PRIVATE
  # Generic router library
  src/lib/packet_router.c

  # IoTSense protocol router
  src/iotsense_router.c

  # Application modules
  src/modules/tcp_server.c
  src/modules/temperature_sensor.c
  src/modules/system_control.c
  src/modules/led_controller.c

  # Main application
  src/main.c
)

# Add linker script fragment for router iterable sections
zephyr_linker_sources(SECTIONS sections-rom.ld)