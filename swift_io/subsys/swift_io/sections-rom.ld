/*
 * Linker script section for packet_io connections
 *
 * This creates an iterable section in ROM for static packet connections.
 * Z_LINK_ITERABLE_SUBALIGN is a standard Zephyr alignment value for
 * iterable sections (typically 4 bytes), ensuring proper alignment
 * for struct pointers across different architectures.
 */
#include <zephyr/linker/iterable_sections.h>

ITERABLE_SECTION_ROM(packet_connection, Z_LINK_ITERABLE_SUBALIGN)