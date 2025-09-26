/*
 * Linker script section for swift_io connections
 *
 * This creates an iterable section in ROM for static swift_io connections.
 * Z_LINK_ITERABLE_SUBALIGN is a standard Zephyr alignment value for
 * iterable sections (typically 4 bytes), ensuring proper alignment
 * for struct pointers across different architectures.
 */
#include <zephyr/linker/iterable_sections.h>

ITERABLE_SECTION_ROM(swift_io_connection, Z_LINK_ITERABLE_SUBALIGN)