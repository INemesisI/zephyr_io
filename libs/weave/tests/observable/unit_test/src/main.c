/*
 * Copyright (c) 2026 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>
#include <weave/observable.h>

/* Test configuration constants */
#define TEST_QUEUE_SIZE 16

/* =============================================================================
 * Test Value Types
 * =============================================================================
 */

struct test_sensor_data {
	int32_t temperature;
	int32_t humidity;
	uint32_t timestamp;
};

/* =============================================================================
 * Test Capture Context
 * =============================================================================
 */

struct observer_capture {
	atomic_t call_count;
	void *last_user_data;
	struct weave_observable *last_obs;
	struct test_sensor_data last_value;
};

static struct observer_capture captures[4] = {0};

static void reset_all_captures(void)
{
	ARRAY_FOR_EACH_PTR(captures, capture) {
		atomic_clear(&capture->call_count);
		capture->last_user_data = NULL;
		capture->last_obs = NULL;
		memset(&capture->last_value, 0, sizeof(capture->last_value));
	}
}

/* =============================================================================
 * Observer Handlers
 * =============================================================================
 */

static void observer_handler(struct weave_observable *obs, void *user_data)
{
	struct observer_capture *capture = (struct observer_capture *)user_data;

	zassert_not_null(capture, "Capture context should not be NULL");
	zassert_not_null(obs, "Observable should not be NULL");

	atomic_inc(&capture->call_count);
	capture->last_user_data = user_data;
	capture->last_obs = obs;

	/* Read the current value */
	weave_observable_get_unchecked(obs, &capture->last_value);
}

/* =============================================================================
 * Test Infrastructure
 * =============================================================================
 */

/* Message queue for queued observers */
WEAVE_MSGQ_DEFINE(obs_queue, TEST_QUEUE_SIZE);

/* Define observables - using minimal form (no handler, no validator) */
WEAVE_OBSERVABLE_DEFINE(sensor_obs, struct test_sensor_data, WV_NO_HANDLER, WV_IMMEDIATE, NULL,
			WV_NO_VALID);
WEAVE_OBSERVABLE_DEFINE(isolated_obs, struct test_sensor_data, WV_NO_HANDLER, WV_IMMEDIATE, NULL,
			WV_NO_VALID);

/* Declare for type-safe macros */
WEAVE_OBSERVABLE_DECLARE(sensor_obs, struct test_sensor_data);

/* Define observers */
WEAVE_OBSERVER_DEFINE(observer_imm_0, observer_handler, WV_IMMEDIATE, &captures[0]);
WEAVE_OBSERVER_DEFINE(observer_imm_1, observer_handler, WV_IMMEDIATE, &captures[1]);
WEAVE_OBSERVER_DEFINE(observer_queued_0, observer_handler, &obs_queue, &captures[2]);
WEAVE_OBSERVER_DEFINE(observer_queued_1, observer_handler, &obs_queue, &captures[3]);

/* Connect observers to sensor_obs:
 * - 2 immediate observers
 * - 2 queued observers
 */
WEAVE_OBSERVER_CONNECT(sensor_obs, observer_imm_0);
WEAVE_OBSERVER_CONNECT(sensor_obs, observer_imm_1);
WEAVE_OBSERVER_CONNECT(sensor_obs, observer_queued_0);
WEAVE_OBSERVER_CONNECT(sensor_obs, observer_queued_1);

/* isolated_obs has no observers */

/* =============================================================================
 * Helper Functions
 * =============================================================================
 */

static void process_all_messages(void)
{
	int count = 0;
	const int max_iterations = 50;

	while (weave_process_messages(&obs_queue, K_NO_WAIT) > 0 && count < max_iterations) {
		count++;
	}
}

/* =============================================================================
 * Test Setup/Teardown
 * =============================================================================
 */

static void test_setup(void *fixture)
{
	ARG_UNUSED(fixture);

	reset_all_captures();
	k_msgq_purge(&obs_queue);

	/* Reset observable value */
	struct test_sensor_data zero = {0};
	weave_observable_set_unchecked(&sensor_obs, &zero);
	reset_all_captures(); /* Clear notifications from reset */
	k_msgq_purge(&obs_queue);
}

static void test_teardown(void *fixture)
{
	ARG_UNUSED(fixture);

	process_all_messages();

	/* Verify queue is empty */
	zassert_equal(k_msgq_num_used_get(&obs_queue), 0, "Queue should be empty");
}

ZTEST_SUITE(weave_observable_unit_test, NULL, NULL, test_setup, test_teardown, NULL);

/* =============================================================================
 * Basic Set/Get Tests
 * =============================================================================
 */

ZTEST(weave_observable_unit_test, test_set_get_basic)
{
	struct test_sensor_data input = {
		.temperature = 2500,
		.humidity = 6000,
		.timestamp = 12345,
	};
	struct test_sensor_data output = {0};
	int ret;

	/* Set value */
	ret = weave_observable_set_unchecked(&sensor_obs, &input);
	zassert_true(ret >= 0, "Set should succeed, got %d", ret);

	/* Get value */
	ret = weave_observable_get_unchecked(&sensor_obs, &output);
	zassert_equal(ret, 0, "Get should succeed");

	/* Verify value */
	zassert_equal(output.temperature, 2500, "Temperature should match");
	zassert_equal(output.humidity, 6000, "Humidity should match");
	zassert_equal(output.timestamp, 12345, "Timestamp should match");
}

