# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_PACKET_IO packet_io.c)
zephyr_library_sources_ifdef(CONFIG_PACKET_IO_RUNTIME_OBSERVERS packet_io_runtime.c)

# Add linker script fragment for iterable sections
zephyr_linker_sources(SECTIONS sections-rom.ld)