/*
 * Copyright (c) 2024 Zephyr IO Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include "sensor_module.h"

LOG_MODULE_REGISTER(sensor, LOG_LEVEL_INF);

/* Sensor context */
struct sensor_context {
	/* Module's own functionality */
	struct set_config_request config;
	struct get_stats_reply stats;
	struct k_timer sample_timer;
	struct k_sem data_ready;
	int32_t last_value;
};

/* Static sensor context */
static struct sensor_context sensor_ctx = {
	.config =
		{
			.sample_rate_ms = 1000,
			.threshold = 100,
			.auto_sample = true,
		},
	.stats =
		{
			.min_value = INT32_MAX,
			.max_value = INT32_MIN,
		},
};

/* Define message queue for sensor (10 messages max) */
WEAVE_MSGQ_DEFINE(sensor_msgq, 10);

/* Thread automatically started at boot */
K_THREAD_DEFINE(sensor_thread_id, 2048,
		sensor_thread, NULL, NULL, NULL,
		5, 0, 0);

/* Define sensor module */
WEAVE_MODULE_DEFINE(sensor_module, &sensor_msgq);

/* Define signal for threshold events */
WEAVE_SIGNAL_DEFINE(threshold_exceeded, struct threshold_exceeded_event);

/* Sensor timer callback - runs in ISR context */
static void sensor_timer_expired(struct k_timer *timer)
{
	/* Signal main thread that it's time to sample */
	k_sem_give(&sensor_ctx.data_ready);
}

/* Method implementations */

static int read_sensor_handler(struct weave_module *module,
			       const struct read_sensor_request *request,
			       struct read_sensor_reply *reply)
{
	struct sensor_context *ctx = &sensor_ctx;

	/* Request is required for channel information */
	if (!request) {
		LOG_ERR("read_sensor: request is required");
		return -EINVAL;
	}

	/* Simulate sensor reading */
	int32_t value = (k_uptime_get_32() / 100) % 200;

	/* Only fill reply if provided */
	if (reply) {
		reply->value = value;
		reply->timestamp = k_uptime_get_32();
	}

	/* Update statistics */
	ctx->stats.total_reads++;
	if (value < ctx->stats.min_value) {
		ctx->stats.min_value = value;
	}
	if (value > ctx->stats.max_value) {
		ctx->stats.max_value = value;
	}

	ctx->last_value = value;

	LOG_INF("Sensor read ch%u: value=%d", request->channel, value);

	/* Check threshold */
	if (value > ctx->config.threshold) {
		struct threshold_exceeded_event evt = {
			.value = value,
			.threshold = ctx->config.threshold,
			.timestamp = reply ? reply->timestamp : k_uptime_get_32(),
		};

		ctx->stats.threshold_events++;
		LOG_INF("THRESHOLD EXCEEDED! value=%d > %d", value, ctx->config.threshold);

		/* Emit signal */
		weave_emit_signal(&threshold_exceeded, &evt);
	}

	return 0;
}

static int set_config_handler(struct weave_module *module, const struct set_config_request *request,
			      struct set_config_reply *reply)
{
	struct sensor_context *ctx = &sensor_ctx;

	/* Request is required - we need the new configuration */
	if (!request) {
		LOG_ERR("set_config: request is required");
		return -EINVAL;
	}

	/* Return old config */
	if (reply) {
		reply->sample_rate_ms = ctx->config.sample_rate_ms;
		reply->threshold = ctx->config.threshold;
		reply->auto_sample = ctx->config.auto_sample;
	}

	/* Apply new config */
	ctx->config = *request;

	/* Update timer if sample rate changed */
	if (ctx->config.auto_sample) {
		k_timer_start(&ctx->sample_timer, K_MSEC(ctx->config.sample_rate_ms),
			      K_MSEC(ctx->config.sample_rate_ms));
		LOG_INF("Auto-sampling enabled at %u ms", ctx->config.sample_rate_ms);
	} else {
		k_timer_stop(&ctx->sample_timer);
		LOG_INF("Auto-sampling disabled");
	}

	LOG_INF("Config updated: threshold=%d", ctx->config.threshold);

	return 0;
}

static int get_stats_handler(struct weave_module *module, const struct get_stats_request *request,
			     struct get_stats_reply *reply)
{
	struct sensor_context *ctx = &sensor_ctx;

	/* Reply is typically expected for stats, but we'll allow NULL
	 * in case someone just wants to trigger a log */
	if (reply) {
		*reply = ctx->stats;
		LOG_INF("Stats: reads=%u, threshold_events=%u", reply->total_reads,
			reply->threshold_events);
	}

	return 0;
}

/* Register methods with module prefix */
WEAVE_METHOD_REGISTER(sensor_read_sensor, read_sensor_handler, struct read_sensor_request,
		      struct read_sensor_reply);

WEAVE_METHOD_REGISTER(sensor_set_config, set_config_handler, struct set_config_request,
		      struct set_config_reply);

WEAVE_METHOD_REGISTER(sensor_get_stats, get_stats_handler, struct get_stats_request,
		      struct get_stats_reply);

/* Sensor thread - handles multiple event sources */
void sensor_thread(void *p1, void *p2, void *p3)
{
	struct sensor_context *ctx = &sensor_ctx;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("Sensor thread started");

	/* Set method owners */
	sensor_read_sensor.module = &sensor_module;
	sensor_set_config.module = &sensor_module;
	sensor_get_stats.module = &sensor_module;

	/* Initialize timer */
	k_timer_init(&ctx->sample_timer, sensor_timer_expired, NULL);
	k_sem_init(&ctx->data_ready, 0, 1);

	/* Start auto-sampling if enabled */
	if (ctx->config.auto_sample) {
		k_timer_start(&ctx->sample_timer, K_MSEC(ctx->config.sample_rate_ms),
			      K_MSEC(ctx->config.sample_rate_ms));
	}

	/* Setup polling for multiple event sources */
	struct k_poll_event events[] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_MSGQ_DATA_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY,
					 &sensor_msgq),
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY,
					 &ctx->data_ready),
	};

	/* Main event loop */
	while (1) {
		/* Wait for any event with timeout for periodic tasks */
		int ret = k_poll(events, ARRAY_SIZE(events), K_MSEC(5000));

		/* Handle Weave messages */
		if (events[0].state == K_POLL_STATE_MSGQ_DATA_AVAILABLE) {
			struct weave_message *msg;

			while (k_msgq_get(&sensor_msgq, &msg, K_NO_WAIT) == 0) {
				LOG_DBG("Processing Weave message");
				weave_process_message(&sensor_module, msg);
			}

			events[0].state = K_POLL_STATE_NOT_READY;
		}

		/* Handle timer-triggered sampling */
		if (events[1].state == K_POLL_STATE_SEM_AVAILABLE) {
			k_sem_take(&ctx->data_ready, K_NO_WAIT);

			LOG_DBG("Auto-sampling triggered");

			/* Perform automatic reading */
			struct read_sensor_request req = {.channel = 0};
			struct read_sensor_reply rep;

			/* Call method directly since we're in the same module */
			read_sensor_handler(&sensor_module, &req, &rep);

			events[1].state = K_POLL_STATE_NOT_READY;
		}

		/* Periodic housekeeping (every 5 seconds or on events) */
		if (ret == -EAGAIN) {
			LOG_DBG("Sensor health check");
			/* Could check hardware status, etc. */
		}
	}
}