/*
 * Generic Packet Router Library
 *
 * Provides bidirectional packet routing with configurable header formats.
 * Applications define protocol-specific parsers and packet type mappings.
 */

#ifndef ZEPHYR_INCLUDE_PACKET_ROUTER_H_
#define ZEPHYR_INCLUDE_PACKET_ROUTER_H_

#include <zephyr/kernel.h>
#include <zephyr/net/buf.h>
#include <zephyr/packet_io/packet_io.h>
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

/* Router handler function types */
typedef void (*router_outbound_handler_t)(struct packet_router *router, struct net_buf *buf, uint16_t packet_id);

/** Inbound packet route registration (network → application) */
struct router_inbound_route {
    struct packet_router *router_ptr;
    uint16_t packet_id;
    struct packet_sink *app_sink_ptr;
    sys_snode_t node;
};

/** Outbound packet route registration (application → network) */
struct router_outbound_route {
    struct packet_router *router_ptr;
    uint16_t packet_id;
    struct packet_source *app_source_ptr;
    struct packet_sink handler_sink;
    sys_snode_t node;
};

/** List container for packet routes */
struct router_route_list {
    sys_slist_t list;
    struct k_spinlock lock;
};


/**
 * @brief Packet router instance
 *
 * Each router instance handles one protocol with its own header format.
 * Multiple routers can coexist for different protocols.
 */
struct packet_router {
#ifdef CONFIG_PACKET_IO_NAMES
    const char *name;
#endif
    struct packet_sink network_sink;
    struct packet_source network_source;
    router_outbound_handler_t outbound_handler;
    struct router_route_list inbound_routes;
    struct router_route_list outbound_routes;
#ifdef CONFIG_PACKET_IO_STATS
    atomic_t inbound_packets;
    atomic_t outbound_packets;
    atomic_t unknown_packet_ids;
    atomic_t parse_errors;
    atomic_t buffer_errors;
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
#define ROUTER_INBOUND_ROUTE_DEFINE(_router, _packet_id, _app_sink)         \
    static STRUCT_SECTION_ITERABLE(router_inbound_route,                     \
                                   __inbound_##_router##_##_packet_id) = {  \
        .router_ptr = &_router,                                             \
        .packet_id = _packet_id,                                            \
        .app_sink_ptr = &_app_sink,                                         \
        .node = {NULL},                                                     \
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
#define ROUTER_OUTBOUND_ROUTE_DEFINE(_router, _packet_id, _app_source)      \
    static STRUCT_SECTION_ITERABLE(router_outbound_route,                   \
                                   __outbound_##_router##_##_packet_id) = { \
        .router_ptr = &_router,                                             \
        .packet_id = _packet_id,                                            \
        .app_source_ptr = &_app_source,                                     \
        .handler_sink = PACKET_SINK_INITIALIZER_IMMEDIATE(                  \
                            _router##_out_##_packet_id,                     \
                            _router_common_outbound_handler,                \
                            &__outbound_##_router##_##_packet_id),                                                                  \
        .node = {NULL}                                                      \
    };                                                                      \
    PACKET_CONNECT(&_app_source,                                            \
                    &__outbound_##_router##_##_packet_id.handler_sink)


/**
 * @brief Declare a packet router instance (for forward declaration)
 *
 * Use in headers to declare a router defined elsewhere.
 *
 * @param _name Router instance name
 */
#define ROUTER_DECLARE(_name) \
    extern struct packet_router _name

/**
 * @brief Define a packet router instance
 *
 * Creates a router instance with application-provided handlers.
 *
 * @param _name Router instance name
 * @param _inbound_handler Application's inbound packet handler function
 * @param _outbound_handler Application's outbound packet handler function
 */
#define ROUTER_DEFINE(_name, _inbound_handler, _outbound_handler)           \
    struct packet_router _name = {                                          \
        IF_ENABLED(CONFIG_PACKET_IO_NAMES,                                 \
                   (.name = #_name,))                                      \
        .network_sink = PACKET_SINK_INITIALIZER_IMMEDIATE(_name##_network_sink,  \
                                                           _inbound_handler,      \
                                                           &_name),               \
        .network_source = PACKET_SOURCE_INITIALIZER(_name.network_source),                                                                  \
        .outbound_handler = _outbound_handler,                              \
        .inbound_routes = {                                                  \
            .list = SYS_SLIST_STATIC_INIT(&_name.inbound_routes.list),      \
            .lock = {}                                                      \
        },                                                                  \
        .outbound_routes = {                                                 \
            .list = SYS_SLIST_STATIC_INIT(&_name.outbound_routes.list),     \
            .lock = {}                                                      \
        },                                                                  \
        IF_ENABLED(CONFIG_PACKET_IO_STATS,                                 \
                   (.inbound_packets = ATOMIC_INIT(0),                     \
                    .outbound_packets = ATOMIC_INIT(0),                    \
                    .unknown_packet_ids = ATOMIC_INIT(0),                  \
                    .parse_errors = ATOMIC_INIT(0),                        \
                    .buffer_errors = ATOMIC_INIT(0)))                      \
    }


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
void router_add_inbound_route(struct packet_router *router,
                            struct router_inbound_route *route);

/**
 * @brief Add an outbound route at runtime
 *
 * Registers a new outbound route that monitors an application source
 * and adds headers to outgoing packets.
 *
 * @param router Router instance
 * @param route Outbound route to add (must remain valid)
 */
void router_add_outbound_route(struct packet_router *router,
                             struct router_outbound_route *route);

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
struct router_outbound_route *router_find_outbound_route_by_source(
    struct packet_router *router,
    struct packet_source *source);

void _router_common_outbound_handler(struct packet_sink *sink, struct net_buf *buf);

#ifdef CONFIG_PACKET_IO_STATS
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

#endif /* ZEPHYR_INCLUDE_PACKET_ROUTER_H_ */