ZTEST(weave_observable_unit_test, test_set_notifies_observers)
{
	struct test_sensor_data input = {
		.temperature = 1000,
		.humidity = 5000,
		.timestamp = 100,
	};
	int ret;

	ret = weave_observable_set_unchecked(&sensor_obs, &input);

	/* Should notify 4 observers */
	zassert_equal(ret, 4, "Should notify 4 observers");

	/* Immediate observers should have been called already */
	zassert_equal(atomic_get(&captures[0].call_count), 1, "Immediate observer 0 called");
	zassert_equal(atomic_get(&captures[1].call_count), 1, "Immediate observer 1 called");
	zassert_equal(captures[0].last_obs, &sensor_obs, "Observer 0 got correct observable");
	zassert_equal(captures[1].last_obs, &sensor_obs, "Observer 1 got correct observable");

	/* Queued observers not yet called */
	zassert_equal(atomic_get(&captures[2].call_count), 0, "Queued observer 0 not yet called");
	zassert_equal(atomic_get(&captures[3].call_count), 0, "Queued observer 1 not yet called");

	/* Process queued */
	process_all_messages();

	/* Now queued observers should be called */
	zassert_equal(atomic_get(&captures[2].call_count), 1, "Queued observer 0 called");
	zassert_equal(atomic_get(&captures[3].call_count), 1, "Queued observer 1 called");

	/* Verify all observers got the same value */
	zassert_equal(captures[0].last_value.temperature, 1000, "Observer 0 value correct");
	zassert_equal(captures[1].last_value.temperature, 1000, "Observer 1 value correct");
	zassert_equal(captures[2].last_value.temperature, 1000, "Observer 2 value correct");
	zassert_equal(captures[3].last_value.temperature, 1000, "Observer 3 value correct");
}

ZTEST(weave_observable_unit_test, test_set_isolated_observable)
{
	struct test_sensor_data input = {.temperature = 999, .humidity = 999, .timestamp = 999};
	int ret;

	/* isolated_obs has no observers */
	ret = weave_observable_set_unchecked(&isolated_obs, &input);
	zassert_equal(ret, 0, "Should return 0 observers notified");

	/* No captures should be affected */
	zassert_equal(atomic_get(&captures[0].call_count), 0, "No observers called");
}

/* =============================================================================
 * Input Validation Tests - set_unchecked
 * =============================================================================
 */

ZTEST(weave_observable_unit_test, test_set_null_obs)
{
	struct test_sensor_data input = {.temperature = 1, .humidity = 2, .timestamp = 3};
	int ret;

	ret = weave_observable_set_unchecked(NULL, &input);
	zassert_equal(ret, -EINVAL, "Should reject NULL observable");
}

ZTEST(weave_observable_unit_test, test_set_null_value)
{
	int ret;

	ret = weave_observable_set_unchecked(&sensor_obs, NULL);
	zassert_equal(ret, -EINVAL, "Should reject NULL value");
}

ZTEST(weave_observable_unit_test, test_set_null_obs_value)
{
	/* Create an observable with NULL value pointer */
	struct weave_observable bad_obs = {
		.source = WEAVE_SOURCE_INITIALIZER(bad_obs, &weave_observable_ops),
		.value = NULL, /* NULL value buffer! */
		.size = sizeof(struct test_sensor_data),
		.sem = Z_SEM_INITIALIZER(bad_obs.sem, 1, 1),
	};
	struct test_sensor_data input = {.temperature = 1, .humidity = 2, .timestamp = 3};
	int ret;

	ret = weave_observable_set_unchecked(&bad_obs, &input);
	zassert_equal(ret, -EINVAL, "Should reject observable with NULL value buffer");
}

/* =============================================================================
 * Input Validation Tests - get_unchecked
 * =============================================================================
 */

ZTEST(weave_observable_unit_test, test_get_null_obs)
{
	struct test_sensor_data output = {0};
	int ret;

	ret = weave_observable_get_unchecked(NULL, &output);
	zassert_equal(ret, -EINVAL, "Should reject NULL observable");
}

ZTEST(weave_observable_unit_test, test_get_null_value)
{
	int ret;

	ret = weave_observable_get_unchecked(&sensor_obs, NULL);
	zassert_equal(ret, -EINVAL, "Should reject NULL value");
}

ZTEST(weave_observable_unit_test, test_get_null_obs_value)
{
	/* Create an observable with NULL value pointer */
	struct weave_observable bad_obs = {
		.source = WEAVE_SOURCE_INITIALIZER(bad_obs, &weave_observable_ops),
		.value = NULL, /* NULL value buffer! */
		.size = sizeof(struct test_sensor_data),
		.sem = Z_SEM_INITIALIZER(bad_obs.sem, 1, 1),
	};
	struct test_sensor_data output = {0};
	int ret;

	ret = weave_observable_get_unchecked(&bad_obs, &output);
	zassert_equal(ret, -EINVAL, "Should reject observable with NULL value buffer");
}

/* =============================================================================
 * Type-Safe Macro Tests
 * =============================================================================
 */

