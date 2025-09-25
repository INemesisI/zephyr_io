/*
 * Linker script sections for packet router routes
 *
 * This creates iterable sections in ROM for static router inbound and outbound routes.
 * Z_LINK_ITERABLE_SUBALIGN is a standard Zephyr alignment value for
 * iterable sections (typically 4 bytes), ensuring proper alignment
 * for struct pointers across different architectures.
 */
#include <zephyr/linker/iterable_sections.h>

ITERABLE_SECTION_ROM(router_inbound_route, Z_LINK_ITERABLE_SUBALIGN)
ITERABLE_SECTION_ROM(router_outbound_route, Z_LINK_ITERABLE_SUBALIGN)