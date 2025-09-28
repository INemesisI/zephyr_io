# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_FLOW_ROUTER flow_router.c)

# Add linker script fragment for router iterable sections
zephyr_linker_sources(SECTIONS sections-rom.ld)