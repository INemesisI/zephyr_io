/*
 * Copyright (c) 2024 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ZBUS channel helpers for register mapper
 *
 * Provides macros and structures for defining ZBUS channels with
 * register mapper state tracking for block write operations.
 */

#ifndef ZEPHYR_INCLUDE_REGISTER_CHANNEL_H_
#define ZEPHYR_INCLUDE_REGISTER_CHANNEL_H_

#include <zephyr/zbus/zbus.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Channel state - stored in ZBUS channel user_data */
struct channel_state {
	bool update_pending;          /* Notification pending for this channel */
	/* Additional runtime state can be added here */
};

/**
 * @brief Define a ZBUS channel with register mapper state tracking
 *
 * This macro creates a ZBUS channel with an associated channel_state
 * structure for tracking pending updates during block write operations.
 * It has the same signature as ZBUS_CHAN_DEFINE but automatically manages
 * the channel_state in user_data for register mapper support.
 *
 * @param _name Channel variable name
 * @param _type Channel message type
 * @param _validator Channel validator (can be NULL)
 * @param _user_data User data (MUST be NULL or &_name##_state for register mapper)
 * @param _observers Channel observers
 * @param _init_val Initial message value
 */
/* Helper macro to strip parentheses */
#define REGISTER_CHAN_STRIP_PARENS(...) __VA_ARGS__

#define REGISTER_CHAN_DEFINE(_name, _type, _validator, _user_data, _observers, _init_val) \
	static struct channel_state _name##_state = {                         \
		.update_pending = false                                       \
	};                                                                     \
	BUILD_ASSERT(_user_data == NULL || _user_data == &_name##_state,      \
		     "user_data must be NULL for register mapper channels");  \
	ZBUS_CHAN_DEFINE(_name,                                               \
		_type,                                                         \
		_validator,                                                    \
		&_name##_state, /* Channel state in user_data */              \
		_observers,                                                    \
		REGISTER_CHAN_STRIP_PARENS _init_val                          \
	)

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_REGISTER_CHANNEL_H_ */