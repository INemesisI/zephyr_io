# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_REGISTER_MAPPER register_mapper.c)

# Add linker script fragment for iterable sections
zephyr_linker_sources(SECTIONS sections-rom.ld)