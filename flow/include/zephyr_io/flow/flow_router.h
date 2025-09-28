/*
 * Copyright (c) 2024 Zephyr Project
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Generic Packet Router Framework
 *
 * This framework provides a protocol-agnostic packet routing engine that can be
 * used to implement any protocol requiring header addition/removal and packet
 * routing based on packet IDs.
 *
 * Key features:
 * - Zero-copy operation using net_buf reference counting
 * - Compile-time route registration via iterable sections
 * - Bidirectional packet transformation (add/strip headers)
 * - Protocol-agnostic design - implement any header format
 * - Statistics tracking for monitoring and debugging
 *
 * Usage:
 * 1. Define a router instance with ROUTER_DEFINE()
 * 2. Implement protocol-specific inbound/outbound handlers
 * 3. Register routes with ROUTER_INBOUND/OUTBOUND_ROUTE_DEFINE()
 * 4. Initialize with router_init() during system startup
 */

#ifndef ZEPHYR_INCLUDE_FLOW_ROUTER_H_
#define ZEPHYR_INCLUDE_FLOW_ROUTER_H_

#include <zephyr/kernel.h>
#include <zephyr/net/buf.h>
#include <zephyr_io/flow/flow.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/logging/log.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup packet_router_apis Packet Router APIs
 * @ingroup networking
 * @{
 */

/* Forward declarations */
struct packet_router;

/**
 * @typedef router_outbound_handler_t
 * @brief Outbound packet handler function signature
 *
 * Called by the framework when an application packet needs a protocol
 * header added before transmission.
 *
 * @param router Router instance
 * @param buf Packet buffer to process (payload only)
 * @param packet_id Packet ID to include in header
 */
typedef void (*router_outbound_handler_t)(struct packet_router *router, struct net_buf *buf,
					  uint16_t packet_id);

/**
 * @struct router_inbound_route
 * @brief Inbound packet route registration (network → application)
 *
 * Maps a packet ID from the network to an application sink.
 * When a packet with matching ID arrives, its payload (header stripped)
 * is forwarded to the specified sink.
 */
struct router_inbound_route {
	struct packet_router *router_ptr; /**< Associated router instance */
	uint16_t packet_id;               /**< Packet ID to match */
	struct flow_sink *app_sink_ptr; /**< Destination sink for payload */
	sys_snode_t node;                 /**< List node for route table */
};

/**
 * @struct router_outbound_route
 * @brief Outbound packet route registration (application → network)
 *
 * Associates an application source with a packet ID. When the source
 * sends a packet, the router adds a protocol header with the specified
 * packet ID before forwarding to the network.
 */
struct router_outbound_route {
	struct packet_router *router_ptr;     /**< Associated router instance */
	uint16_t packet_id;                   /**< Packet ID to add in header */
	struct flow_source *app_source_ptr; /**< Source being monitored */
	struct flow_sink handler_sink;      /**< Internal sink for processing */
	sys_snode_t node;                     /**< List node for route table */
};

/**
 * @struct router_route_list
 * @brief Thread-safe container for packet routes
 *
 * Maintains a list of routes with spinlock protection for
 * concurrent access from multiple contexts.
 */
struct router_route_list {
	sys_slist_t list;       /**< Single-linked list of routes */
	struct k_spinlock lock; /**< Spinlock for thread safety */
};

/**
 * @struct packet_router
 * @brief Packet router instance
 *
 * Core router structure containing routing tables, handlers, and statistics.
 * Each router instance handles one protocol with its own header format.
 * Multiple routers can coexist for different protocols.
 *
 * The router acts as both a sink (for network packets) and a source
 * (for forwarding processed packets).
 */
struct packet_router {
#ifdef CONFIG_FLOW_NAMES
	const char *name; /**< Debug name */
#endif
	/* Network interface */
	struct flow_sink network_sink;     /**< Sink for inbound packets */
	struct flow_source network_source; /**< Source for outbound packets */

	/* Protocol handler */
	router_outbound_handler_t outbound_handler; /**< Add header function */

	/* Routing tables */
	struct router_route_list inbound_routes;  /**< Network → app routes */
	struct router_route_list outbound_routes; /**< App → network routes */

#ifdef CONFIG_FLOW_STATS
	/* Statistics counters */
	atomic_t inbound_packets;    /**< Total inbound packets processed */
	atomic_t outbound_packets;   /**< Total outbound packets processed */
	atomic_t unknown_packet_ids; /**< Packets with unregistered IDs */
	atomic_t parse_errors;       /**< Protocol parsing failures */
	atomic_t buffer_errors;      /**< Buffer allocation failures */
#endif
};

/**
 * @brief Register inbound route (network → application)
 *
 * Maps a packet ID to an application sink for inbound routing.
 *
 * @param _router Router instance name
 * @param _packet_id Packet ID to handle (e.g., 1, 9, 500)
 * @param _app_sink Application sink to receive payloads
 */
#define ROUTER_INBOUND_ROUTE_DEFINE(_router, _packet_id, _app_sink)                                \
	static STRUCT_SECTION_ITERABLE(router_inbound_route,                                       \
				       __inbound_##_router##_##_packet_id) = {                     \
		.router_ptr = &_router,                                                            \
		.packet_id = _packet_id,                                                           \
		.app_sink_ptr = &_app_sink,                                                        \
		.node = {NULL},                                                                    \
	}

/**
 * @brief Register outbound route (application → network)
 *
 * Associates an application source with a packet ID for outbound routing.
 *
 * @param _router Router instance name
 * @param _packet_id Packet ID for outbound packets
 * @param _app_source Application source to monitor
 */
#define ROUTER_OUTBOUND_ROUTE_DEFINE(_router, _packet_id, _app_source)                             \
	static STRUCT_SECTION_ITERABLE(router_outbound_route,                                      \
				       __outbound_##_router##_##_packet_id) = {                    \
		.router_ptr = &_router,                                                            \
		.packet_id = _packet_id,                                                           \
		.app_source_ptr = &_app_source,                                                    \
		.handler_sink = FLOW_SINK_INITIALIZER_IMMEDIATE(                                 \
			_router##_out_##_packet_id, _router_common_outbound_handler,               \
			&__outbound_##_router##_##_packet_id),                                     \
		.node = {NULL}};                                                                   \
	FLOW_CONNECT(&_app_source, &__outbound_##_router##_##_packet_id.handler_sink)

/**
 * @brief Declare a packet router instance (for forward declaration)
 *
 * Use in headers to declare a router defined elsewhere.
 *
 * @param _name Router instance name
 */
#define ROUTER_DECLARE(_name) extern struct packet_router _name

/**
 * @brief Define a packet router instance
 *
 * Creates a router instance with application-provided handlers.
 *
 * @param _name Router instance name
 * @param _inbound_handler Application's inbound packet handler function
 * @param _outbound_handler Application's outbound packet handler function
 */
#define ROUTER_DEFINE(_name, _inbound_handler, _outbound_handler)                                  \
	struct packet_router _name = {                                                             \
		IF_ENABLED(CONFIG_FLOW_NAMES, (.name = #_name, )).network_sink =              \
			FLOW_SINK_INITIALIZER_IMMEDIATE(_name##_network_sink, _inbound_handler,  \
							  &_name),                                 \
		.network_source = FLOW_SOURCE_INITIALIZER(_name.network_source),                 \
		.outbound_handler = _outbound_handler,                                             \
		.inbound_routes = {.list = SYS_SLIST_STATIC_INIT(&_name.inbound_routes.list),      \
				   .lock = {}},                                                    \
		.outbound_routes = {.list = SYS_SLIST_STATIC_INIT(&_name.outbound_routes.list),    \
				    .lock = {}},                                                   \
		IF_ENABLED(CONFIG_FLOW_STATS,                                                 \
			   (.inbound_packets = ATOMIC_INIT(0), .outbound_packets = ATOMIC_INIT(0), \
			    .unknown_packet_ids = ATOMIC_INIT(0), .parse_errors = ATOMIC_INIT(0),  \
			    .buffer_errors = ATOMIC_INIT(0)))}

/**
 * @brief Initialize packet router
 *
 * Sets up routing tables and registers static packet routes.
 *
 * @param router Router instance to initialize
 * @return 0 on success, negative errno on error
 */
int router_init(struct packet_router *router);

/**
 * @brief Add an inbound route at runtime
 *
 * Registers a new inbound route that maps a packet ID to an application sink.
 * This allows dynamic routing configuration after initialization.
 *
 * @param router Router instance
 * @param route Inbound route to add (must remain valid)
 */
void router_add_inbound_route(struct packet_router *router, struct router_inbound_route *route);

/**
 * @brief Add an outbound route at runtime
 *
 * Registers a new outbound route that monitors an application source
 * and adds headers to outgoing packets.
 *
 * @param router Router instance
 * @param route Outbound route to add (must remain valid)
 */
void router_add_outbound_route(struct packet_router *router, struct router_outbound_route *route);

/**
 * @brief Find an inbound route by packet ID
 *
 * @param router Router instance
 * @param packet_id Packet ID to search for
 * @return Pointer to route or NULL if not found
 */
struct router_inbound_route *router_find_inbound_route(struct packet_router *router,
						       uint16_t packet_id);

/**
 * @brief Find an outbound route by source
 *
 * @param router Router instance
 * @param source Source to search for
 * @return Pointer to route or NULL if not found
 */
struct router_outbound_route *router_find_outbound_route_by_source(struct packet_router *router,
								   struct flow_source *source);

void _router_common_outbound_handler(struct flow_sink *sink, struct net_buf *buf);

#ifdef CONFIG_FLOW_STATS
/**
 * @brief Router statistics structure
 */
struct router_stats {
	uint32_t inbound_packets;
	uint32_t outbound_packets;
	uint32_t unknown_packet_ids;
	uint32_t parse_errors;
	uint32_t buffer_errors;
};

/**
 * @brief Get router statistics
 *
 * @param router Router instance
 * @param stats Output statistics structure
 */
void router_get_stats(struct packet_router *router, struct router_stats *stats);

/**
 * @brief Reset router statistics
 *
 * @param router Router instance
 */
void router_reset_stats(struct packet_router *router);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_FLOW_ROUTER_H_ */