ZTEST(weave_observable_unit_test, test_type_safe_set_get)
{
	struct test_sensor_data input = {
		.temperature = 3333,
		.humidity = 4444,
		.timestamp = 5555,
	};
	struct test_sensor_data output = {0};

	/* Use type-safe macros */
	WEAVE_OBSERVABLE_SET(sensor_obs, &input);

	process_all_messages(); /* Process queued observers */

	WEAVE_OBSERVABLE_GET(sensor_obs, &output);

	zassert_equal(output.temperature, 3333, "Temperature should match");
	zassert_equal(output.humidity, 4444, "Humidity should match");
	zassert_equal(output.timestamp, 5555, "Timestamp should match");
}

/* =============================================================================
 * Observer Notification Tests
 * =============================================================================
 */

ZTEST(weave_observable_unit_test, test_immediate_observer)
{
	struct test_sensor_data input = {.temperature = 111, .humidity = 222, .timestamp = 333};

	weave_observable_set_unchecked(&sensor_obs, &input);

	/* Immediate observers should be called synchronously */
	zassert_equal(atomic_get(&captures[0].call_count), 1, "Immediate observer called");
	zassert_equal(captures[0].last_value.temperature, 111, "Value correct");
	zassert_equal(captures[0].last_user_data, &captures[0], "User data correct");
}

ZTEST(weave_observable_unit_test, test_queued_observer)
{
	struct test_sensor_data input = {.temperature = 777, .humidity = 888, .timestamp = 999};

	weave_observable_set_unchecked(&sensor_obs, &input);

	/* Queued observers should NOT be called yet */
	zassert_equal(atomic_get(&captures[2].call_count), 0, "Queued observer not yet called");

	/* Process queue */
	process_all_messages();

	/* Now queued observers should be called */
	zassert_equal(atomic_get(&captures[2].call_count), 1, "Queued observer called");
	zassert_equal(captures[2].last_value.temperature, 777, "Value correct");
}

ZTEST(weave_observable_unit_test, test_multiple_sets)
{
	const int num_sets = 5;
	struct test_sensor_data input;

	for (int i = 0; i < num_sets; i++) {
		input.temperature = i * 100;
		input.humidity = i * 200;
		input.timestamp = i;

		weave_observable_set_unchecked(&sensor_obs, &input);
	}

	/* Immediate observers should have been called num_sets times */
	zassert_equal(atomic_get(&captures[0].call_count), num_sets,
		      "Immediate observer called %d times", num_sets);
	zassert_equal(atomic_get(&captures[1].call_count), num_sets,
		      "Immediate observer called %d times", num_sets);

	/* Last value should be from last set */
	zassert_equal(captures[0].last_value.temperature, 400, "Last temperature correct");

	/* Process queued */
	process_all_messages();

	/* Queued observers should also have been called num_sets times */
	zassert_equal(atomic_get(&captures[2].call_count), num_sets,
		      "Queued observer called %d times", num_sets);
	zassert_equal(atomic_get(&captures[3].call_count), num_sets,
		      "Queued observer called %d times", num_sets);
}

/* =============================================================================
 * Structure Verification Tests
 * =============================================================================
 */

ZTEST(weave_observable_unit_test, test_observable_define_structure)
{
	/* Verify WEAVE_OBSERVABLE_DEFINE creates correct structure */
	zassert_not_null(sensor_obs.value, "Value buffer should not be NULL");
	zassert_equal(sensor_obs.size, sizeof(struct test_sensor_data), "Size should match");
	zassert_equal(sensor_obs.source.ops, &weave_observable_ops, "Ops should be set");
}

ZTEST(weave_observable_unit_test, test_observer_define_structure)
{
	/* Verify WEAVE_OBSERVER_DEFINE creates correct structure */
	zassert_not_null(observer_imm_0.handler, "Handler should be set");
	zassert_is_null(observer_imm_0.queue, "Immediate observer has NULL queue");
	zassert_equal(observer_imm_0.user_data, &captures[0], "User data correct");

	zassert_not_null(observer_queued_0.handler, "Handler should be set");
	zassert_equal(observer_queued_0.queue, &obs_queue, "Queued observer has queue");
	zassert_equal(observer_queued_0.user_data, &captures[2], "User data correct");
}

/* =============================================================================
 * Value Persistence Tests
 * =============================================================================
 */

ZTEST(weave_observable_unit_test, test_value_persists)
{
	struct test_sensor_data input = {.temperature = 9999, .humidity = 8888, .timestamp = 7777};
	struct test_sensor_data output1 = {0};
	struct test_sensor_data output2 = {0};

	weave_observable_set_unchecked(&sensor_obs, &input);
	process_all_messages();

	/* Get value multiple times */
	weave_observable_get_unchecked(&sensor_obs, &output1);
	weave_observable_get_unchecked(&sensor_obs, &output2);

	/* Both should be identical */
	zassert_equal(output1.temperature, 9999, "First get correct");
	zassert_equal(output2.temperature, 9999, "Second get correct");
	zassert_mem_equal(&output1, &output2, sizeof(output1), "Both reads identical");
}

ZTEST(weave_observable_unit_test, test_value_overwrites)
{
	struct test_sensor_data input1 = {.temperature = 100, .humidity = 200, .timestamp = 300};
	struct test_sensor_data input2 = {.temperature = 400, .humidity = 500, .timestamp = 600};
	struct test_sensor_data output = {0};

	weave_observable_set_unchecked(&sensor_obs, &input1);
	process_all_messages();
	reset_all_captures();

	weave_observable_set_unchecked(&sensor_obs, &input2);
	process_all_messages();

	weave_observable_get_unchecked(&sensor_obs, &output);

	/* Should have second value */
	zassert_equal(output.temperature, 400, "Temperature overwritten");
	zassert_equal(output.humidity, 500, "Humidity overwritten");
	zassert_equal(output.timestamp, 600, "Timestamp overwritten");
}

