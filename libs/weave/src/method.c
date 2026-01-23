/*
 * Copyright (c) 2026 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <weave/method.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(weave_method, CONFIG_WEAVE_LOG_LEVEL);

/* ============================ Public API ============================ */

int weave_method_call_unchecked(struct weave_method *method, const void *request,
				size_t request_size, void *response, size_t response_size)
{
	if (!method) {
		return -EINVAL;
	}

	/* Validate sizes (NULL allowed when expected size is 0) */
	if (method->request_size > 0 && request_size < method->request_size) {
		return -EINVAL;
	}
	if (method->response_size > 0 && response_size < method->response_size) {
		return -EINVAL;
	}

	/* Initialize call context on caller's stack */
	struct weave_method_context ctx = {
		.request = request,
		.response = response,
		.result = 0,
	};
	k_sem_init(&ctx.completion, 0, 1);

	/* Send to method's sink - blocks forever for queue admission */
	int ret = weave_sink_send(&method->sink, &ctx, K_FOREVER);
	if (ret != 0) {
		LOG_DBG("Queue admission failed: %d", ret);
		return ret;
	}

	/* Wait forever for handler completion (safe by design - caller blocks) */
	k_sem_take(&ctx.completion, K_FOREVER);

	LOG_DBG("Call completed with result %d", ctx.result);

	return ctx.result;
}

int weave_method_call_async(struct weave_method *method, const void *request, size_t request_size,
			    void *response, size_t response_size, struct weave_method_context *ctx)
{
	if (!method || !ctx) {
		return -EINVAL;
	}

	/* Validate sizes (NULL allowed when expected size is 0) */
	if (method->request_size > 0 && request_size < method->request_size) {
		return -EINVAL;
	}
	if (method->response_size > 0 && response_size < method->response_size) {
		return -EINVAL;
	}

	/* Initialize caller-provided context */
	ctx->request = request;
	ctx->response = response;
	ctx->result = 0;
	k_sem_init(&ctx->completion, 0, 1);

	/* Send to method's sink - blocks forever for queue admission */
	int ret = weave_sink_send(&method->sink, ctx, K_FOREVER);
	if (ret != 0) {
		LOG_DBG("Queue admission failed: %d", ret);
		return ret;
	}

	LOG_DBG("Async call queued successfully");
	return 0;
}

int weave_method_wait(struct weave_method_context *ctx, k_timeout_t timeout)
{
	if (!ctx) {
		return -EINVAL;
	}

	int ret = k_sem_take(&ctx->completion, timeout);
	if (ret != 0) {
		LOG_DBG("Wait timed out");
		return -EAGAIN;
	}

	LOG_DBG("Async call completed with result %d", ctx->result);
	return ctx->result;
}

void weave_method_dispatch(void *ptr, void *user_data)
{
	struct weave_method *method = user_data;
	struct weave_method_context *ctx = ptr;

	if (!method || !ctx) {
		if (ctx) {
			ctx->result = -EINVAL;
			k_sem_give(&ctx->completion);
		}
		return;
	}

	/* Call the user's handler */
	ctx->result = method->handler(ctx->request, ctx->response, method->user_data);

	/* Signal completion to the waiting caller */
	k_sem_give(&ctx->completion);
}
