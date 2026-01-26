/*
 * Copyright (c) 2026 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Weave Method public API
 *
 * Weave Method - Remote Procedure Call framework
 *
 * The Weave Method subsystem provides type-safe RPC with zero-allocation
 * call semantics. Built on Weave for message transport.
 */

#ifndef ZEPHYR_INCLUDE_WEAVE_METHOD_H_
#define ZEPHYR_INCLUDE_WEAVE_METHOD_H_

#include <weave/core.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util_macro.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup weave_method_apis Weave Method APIs
 * @ingroup os_services
 * @{
 */

/* ============================ Type Definitions ============================ */

struct weave_method;

/**
 * @brief Method handler function type
 *
 * @param request Pointer to request data
 * @param response Pointer to response buffer
 * @param user_data User data from method definition
 * @return 0 on success, negative errno on error
 */
typedef int (*weave_method_handler_t)(const void *request, void *response, void *user_data);

/**
 * @brief Method call context - lives on caller's stack
 *
 * Contains pointers to caller-owned data. Safe because caller blocks
 * until handler completes.
 */
struct weave_method_context {
	/** Completion semaphore */
	struct k_sem completion;
	/** Handler result */
	int result;
	/** Pointer to request data (caller's stack) */
	const void *request;
	/** Pointer to response buffer (caller's stack) */
	void *response;
};

/**
 * @brief Method definition
 *
 * Represents a callable RPC endpoint. Each method has an embedded
 * Weave sink for message transport.
 */
struct weave_method {
	/** Embedded sink for message transport */
	struct weave_sink sink;
	/** User's handler function */
	weave_method_handler_t handler;
	/** User data passed to handler */
	void *user_data;
	/** Expected request size (for validation) */
	size_t request_size;
	/** Expected response size (for validation) */
	size_t response_size;
};

/* ============================ Macros ============================ */

/**
 * @brief Void type marker for methods with no request or response
 *
 * Use WV_VOID as the type parameter in WEAVE_METHOD_DEFINE/DECLARE
 * when a method has no request or no response.
 *
 * Example:
 *   WEAVE_METHOD_DEFINE(get_stats, handler, &queue, NULL, WV_VOID, struct stats_response);
 *   WEAVE_METHOD_CALL(get_stats, WV_VOID, &response);
 */
#define WV_VOID /* empty - detected by IS_EMPTY() */

/** @brief Return 0 if empty token, otherwise sizeof(_type) */
#define WEAVE_TYPE_SIZE(_type) COND_CODE_1(IS_EMPTY(_type), ((size_t)0), (sizeof(_type)))

/** @brief Return NULL if empty token, otherwise the value */
#define WEAVE_PTR_OR_NULL(_val) COND_CODE_1(IS_EMPTY(_val), (NULL), (_val))

/** @brief Return 0 if empty token, otherwise sizeof(*_val) */
#define WEAVE_SIZE_OF_PTR(_val) COND_CODE_1(IS_EMPTY(_val), ((size_t)0), (sizeof(*(_val))))

/** @brief Generate typedef if type is not empty */
#define WEAVE_TYPEDEF_IF(_type, _alias) COND_CODE_1(IS_EMPTY(_type), (), (typedef _type _alias;))

/**
 * @brief Declare a method (in header file)
 *
 * Creates typedefs for request/response types enabling compile-time checking.
 * Use WV_VOID for _req_type or _res_type when the method has no request or response.
 *
 * @param _name Method name
 * @param _req_type Request struct type, or WV_VOID if none
 * @param _res_type Response struct type, or WV_VOID if none
 */
#define WEAVE_METHOD_DECLARE(_name, _req_type, _res_type)                                          \
	WEAVE_TYPEDEF_IF(_req_type, _name##_req_t)                                                 \
	WEAVE_TYPEDEF_IF(_res_type, _name##_res_t)                                                 \
	extern struct weave_method _name

/**
 * @brief Type-safe method call macro
 *
 * Automatically derives sizes from passed pointers. Blocks until handler
 * completes - this is synchronous RPC semantics.
 *
 * Use WV_VOID for _req or _res when the method has no request or response.
 *
 * @param _method Method name (not pointer)
 * @param _req Pointer to request data, or WV_VOID if none
 * @param _res Pointer to response buffer, or WV_VOID if none
 * @return 0 on success, negative errno on error
 */
#define WEAVE_METHOD_CALL(_method, _req, _res)                                                     \
	weave_method_call_unchecked(&_method, WEAVE_PTR_OR_NULL(_req), WEAVE_SIZE_OF_PTR(_req),    \
				    WEAVE_PTR_OR_NULL(_res), WEAVE_SIZE_OF_PTR(_res))

/**
 * @brief Type-safe async method call macro
 *
 * Queues the method call but returns immediately without waiting for
 * completion. Use WEAVE_METHOD_WAIT() to wait for the result.
 *
 * The context must remain valid until wait completes. Request and response
 * buffers must also remain valid as they are accessed by the handler.
 *
 * @param _method Method name (not pointer)
 * @param _req Pointer to request data, or WV_VOID if none
 * @param _res Pointer to response buffer, or WV_VOID if none
 * @param _ctx Pointer to weave_method_context (caller-owned)
 * @return 0 if queued successfully, negative errno on error
 */
#define WEAVE_METHOD_CALL_ASYNC(_method, _req, _res, _ctx)                                         \
	weave_method_call_async(&_method, WEAVE_PTR_OR_NULL(_req), WEAVE_SIZE_OF_PTR(_req),        \
				WEAVE_PTR_OR_NULL(_res), WEAVE_SIZE_OF_PTR(_res), (_ctx))

/**
 * @brief Wait for async method call completion
 *
 * Blocks until the method handler completes and returns the handler's result.
 *
 * @param _ctx Pointer to weave_method_context from WEAVE_METHOD_CALL_ASYNC
 * @param _timeout Wait timeout (K_FOREVER, K_NO_WAIT, or K_MSEC(n))
 * @return Handler result on success, -EAGAIN on timeout
 */
#define WEAVE_METHOD_WAIT(_ctx, _timeout) weave_method_wait((_ctx), (_timeout))

/**
 * @brief Define a method with queued execution
 *
 * Handler runs in the processing thread that calls weave_process_message().
 * Use WV_VOID for _req_type or _res_type when the method has no request or response.
 *
 * @param _name Method variable name
 * @param _handler Handler function
 * @param _queue Message queue (WV_IMMEDIATE for immediate mode, or &queue for queued)
 * @param _user_data User data passed to handler (or NULL)
 * @param _req_type Request struct type, or WV_VOID if none
 * @param _res_type Response struct type, or WV_VOID if none
 */
#define WEAVE_METHOD_DEFINE(_name, _handler, _queue, _user_data, _req_type, _res_type)             \
	struct weave_method _name = {                                                              \
		.sink = WEAVE_SINK_INITIALIZER(weave_method_dispatch, (_queue), &_name),           \
		.handler = (weave_method_handler_t)(_handler),                                     \
		.user_data = (_user_data),                                                         \
		.request_size = WEAVE_TYPE_SIZE(_req_type),                                        \
		.response_size = WEAVE_TYPE_SIZE(_res_type),                                       \
	}

/* ============================ Function APIs ============================ */

/**
 * @brief Call a method (unchecked - no type checking)
 *
 * @note Prefer using WEAVE_METHOD_CALL() macro for compile-time type safety.
 *
 * Blocks until the handler completes - this is synchronous RPC semantics.
 *
 * @param method Pointer to method
 * @param request Pointer to request data
 * @param request_size Size of request data
 * @param response Pointer to response buffer
 * @param response_size Size of response buffer
 * @return Handler result on success, negative errno on error
 */
int weave_method_call_unchecked(struct weave_method *method, const void *request,
				size_t request_size, void *response, size_t response_size);

/**
 * @brief Call a method asynchronously (unchecked - no type checking)
 *
 * @note Prefer using WEAVE_METHOD_CALL_ASYNC() macro for compile-time type safety.
 *
 * Queues the method call but returns immediately. The caller must call
 * weave_method_wait() to wait for completion and get the result.
 *
 * The context, request, and response buffers must remain valid until
 * weave_method_wait() returns.
 *
 * @param method Pointer to method
 * @param request Pointer to request data
 * @param request_size Size of request data
 * @param response Pointer to response buffer
 * @param response_size Size of response buffer
 * @param ctx Caller-owned context (will be initialized)
 * @return 0 if queued successfully, negative errno on error
 */
int weave_method_call_async(struct weave_method *method, const void *request, size_t request_size,
			    void *response, size_t response_size, struct weave_method_context *ctx);

/**
 * @brief Wait for async method call completion
 *
 * Blocks until the handler completes or timeout expires.
 *
 * @param ctx Context from weave_method_call_async()
 * @param timeout Wait timeout (K_FOREVER for indefinite, K_NO_WAIT for poll)
 * @return Handler result on success, -EAGAIN on timeout
 */
int weave_method_wait(struct weave_method_context *ctx, k_timeout_t timeout);

/**
 * @brief Global dispatch function for Weave methods
 *
 * Called by Weave when a call context arrives at the method's sink.
 * Do not call directly.
 *
 * @param ptr Pointer to weave_method_context
 * @param user_data Pointer to weave_method (const)
 */
void weave_method_dispatch(void *ptr, void *user_data);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_WEAVE_METHOD_H_ */