/* =============================================================================
 * Simple Value Type Tests
 * =============================================================================
 */

/* Define observable with simple int type */
WEAVE_OBSERVABLE_DEFINE(int_obs, int32_t, WV_NO_HANDLER, WV_IMMEDIATE, NULL, WV_NO_VALID);
WEAVE_OBSERVABLE_DECLARE(int_obs, int32_t);

ZTEST(weave_observable_unit_test, test_simple_int_observable)
{
	int32_t input = 42;
	int32_t output = 0;
	int ret;

	ret = weave_observable_set_unchecked(&int_obs, &input);
	zassert_equal(ret, 0, "Set should succeed (no observers connected)");

	ret = weave_observable_get_unchecked(&int_obs, &output);
	zassert_equal(ret, 0, "Get should succeed");
	zassert_equal(output, 42, "Value should match");

	/* Test type-safe macros */
	input = 123;
	WEAVE_OBSERVABLE_SET(int_obs, &input);
	WEAVE_OBSERVABLE_GET(int_obs, &output);
	zassert_equal(output, 123, "Value should match after macro set");
}

/* =============================================================================
 * Observable Ops Tests
 * =============================================================================
 */

ZTEST(weave_observable_unit_test, test_observable_ref_always_succeeds)
{
	/* Observable source can be connected to multiple sinks,
	 * as ops.ref is NULL, no reference checking is needed
	 */
	zassert_is_null(weave_observable_ops.ref, "Ref should be NULL for observables");
}

ZTEST(weave_observable_unit_test, test_observable_ops_structure)
{
	/* Verify the ops structure - observables use NULL ops for simpler lifecycle */
	zassert_is_null(weave_observable_ops.ref, "Ref function should be NULL");
	zassert_is_null(weave_observable_ops.unref, "Unref should be NULL (no cleanup needed)");
}

/* =============================================================================
 * Owner Handler Tests
 * =============================================================================
 */

static atomic_t owner_handler_count;
static struct test_sensor_data owner_handler_last_value;
static void *owner_handler_last_user_data;

static void owner_handler_fn(struct weave_observable *obs, const void *user_data)
{
	atomic_inc(&owner_handler_count);
	owner_handler_last_user_data = (void *)user_data;
	weave_observable_get_unchecked(obs, &owner_handler_last_value);
}

static int owner_context_value = 42;

/* Observable with immediate owner handler */
WEAVE_OBSERVABLE_DEFINE(owned_obs_imm, struct test_sensor_data, owner_handler_fn, WV_IMMEDIATE,
			&owner_context_value, WV_NO_VALID);

ZTEST(weave_observable_unit_test, test_owner_handler_immediate)
{
	/* Reset state */
	atomic_clear(&owner_handler_count);
	owner_handler_last_user_data = NULL;
	memset(&owner_handler_last_value, 0, sizeof(owner_handler_last_value));

	struct test_sensor_data input = {
		.temperature = 2222,
		.humidity = 3333,
		.timestamp = 4444,
	};

	int ret = weave_observable_set_unchecked(&owned_obs_imm, &input);
	zassert_equal(ret, 0, "Set should succeed (no external observers)");

	/* Owner handler should have been called immediately */
	zassert_equal(atomic_get(&owner_handler_count), 1, "Owner handler should be called");
	zassert_equal(owner_handler_last_user_data, &owner_context_value, "User data correct");
	zassert_equal(owner_handler_last_value.temperature, 2222, "Value correct");
}

/* Message queue for queued owner handler */
WEAVE_MSGQ_DEFINE(owner_queue, 4);

/* Observable with queued owner handler */
WEAVE_OBSERVABLE_DEFINE(owned_obs_queued, struct test_sensor_data, owner_handler_fn, &owner_queue,
			&owner_context_value, WV_NO_VALID);

ZTEST(weave_observable_unit_test, test_owner_handler_queued)
{
	/* Reset state */
	atomic_clear(&owner_handler_count);
	owner_handler_last_user_data = NULL;
	memset(&owner_handler_last_value, 0, sizeof(owner_handler_last_value));
	k_msgq_purge(&owner_queue);

	struct test_sensor_data input = {
		.temperature = 5555,
		.humidity = 6666,
		.timestamp = 7777,
	};

	int ret = weave_observable_set_unchecked(&owned_obs_queued, &input);
	zassert_equal(ret, 0, "Set should succeed");

	/* Owner handler should NOT have been called yet */
	zassert_equal(atomic_get(&owner_handler_count), 0, "Owner handler not yet called");

	/* Process the queue */
	ret = weave_process_messages(&owner_queue, K_NO_WAIT);
	zassert_equal(ret, 1, "Should process 1 message");

	/* Now owner handler should have been called */
	zassert_equal(atomic_get(&owner_handler_count), 1, "Owner handler called after processing");
	zassert_equal(owner_handler_last_value.temperature, 5555, "Value correct");
}

/* =============================================================================
 * Validator Tests
 * =============================================================================
 */

