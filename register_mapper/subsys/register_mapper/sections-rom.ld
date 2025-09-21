/*
 * Linker script section for register_mapper mappings
 *
 * This creates an iterable section in ROM for static register mappings.
 * Z_LINK_ITERABLE_SUBALIGN is a standard Zephyr alignment value for
 * iterable sections (typically 4 bytes), ensuring proper alignment
 * for struct pointers across different architectures.
 */
#include <zephyr/linker/iterable_sections.h>

ITERABLE_SECTION_ROM(reg_mapping, Z_LINK_ITERABLE_SUBALIGN)