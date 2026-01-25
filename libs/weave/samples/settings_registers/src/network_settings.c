/*
 * Copyright (c) 2026 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "settings.h"
#include "network_settings.h"

LOG_MODULE_DECLARE(app, LOG_LEVEL_INF);

/* Network configuration structure - internal to this compilation unit */
struct network_config {
	uint32_t ip_addr;  /* IPv4 address */
	uint16_t port;     /* Port number */
	bool dhcp_enabled; /* 0=static, 1=DHCP */
};

static const struct setting_field network_fields[] = {
	SETTING_FIELD_FROM_TYPE(0x00, struct network_config, ip_addr, SETTING_FLAG_RW),
	SETTING_FIELD_FROM_TYPE(0x04, struct network_config, port, SETTING_FLAG_RW),
	SETTING_FIELD_FROM_TYPE(0x06, struct network_config, dhcp_enabled, SETTING_FLAG_RW),
};

static const struct setting_group network_group = {
	.name = "network",
	.base_reg = 0x200,
	.size = sizeof(struct network_config),
	.fields = network_fields,
	.field_count = ARRAY_SIZE(network_fields),
};

/* Validator: port != 0 */
static int network_validate(struct weave_observable *obs, const void *new_value, void *user_data)
{
	ARG_UNUSED(obs);
	ARG_UNUSED(user_data);

	const struct network_config *cfg = new_value;

	if (cfg->port == 0) {
		return -EINVAL;
	}
	return 0;
}

/* Handler prints network settings on change */
static void on_network_changed(struct weave_observable *obs, void *user_data)
{
	ARG_UNUSED(user_data);

	struct network_config cfg;
	weave_observable_get_unchecked(obs, &cfg);
	LOG_INF("[NETWORK] ip=0x%08x port=%u dhcp=%u", cfg.ip_addr, cfg.port, cfg.dhcp_enabled);
}

SETTING_OBSERVABLE_DEFINE(network_settings, struct network_config, on_network_changed, WV_IMMEDIATE,
			  &network_group, network_validate);