static atomic_t validator_call_count;
static struct test_sensor_data validator_last_value;
static void *validator_last_user_data;

static int accepting_validator(struct weave_observable *obs, const void *new_value, void *user_data)
{
	ARG_UNUSED(obs);
	atomic_inc(&validator_call_count);
	validator_last_user_data = user_data;
	memcpy(&validator_last_value, new_value, sizeof(validator_last_value));
	return 0; /* Accept */
}

static int rejecting_validator(struct weave_observable *obs, const void *new_value, void *user_data)
{
	ARG_UNUSED(obs);
	atomic_inc(&validator_call_count);
	validator_last_user_data = user_data;
	memcpy(&validator_last_value, new_value, sizeof(validator_last_value));
	return -EINVAL; /* Reject */
}

static int validator_context_value = 99;

/* Observable with accepting validator */
WEAVE_OBSERVABLE_DEFINE(validated_obs_accept, struct test_sensor_data, WV_NO_HANDLER, WV_IMMEDIATE,
			&validator_context_value, accepting_validator);

ZTEST(weave_observable_unit_test, test_validator_accepts)
{
	/* Reset state */
	atomic_clear(&validator_call_count);
	validator_last_user_data = NULL;
	memset(&validator_last_value, 0, sizeof(validator_last_value));

	struct test_sensor_data input = {
		.temperature = 1111,
		.humidity = 2222,
		.timestamp = 3333,
	};

	int ret = weave_observable_set_unchecked(&validated_obs_accept, &input);
	zassert_equal(ret, 0, "Set should succeed when validator accepts");

	/* Validator should have been called */
	zassert_equal(atomic_get(&validator_call_count), 1, "Validator should be called");
	zassert_equal(validator_last_user_data, &validator_context_value, "User data correct");
	zassert_equal(validator_last_value.temperature, 1111, "Validator saw correct value");

	/* Value should be updated */
	struct test_sensor_data output;
	weave_observable_get_unchecked(&validated_obs_accept, &output);
	zassert_equal(output.temperature, 1111, "Value should be set");
}

/* Observable with rejecting validator */
WEAVE_OBSERVABLE_DEFINE(validated_obs_reject, struct test_sensor_data, WV_NO_HANDLER, WV_IMMEDIATE,
			&validator_context_value, rejecting_validator);

ZTEST(weave_observable_unit_test, test_validator_rejects)
{
	/* First set a known value */
	struct test_sensor_data initial = {
		.temperature = 100,
		.humidity = 200,
		.timestamp = 300,
	};
	/* Temporarily remove validator to set initial value */
	weave_observable_validator_t orig_validator = validated_obs_reject.validator;
	validated_obs_reject.validator = NULL;
	weave_observable_set_unchecked(&validated_obs_reject, &initial);
	validated_obs_reject.validator = orig_validator;

	/* Reset state */
	atomic_clear(&validator_call_count);

	struct test_sensor_data input = {
		.temperature = 9999,
		.humidity = 8888,
		.timestamp = 7777,
	};

	int ret = weave_observable_set_unchecked(&validated_obs_reject, &input);
	zassert_equal(ret, -EINVAL, "Set should fail when validator rejects");

	/* Validator should have been called */
	zassert_equal(atomic_get(&validator_call_count), 1, "Validator should be called");

	/* Value should NOT be updated */
	struct test_sensor_data output;
	weave_observable_get_unchecked(&validated_obs_reject, &output);
	zassert_equal(output.temperature, 100, "Value should remain unchanged");
}

/* =============================================================================
 * Validator + Handler Combined Tests
 * =============================================================================
 */

static atomic_t combined_handler_count;
static atomic_t combined_validator_count;

static void combined_handler(struct weave_observable *obs, const void *user_data)
{
	ARG_UNUSED(obs);
	ARG_UNUSED(user_data);
	atomic_inc(&combined_handler_count);
}

static int combined_validator(struct weave_observable *obs, const void *new_value, void *user_data)
{
	ARG_UNUSED(obs);
	ARG_UNUSED(user_data);
	const struct test_sensor_data *data = new_value;
	atomic_inc(&combined_validator_count);
	/* Only accept temperature values >= 0 */
	return (data->temperature >= 0) ? 0 : -ERANGE;
}

static int combined_context = 123;

WEAVE_OBSERVABLE_DEFINE(combined_obs, struct test_sensor_data, combined_handler, WV_IMMEDIATE,
			&combined_context, combined_validator);

ZTEST(weave_observable_unit_test, test_validator_and_handler_combined_accept)
{
	atomic_clear(&combined_handler_count);
	atomic_clear(&combined_validator_count);

	struct test_sensor_data input = {
		.temperature = 500, /* Valid: >= 0 */
		.humidity = 50,
		.timestamp = 123,
	};

	int ret = weave_observable_set_unchecked(&combined_obs, &input);
	zassert_equal(ret, 0, "Set should succeed");

	/* Both validator and handler should be called */
	zassert_equal(atomic_get(&combined_validator_count), 1, "Validator called");
	zassert_equal(atomic_get(&combined_handler_count), 1, "Handler called");
}

