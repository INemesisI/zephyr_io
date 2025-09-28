/*
 * Packet I/O Sample - Validator Module Implementation
 *
 * Self-contained packet validator checking integrity and content
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/buf.h>
#include <zephyr_io/flow/flow.h>

#include "sensors.h" /* For SOURCE_ID_SENSOR1/2 */
#include "validator.h"

LOG_MODULE_REGISTER(validator, LOG_LEVEL_INF);

/* Forward declaration of handler function */
static void validator_handler(struct flow_sink *sink, struct net_buf *buf);

/* Define routed validator sinks with immediate execution - filter by packet ID
 */
FLOW_SINK_DEFINE_ROUTED_IMMEDIATE(validator1_sink, validator_handler, SOURCE_ID_SENSOR1, NULL);
FLOW_SINK_DEFINE_ROUTED_IMMEDIATE(validator2_sink, validator_handler, SOURCE_ID_SENSOR2, NULL);

/* Validator context with per-sensor statistics */
struct validator_ctx {
	uint8_t sensor_id;
	const char *name;
	uint32_t validated;
	uint32_t failed;
	bool first_packet;
	uint8_t expected_pattern;
	size_t expected_size;
	struct flow_sink *expected_sink; /* To verify correct sink is called */
};

/* Context for each validator */
static struct validator_ctx sensor1_ctx = {.sensor_id = SOURCE_ID_SENSOR1,
					   .name = "SENSOR1",
					   .first_packet = true,
					   .expected_pattern = 0xA1, /* Sensor 1 sends all 0xA1 */
					   .expected_size = 256,
					   .expected_sink = &validator1_sink};

static struct validator_ctx sensor2_ctx = {.sensor_id = SOURCE_ID_SENSOR2,
					   .name = "SENSOR2",
					   .first_packet = true,
					   .expected_pattern = 0xB2, /* Sensor 2 sends all 0xB2 */
					   .expected_size = 384,
					   .expected_sink = &validator2_sink};

/* Now update the sinks to use the contexts */
static void __attribute__((constructor)) init_sink_contexts(void)
{
	validator1_sink.user_data = &sensor1_ctx;
	validator2_sink.user_data = &sensor2_ctx;
}

/* Common validation handler */
static void validator_handler(struct flow_sink *sink, struct net_buf *buf)
{
	struct validator_ctx *ctx = (struct validator_ctx *)sink->user_data;
	size_t data_len = net_buf_frags_len(buf);

	/* Verify correct sink is being called */
	if (sink != ctx->expected_sink) {
		LOG_ERR("%s: Wrong sink called! Expected %p, got %p", ctx->name, ctx->expected_sink,
			sink);
		return;
	}

	/* Log startup message on first packet (required for test harness) */
	if (ctx->first_packet) {
		LOG_INF("Sensor %d validator started (immediate mode, filtering ID=0x%02x)",
			ctx->sensor_id, ctx->sensor_id);
		ctx->first_packet = false;
	}

	/* Validate packet size and content */
	bool valid = true;

	/* Check size first */
	if (data_len != ctx->expected_size) {
		LOG_ERR("%s: Size mismatch: Expected %zu, Got %zu", ctx->name, ctx->expected_size,
			data_len);
		valid = false;
	}

	/* Check payload content - verify all bytes match the pattern */
	for (size_t i = 0; i < buf->len; i++) {
		if (buf->data[i] != ctx->expected_pattern) {
			LOG_ERR("%s: Wrong byte at %zu: Expected 0x%02x, Got 0x%02x", ctx->name, i,
				ctx->expected_pattern, buf->data[i]);
			valid = false;
			break;
		}
	}

	/* Update statistics and log result */
	if (valid) {
		ctx->validated++;
		LOG_INF("%s VALID: %zu bytes [Total: %d valid, %d failed]", ctx->name, data_len,
			ctx->validated, ctx->failed);
	} else {
		ctx->failed++;
		LOG_ERR("%s INVALID: %zu bytes [Total: %d valid, %d failed]", ctx->name, data_len,
			ctx->validated, ctx->failed);
	}
}

/* No thread needed for immediate handler - executes in source context */