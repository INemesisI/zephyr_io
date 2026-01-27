/*
 * Copyright (c) 2026 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Linker script fragment for Weave module.
 * Creates iterable sections for static connections.
 */

#include <zephyr/linker/iterable_sections.h>

/* Weave core connections */
ITERABLE_SECTION_ROM(weave_connection, Z_LINK_ITERABLE_SUBALIGN)

/* Flow connections (net_buf routing) */
ITERABLE_SECTION_ROM(flow_connection, Z_LINK_ITERABLE_SUBALIGN)