ZTEST(weave_observable_unit_test, test_validator_and_handler_combined_reject)
{
	atomic_clear(&combined_handler_count);
	atomic_clear(&combined_validator_count);

	struct test_sensor_data input = {
		.temperature = -100, /* Invalid: < 0 */
		.humidity = 50,
		.timestamp = 123,
	};

	int ret = weave_observable_set_unchecked(&combined_obs, &input);
	zassert_equal(ret, -ERANGE, "Set should fail with validator error");

	/* Validator should be called, but handler should NOT */
	zassert_equal(atomic_get(&combined_validator_count), 1, "Validator called");
	zassert_equal(atomic_get(&combined_handler_count), 0, "Handler NOT called on rejection");
}

/* =============================================================================
 * Owner Handler with External Observers Tests
 * =============================================================================
 */

static atomic_t owner_before_external_count;
static atomic_t external_observer_count;

static void owner_before_external_handler(struct weave_observable *obs, const void *user_data)
{
	ARG_UNUSED(obs);
	ARG_UNUSED(user_data);
	atomic_inc(&owner_before_external_count);
}

static void external_observer_handler(struct weave_observable *obs, const void *user_data)
{
	ARG_UNUSED(obs);
	ARG_UNUSED(user_data);
	atomic_inc(&external_observer_count);
}

WEAVE_OBSERVABLE_DEFINE(obs_with_owner_and_external, struct test_sensor_data,
			owner_before_external_handler, WV_IMMEDIATE, NULL, WV_NO_VALID);

WEAVE_OBSERVER_DEFINE(external_obs, external_observer_handler, WV_IMMEDIATE, NULL);
WEAVE_OBSERVER_CONNECT(obs_with_owner_and_external, external_obs);

ZTEST(weave_observable_unit_test, test_owner_handler_and_external_observers)
{
	atomic_clear(&owner_before_external_count);
	atomic_clear(&external_observer_count);

	struct test_sensor_data input = {
		.temperature = 777,
		.humidity = 888,
		.timestamp = 999,
	};

	int ret = weave_observable_set_unchecked(&obs_with_owner_and_external, &input);
	zassert_equal(ret, 1, "Should notify 1 external observer");

	/* Both owner handler and external observer should be called */
	zassert_equal(atomic_get(&owner_before_external_count), 1, "Owner handler called");
	zassert_equal(atomic_get(&external_observer_count), 1, "External observer called");
}

/* =============================================================================
 * Structure Verification for New Fields
 * =============================================================================
 */

ZTEST(weave_observable_unit_test, test_observable_define_with_handler)
{
	/* Verify WEAVE_OBSERVABLE_DEFINE sets owner_sink correctly */
	zassert_not_null(owned_obs_imm.owner_sink.handler, "Handler should be set");
	zassert_is_null(owned_obs_imm.owner_sink.queue, "Immediate mode has NULL queue");
	zassert_equal(owned_obs_imm.owner_sink.user_data, &owner_context_value, "User data set");
	zassert_is_null(owned_obs_imm.validator, "No validator");
}

ZTEST(weave_observable_unit_test, test_observable_define_with_validator)
{
	/* Verify validator is set correctly */
	zassert_is_null(validated_obs_accept.owner_sink.handler, "No handler");
	zassert_not_null(validated_obs_accept.validator, "Validator should be set");
	zassert_equal(validated_obs_accept.owner_sink.user_data, &validator_context_value,
		      "User data set for validator");
}

ZTEST(weave_observable_unit_test, test_observable_define_minimal)
{
	/* Verify minimal observable (no handler, no validator) */
	zassert_is_null(sensor_obs.owner_sink.handler, "No handler");
	zassert_is_null(sensor_obs.validator, "No validator");
	zassert_is_null(sensor_obs.owner_sink.user_data, "No user data");
}

/* =============================================================================
 * Claim/Finish/Publish API Tests
 * =============================================================================
 */

ZTEST(weave_observable_unit_test, test_claim_returns_valid_pointer)
{
	void *ptr = weave_observable_claim(&sensor_obs, K_NO_WAIT);
	zassert_not_null(ptr, "Claim should return valid pointer");
	zassert_equal(ptr, sensor_obs.value, "Pointer should be to value buffer");
	weave_observable_finish(&sensor_obs);
}

ZTEST(weave_observable_unit_test, test_claim_null_obs_returns_null)
{
	void *ptr = weave_observable_claim(NULL, K_NO_WAIT);
	zassert_is_null(ptr, "Claim with NULL observable should return NULL");
}

ZTEST(weave_observable_unit_test, test_claim_timeout_already_locked)
{
	/* First claim succeeds */
	void *ptr1 = weave_observable_claim(&sensor_obs, K_NO_WAIT);
	zassert_not_null(ptr1, "First claim should succeed");

	/* Second claim with K_NO_WAIT should timeout */
	void *ptr2 = weave_observable_claim(&sensor_obs, K_NO_WAIT);
	zassert_is_null(ptr2, "Second claim with K_NO_WAIT should return NULL (timeout)");

	weave_observable_finish(&sensor_obs);
}

ZTEST(weave_observable_unit_test, test_finish_releases_lock)
{
	/* First claim succeeds */
	void *ptr1 = weave_observable_claim(&sensor_obs, K_NO_WAIT);
	zassert_not_null(ptr1, "First claim should succeed");

	/* Finish releases lock */
	weave_observable_finish(&sensor_obs);

	/* Second claim should now succeed */
	void *ptr2 = weave_observable_claim(&sensor_obs, K_NO_WAIT);
	zassert_not_null(ptr2, "Second claim should succeed after finish");
	zassert_equal(ptr2, sensor_obs.value, "Pointer should be to value buffer");

	weave_observable_finish(&sensor_obs);
}

