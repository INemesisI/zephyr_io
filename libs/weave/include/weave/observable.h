/*
 * Copyright (c) 2026 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Weave Observable API
 *
 * Weave Observable - Stateful publish/subscribe
 *
 * Observables hold state and notify connected sinks on changes.
 * - Zero allocation
 * - Uses standard weave sinks as observers
 * - Sink handler receives pointer to observable, reads value via get
 *
 * Similar to zbus channels but simpler and lighter weight.
 */

#ifndef ZEPHYR_INCLUDE_WEAVE_OBSERVABLE_H_
#define ZEPHYR_INCLUDE_WEAVE_OBSERVABLE_H_

#include <weave/core.h>
#include <zephyr/kernel.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup weave_observable_apis Weave Observable APIs
 * @ingroup os_services
 * @{
 */

/**
 * Passthrough ops for observable sources.
 * Non-NULL ops bypasses the "single sink" restriction in core.c,
 * but NULL ref/unref means no actual lifecycle management needed.
 */
static const struct weave_payload_ops weave_observable_ops = {
	.ref = NULL,
	.unref = NULL,
};

/* ============================ Type Definitions ============================ */

/* Forward declaration */
struct weave_observable;

/**
 * @brief Observable handler function signature (owner notification)
 *
 * @param obs Pointer to the observable that changed
 * @param user_data User data from owner_sink definition
 */
typedef void (*weave_observable_handler_t)(struct weave_observable *obs, void *user_data);

/**
 * @brief Observable validator function signature
 *
 * Validators can reject value changes before they are applied.
 *
 * @param obs Pointer to the observable
 * @param new_value Pointer to the proposed new value
 * @param user_data User data from owner_sink definition
 * @return 0 to accept the change, negative error code to reject
 */
typedef int (*weave_observable_validator_t)(struct weave_observable *obs, const void *new_value,
					    void *user_data);

/**
 * @brief Observable structure
 *
 * Holds state and notifies connected observers on changes.
 * Includes optional owner handler (called before external observers)
 * and optional validator (can reject changes).
 */
struct weave_observable {
	/** Source for external observer notifications */
	struct weave_source source;
	/** Pointer to value buffer */
	void *value;
	/** Value size in bytes */
	size_t size;
	/** Semaphore protecting value during updates */
	struct k_sem sem;
	/** Owner notification sink (handler/queue/user_data) */
	struct weave_sink owner_sink;
	/** Validator function (optional) */
	weave_observable_validator_t validator;
};

/* ============================ Macros ============================ */

/** @brief No owner handler */
#define WV_NO_HANDLER NULL

/** @brief No validator */
#define WV_NO_VALID NULL

/**
 * @brief Declare an observable (for header files)
 *
 * Creates typedef for type-safe SET/GET macros.
 *
 * @param _name Observable variable name
 * @param _type Value type
 */
#define WEAVE_OBSERVABLE_DECLARE(_name, _type)                                                     \
	typedef _type _name##_obs_t;                                                               \
	extern struct weave_observable _name

/**
 * @brief Define an observable with static value storage
 *
 * Creates an observable with optional owner handler and validator.
 * The handler and validator share the same user_data from owner_sink.
 *
 * @param _name Observable variable name
 * @param _type Value type
 * @param _handler Owner handler (WV_NO_HANDLER if none)
 * @param _queue Message queue for handler (WV_IMMEDIATE or &queue)
 * @param _user_data User data for handler and validator
 * @param _validator Validator function (WV_NO_VALID if none)
 */
#define WEAVE_OBSERVABLE_DEFINE(_name, _type, _handler, _queue, _user_data, _validator)            \
	static _type _name##_value;                                                                \
	struct weave_observable _name = {                                                          \
		.source = WEAVE_SOURCE_INITIALIZER(_name, &weave_observable_ops),                  \
		.value = &_name##_value,                                                           \
		.size = sizeof(_type),                                                             \
		.sem = Z_SEM_INITIALIZER(_name.sem, 1, 1),                                         \
		.owner_sink = WEAVE_SINK_INITIALIZER((weave_handler_t)(_handler), (_queue),        \
						     (_user_data), WV_NO_OPS),                     \
		.validator = (_validator),                                                         \
	}

/**
 * @brief Define an observer
 *
 * Unified macro for all observer configurations.
 * Handler signature: void handler(struct weave_observable *obs, void *user_data)
 *
 * @param _name Observer variable name
 * @param _handler Handler function
 * @param _queue Message queue (WV_IMMEDIATE for immediate mode, or &queue for queued)
 * @param _user_data User data pointer (NULL if unused)
 */
#define WEAVE_OBSERVER_DEFINE(_name, _handler, _queue, _user_data)                                 \
	struct weave_sink _name = WEAVE_SINK_INITIALIZER((weave_handler_t)(_handler), (_queue),    \
							 (_user_data), WV_NO_OPS)

/**
 * @brief Connect an observer to an observable
 *
 * @param _observable Observable variable (not pointer)
 * @param _observer Observer variable (not pointer)
 */
#define WEAVE_OBSERVER_CONNECT(_observable, _observer)                                             \
	WEAVE_CONNECT(&(_observable).source, &(_observer))

/**
 * @brief Type-safe set macro
 *
 * Copies value to observable and notifies all connected observers.
 * Compile-time type checking via pointer assignment.
 *
 * @param _observable Observable variable (not pointer)
 * @param _value_ptr Pointer to new value
 * @return Number of observers notified, or negative errno
 */
#define WEAVE_OBSERVABLE_SET(_observable, _value_ptr)                                              \
	weave_observable_set_unchecked(&(_observable), (_value_ptr));                              \
	_observable##_obs_t *_chk_set __attribute__((unused)) = (_value_ptr)

/**
 * @brief Type-safe get macro
 *
 * Copies current value from observable.
 * Compile-time type checking via pointer assignment.
 *
 * @param _observable Observable variable (not pointer)
 * @param _value_ptr Pointer to receive value
 * @return 0 on success, negative errno on error
 */
#define WEAVE_OBSERVABLE_GET(_observable, _value_ptr)                                              \
	weave_observable_get_unchecked(&(_observable), (_value_ptr));                              \
	_observable##_obs_t *_chk_get __attribute__((unused)) = (_value_ptr)

/* ============================ Function APIs ============================ */

/**
 * @brief Set observable value and notify observers (unchecked - no type checking)
 *
 * @note Prefer using WEAVE_OBSERVABLE_SET() macro for compile-time type safety.
 *
 * Thread-safe: locks during memcpy, notifies after unlock.
 * Connected observers receive pointer to observable.
 *
 * @param obs Pointer to observable
 * @param value Pointer to new value
 * @return Number of observers notified, or negative errno
 */
int weave_observable_set_unchecked(struct weave_observable *obs, const void *value);

/**
 * @brief Get current observable value
 *
 * Thread-safe copy of current value.
 *
 * @param obs Pointer to observable
 * @param value Pointer to receive value
 * @return 0 on success, negative errno on error
 */
int weave_observable_get_unchecked(struct weave_observable *obs, void *value);

/**
 * @brief Claim exclusive access to observable value
 *
 * Locks the observable and returns pointer to value for in-place modification.
 * Must be followed by finish() or publish().
 *
 * @param obs Pointer to observable
 * @param timeout How long to wait for lock
 * @return Pointer to value buffer, or NULL on timeout/error
 */
void *weave_observable_claim(struct weave_observable *obs, k_timeout_t timeout);

/**
 * @brief Finish claimed access without notifying observers
 *
 * Releases lock without triggering notifications.
 * Use for read-only access or when notification is not needed.
 *
 * @param obs Pointer to observable (must have been claimed)
 */
void weave_observable_finish(struct weave_observable *obs);

/**
 * @brief Publish claimed modifications and notify observers
 *
 * Releases lock and notifies owner handler + all connected observers.
 *
 * @param obs Pointer to observable (must have been claimed)
 * @return Number of observers notified, or negative errno
 */
int weave_observable_publish(struct weave_observable *obs);

/**
 * @brief Validate a value without applying it
 *
 * Calls the observable's validator (if defined) on the given value.
 * Does not modify state or notify observers.
 *
 * @param obs Pointer to observable
 * @param value Pointer to value to validate
 * @return 0 if valid, negative errno if invalid or no validator
 */
int weave_observable_validate(struct weave_observable *obs, const void *value);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_WEAVE_OBSERVABLE_H_ */
