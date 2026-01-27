/*
 * Copyright (c) 2026 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <weave/method.h>
#include "sensor_module.h"

LOG_MODULE_REGISTER(sensor, LOG_LEVEL_INF);

/* ============================ Sensor Context ============================ */

struct sensor_context {
	struct set_config_request config;
	struct get_stats_response stats;
	struct k_timer sample_timer;
	struct k_sem data_ready;
	int32_t last_value;
};

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

/* Stop flag for clean shutdown */
static volatile bool sensor_running = true;

void sensor_stop(void)
{
	sensor_running = false;
}

/* ============================ Message Queue ============================ */

/* Define message queue for sensor methods (10 messages max) */
WEAVE_MSGQ_DEFINE(sensor_msgq, 10);

/* Thread automatically started at boot */
K_THREAD_DEFINE(sensor_thread_id, 2048, sensor_thread, NULL, NULL, NULL, 5, 0, 0);

/* ============================ Timer Callback ============================ */

static void sensor_timer_expired(struct k_timer *timer)
{
	k_sem_give(&sensor_ctx.data_ready);
}

/* ============================ Method Handlers ============================ */

static int read_sensor_handler(const struct read_sensor_request *request,
			       struct read_sensor_response *res, void *user_data)
{
	struct sensor_context *ctx = &sensor_ctx;

	if (!request) {
		LOG_ERR("read_sensor: request is required");
		return -EINVAL;
	}

	/* Simulate sensor reading */
	int32_t value = (k_uptime_get_32() / 100) % 200;

	if (res) {
		res->value = value;
		res->timestamp = k_uptime_get_32();
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

	/* Check threshold (just log, no signal) */
	if (value > ctx->config.threshold) {
		ctx->stats.threshold_events++;
		LOG_WRN("Threshold exceeded: value=%d > %d", value, ctx->config.threshold);
	}

	return 0;
}

static int set_config_handler(const struct set_config_request *request, void *res, void *user_data)
{
	struct sensor_context *ctx = &sensor_ctx;

	ARG_UNUSED(res);

	if (!request) {
		LOG_ERR("set_config: request is required");
		return -EINVAL;
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

static int get_stats_handler(const void *request, struct get_stats_response *res, void *user_data)
{
	struct sensor_context *ctx = &sensor_ctx;

	if (res) {
		*res = ctx->stats;
		LOG_INF("Stats: reads=%u, threshold_events=%u", res->total_reads,
			res->threshold_events);
	}

	return 0;
}

/* ============================ Method Definitions ============================ */

/* Define methods with queued execution */
WEAVE_METHOD_DEFINE(sensor_read_sensor, read_sensor_handler, &sensor_msgq, NULL,
		    struct read_sensor_request, struct read_sensor_response);

WEAVE_METHOD_DEFINE(sensor_set_config, set_config_handler, &sensor_msgq, NULL,
		    struct set_config_request, WV_VOID);

WEAVE_METHOD_DEFINE(sensor_get_stats, get_stats_handler, &sensor_msgq, NULL, WV_VOID,
		    struct get_stats_response);

/* ============================ Sensor Thread ============================ */

void sensor_thread(void *p1, void *p2, void *p3)
{
	struct sensor_context *ctx = &sensor_ctx;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("Sensor thread started");

	/* Initialize timer and semaphore */
	k_timer_init(&ctx->sample_timer, sensor_timer_expired, NULL);
	k_sem_init(&ctx->data_ready, 0, 1);

	/* Start auto-sampling if enabled */
	if (ctx->config.auto_sample) {
		k_timer_start(&ctx->sample_timer, K_MSEC(ctx->config.sample_rate_ms),
			      K_MSEC(ctx->config.sample_rate_ms));
	}

	/* Process messages and timer events */
	while (sensor_running) {
		struct k_poll_event events[] = {
			K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY,
						 &ctx->data_ready),
			K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
						 K_POLL_MODE_NOTIFY_ONLY, &sensor_msgq),
		};

		k_poll(events, ARRAY_SIZE(events), K_MSEC(100));

		/* Handle timer expiry (auto-sample) */
		if (events[0].state == K_POLL_STATE_SEM_AVAILABLE) {
			k_sem_take(&ctx->data_ready, K_NO_WAIT);

			struct read_sensor_request req = {.channel = 0};
			struct read_sensor_response res;

			LOG_INF("Auto-sample triggered");
			read_sensor_handler(&req, &res, NULL);
		}

		/* Process RPC method calls */
		if (events[1].state == K_POLL_STATE_MSGQ_DATA_AVAILABLE) {
			weave_process_messages(&sensor_msgq, K_NO_WAIT);
		}

		/* Reset poll states */
		events[0].state = K_POLL_STATE_NOT_READY;
		events[1].state = K_POLL_STATE_NOT_READY;
	}

	/* Cleanup */
	k_timer_stop(&ctx->sample_timer);
	LOG_INF("Sensor thread stopped");
}