ZTEST(weave_observable_unit_test, test_finish_null_obs_safe)
{
	/* Should not crash with NULL observable */
	weave_observable_finish(NULL);
}

ZTEST(weave_observable_unit_test, test_publish_releases_lock_and_notifies)
{
	reset_all_captures();

	/* Claim the observable */
	void *ptr = weave_observable_claim(&sensor_obs, K_NO_WAIT);
	zassert_not_null(ptr, "Claim should succeed");

	/* Publish should release lock and notify observers */
	int ret = weave_observable_publish(&sensor_obs);
	zassert_equal(ret, 4, "Publish should notify 4 observers");

	/* Immediate observers should be called already */
	zassert_equal(atomic_get(&captures[0].call_count), 1, "Immediate observer 0 called");
	zassert_equal(atomic_get(&captures[1].call_count), 1, "Immediate observer 1 called");

	/* After finish, lock should be released, so claim should succeed */
	void *ptr2 = weave_observable_claim(&sensor_obs, K_NO_WAIT);
	zassert_not_null(ptr2, "Lock should be released after publish");
	weave_observable_finish(&sensor_obs);

	/* Process queued observers */
	process_all_messages();
	zassert_equal(atomic_get(&captures[2].call_count), 1, "Queued observer 0 called");
	zassert_equal(atomic_get(&captures[3].call_count), 1, "Queued observer 1 called");
}

ZTEST(weave_observable_unit_test, test_publish_null_obs_returns_error)
{
	int ret = weave_observable_publish(NULL);
	zassert_equal(ret, -EINVAL, "Publish with NULL observable should return -EINVAL");
}

ZTEST(weave_observable_unit_test, test_publish_without_prior_claim)
{
	reset_all_captures();

	/* Edge case: publish without claiming first (still safe due to K_NO_WAIT in implementation)
	 * This tests that publish doesn't assume a claim was made
	 */
	int ret = weave_observable_publish(&sensor_obs);
	zassert_equal(ret, 4, "Publish should notify observers (if not already locked)");

	/* Observers should be called */
	zassert_equal(atomic_get(&captures[0].call_count), 1, "Immediate observer called");

	process_all_messages();
	zassert_equal(atomic_get(&captures[2].call_count), 1, "Queued observer called");
}

ZTEST(weave_observable_unit_test, test_validate_no_validator_returns_zero)
{
	/* sensor_obs has no validator */
	struct test_sensor_data input = {.temperature = 42, .humidity = 50, .timestamp = 123};
	int ret = weave_observable_validate(&sensor_obs, &input);
	zassert_equal(ret, 0, "Validate with no validator should return 0");
}

ZTEST(weave_observable_unit_test, test_validate_null_obs_returns_error)
{
	struct test_sensor_data input = {.temperature = 42, .humidity = 50, .timestamp = 123};
	int ret = weave_observable_validate(NULL, &input);
	zassert_equal(ret, -EINVAL, "Validate with NULL observable should return -EINVAL");
}

ZTEST(weave_observable_unit_test, test_validate_with_accepting_validator)
{
	/* Reset state */
	atomic_clear(&validator_call_count);
	validator_last_user_data = NULL;
	memset(&validator_last_value, 0, sizeof(validator_last_value));

	struct test_sensor_data input = {
		.temperature = 2000,
		.humidity = 3000,
		.timestamp = 4000,
	};

	int ret = weave_observable_validate(&validated_obs_accept, &input);
	zassert_equal(ret, 0, "Validate should return 0 (accept)");

	/* Validator should have been called */
	zassert_equal(atomic_get(&validator_call_count), 1, "Validator called");
	zassert_equal(validator_last_user_data, &validator_context_value, "User data passed");
	zassert_equal(validator_last_value.temperature, 2000, "Validator received correct value");
}

ZTEST(weave_observable_unit_test, test_validate_with_rejecting_validator)
{
	/* Reset state */
	atomic_clear(&validator_call_count);
	validator_last_user_data = NULL;
	memset(&validator_last_value, 0, sizeof(validator_last_value));

	struct test_sensor_data input = {
		.temperature = -100,
		.humidity = 50,
		.timestamp = 123,
	};

	int ret = weave_observable_validate(&validated_obs_reject, &input);
	zassert_equal(ret, -EINVAL, "Validate should return error (reject)");

	/* Validator should have been called */
	zassert_equal(atomic_get(&validator_call_count), 1, "Validator called");
}

ZTEST(weave_observable_unit_test, test_claim_modify_publish_workflow)
{
	reset_all_captures();

	/* Claim access to modify value in-place */
	struct test_sensor_data *ptr =
		(struct test_sensor_data *)weave_observable_claim(&sensor_obs, K_NO_WAIT);
	zassert_not_null(ptr, "Claim should succeed");

	/* Modify value in-place */
	ptr->temperature = 5000;
	ptr->humidity = 6000;
	ptr->timestamp = 12345;

	/* Publish to notify observers of changes */
	int ret = weave_observable_publish(&sensor_obs);
	zassert_equal(ret, 4, "Publish should notify 4 observers");

	/* Immediate observers should see the modified value */
	zassert_equal(captures[0].last_value.temperature, 5000,
		      "Observer saw modified temperature");
	zassert_equal(captures[0].last_value.humidity, 6000, "Observer saw modified humidity");
	zassert_equal(captures[0].last_value.timestamp, 12345, "Observer saw modified timestamp");

	/* Process queued observers */
	process_all_messages();
	zassert_equal(captures[2].last_value.temperature, 5000,
		      "Queued observer saw modified temperature");
}

ZTEST(weave_observable_unit_test, test_claim_finish_for_read_only_access)
{
	/* Set a known value */
	struct test_sensor_data input = {
		.temperature = 7000,
		.humidity = 8000,
		.timestamp = 9000,
	};
	weave_observable_set_unchecked(&sensor_obs, &input);
	reset_all_captures();

	/* Claim for read-only access */
	struct test_sensor_data *ptr =
		(struct test_sensor_data *)weave_observable_claim(&sensor_obs, K_NO_WAIT);
	zassert_not_null(ptr, "Claim should succeed");

	/* Read the value without modifying */
	struct test_sensor_data read_value = *ptr;
	zassert_equal(read_value.temperature, 7000, "Read temperature correct");

	/* Finish without publish - no observers notified */
	weave_observable_finish(&sensor_obs);

	/* Verify no observers were called */
	zassert_equal(atomic_get(&captures[0].call_count), 0, "No observers notified after finish");
}

ZTEST(weave_observable_unit_test, test_claim_modify_with_validator_then_publish)
{
	atomic_clear(&combined_handler_count);
	atomic_clear(&combined_validator_count);

	/* Claim the observable */
	struct test_sensor_data *ptr =
		(struct test_sensor_data *)weave_observable_claim(&combined_obs, K_NO_WAIT);
	zassert_not_null(ptr, "Claim should succeed");

	/* Modify value in-place with valid data */
	ptr->temperature = 300; /* Valid: >= 0 */
	ptr->humidity = 40;
	ptr->timestamp = 123;

	/* Publish to notify - note: publish doesn't re-validate, it just notifies */
	int ret = weave_observable_publish(&combined_obs);
	zassert_equal(ret, 0,
		      "Publish should notify observers (combined_obs has no external observers)");

	/* Handler should be called from publish */
	zassert_equal(atomic_get(&combined_handler_count), 1, "Handler called from publish");
}

ZTEST(weave_observable_unit_test, test_multiple_claim_finish_cycles)
{
	/* Test multiple claim/finish cycles in sequence */
	for (int i = 0; i < 3; i++) {
		void *ptr = weave_observable_claim(&sensor_obs, K_NO_WAIT);
		zassert_not_null(ptr, "Claim %d should succeed", i);

		struct test_sensor_data *data = (struct test_sensor_data *)ptr;
		data->temperature = i * 100;

		weave_observable_finish(&sensor_obs);
	}
}

ZTEST(weave_observable_unit_test, test_validate_before_manual_update)
{
	/* Use validate API to check if value is acceptable before modifying */
	atomic_clear(&combined_validator_count);

	struct test_sensor_data test_value = {
		.temperature = 100, /* Valid: >= 0 */
		.humidity = 50,
		.timestamp = 123,
	};

	/* Validate the proposed value */
	int ret = weave_observable_validate(&combined_obs, &test_value);
	zassert_equal(ret, 0, "Validate should accept positive temperature");
	zassert_equal(atomic_get(&combined_validator_count), 1, "Validator called");

	/* Now try invalid value */
	atomic_clear(&combined_validator_count);
	test_value.temperature = -50; /* Invalid: < 0 */
	ret = weave_observable_validate(&combined_obs, &test_value);
	zassert_equal(ret, -ERANGE, "Validate should reject negative temperature");
	zassert_equal(atomic_get(&combined_validator_count), 1, "Validator called");
}

ZTEST(weave_observable_unit_test, test_claim_timeout_with_k_forever)
{
	/* First claim succeeds */
	void *ptr1 = weave_observable_claim(&isolated_obs, K_NO_WAIT);
	zassert_not_null(ptr1, "First claim should succeed");

	/* Note: We cannot effectively test K_FOREVER blocking in a single thread without async
	 * operations. This test documents the API but actual timeout behavior needs multi-threaded
	 * testing. For now, verify K_NO_WAIT case works as expected.
	 */

	weave_observable_finish(&isolated_obs);
}

ZTEST(weave_observable_unit_test, test_publish_returns_observer_count)
{
	reset_all_captures();

	/* isolated_obs has no observers, should return 0 from publish */
	void *ptr = weave_observable_claim(&isolated_obs, K_NO_WAIT);
	zassert_not_null(ptr, "Claim should succeed");

	int ret = weave_observable_publish(&isolated_obs);
	zassert_equal(ret, 0, "Publish on observable with no external observers should return 0");
}

ZTEST(weave_observable_unit_test, test_claim_get_pointer_matches_observable_value)
{
	/* Verify claim returns exactly the observable's value pointer */
	void *claimed_ptr = weave_observable_claim(&sensor_obs, K_NO_WAIT);
	zassert_not_null(claimed_ptr, "Claim should succeed");
	zassert_equal(claimed_ptr, sensor_obs.value,
		      "Claimed pointer should match observable value");
	weave_observable_finish(&sensor_obs);
}
