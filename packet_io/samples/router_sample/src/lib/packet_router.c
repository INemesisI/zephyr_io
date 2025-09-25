/* Packet router implementation */

#include <zephyr/kernel.h>
#include "packet_router.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(packet_router, LOG_LEVEL_DBG);

void _router_common_outbound_handler(struct packet_sink *sink, struct net_buf *buf)
{
    struct router_outbound_route *route = (struct router_outbound_route *)sink->user_data;
    route->router->outbound_handler(route->router, buf, route->packet_id);
}

struct router_inbound_route *router_find_inbound_route(struct packet_router *router,
                                                       uint16_t packet_id)
{
    struct router_inbound_route *route;
    sys_snode_t *node;

    k_spinlock_key_t key = k_spin_lock(&router->inbound_routes.lock);

    SYS_SLIST_FOR_EACH_NODE(&router->inbound_routes.list, node) {
        route = CONTAINER_OF(node, struct router_inbound_route, node);
        if (route->packet_id == packet_id) {
            k_spin_unlock(&router->inbound_routes.lock, key);
            return route;
        }
    }

    k_spin_unlock(&router->inbound_routes.lock, key);
    return NULL;
}

void router_add_inbound_route(struct packet_router *router,
                            struct router_inbound_route *route)
{
    k_spinlock_key_t key = k_spin_lock(&router->inbound_routes.lock);
    sys_slist_append(&router->inbound_routes.list, &route->node);
    k_spin_unlock(&router->inbound_routes.lock, key);

}

void router_add_outbound_route(struct packet_router *router,
                             struct router_outbound_route *route)
{
    k_spinlock_key_t key = k_spin_lock(&router->outbound_routes.lock);
    sys_slist_append(&router->outbound_routes.list, &route->node);
    k_spin_unlock(&router->outbound_routes.lock, key);

}


struct router_outbound_route *router_find_outbound_route_by_source(
    struct packet_router *router,
    struct packet_source *source)
{
    struct router_outbound_route *route;
    sys_snode_t *node;

    k_spinlock_key_t key = k_spin_lock(&router->outbound_routes.lock);

    SYS_SLIST_FOR_EACH_NODE(&router->outbound_routes.list, node) {
        route = CONTAINER_OF(node, struct router_outbound_route, node);
        if (route->app_source == source) {
            k_spin_unlock(&router->outbound_routes.lock, key);
            return route;
        }
    }

    k_spin_unlock(&router->outbound_routes.lock, key);
    return NULL;
}



int router_init(struct packet_router *router)
{
    int inbound_count = 0, outbound_count = 0;


    STRUCT_SECTION_FOREACH(router_inbound_route, inbound_item) {
        if (inbound_item->router == router) {
            router_add_inbound_route(router, inbound_item);
            inbound_count++;
        }
    }

    STRUCT_SECTION_FOREACH(router_outbound_route, outbound_item) {
        if (outbound_item->router == router) {
            router_add_outbound_route(router, outbound_item);
            outbound_count++;
        }
    }

    LOG_INF("Router ready: %d inbound, %d outbound routes",
            inbound_count, outbound_count);

    return 0;
}


#ifdef CONFIG_PACKET_IO_STATS
void router_get_stats(struct packet_router *router, struct router_stats *stats)
{
    stats->inbound_packets = atomic_get(&router->inbound_packets);
    stats->outbound_packets = atomic_get(&router->outbound_packets);
    stats->unknown_packet_ids = atomic_get(&router->unknown_packet_ids);
    stats->parse_errors = atomic_get(&router->parse_errors);
    stats->buffer_errors = atomic_get(&router->buffer_errors);
}

void router_reset_stats(struct packet_router *router)
{
    atomic_set(&router->inbound_packets, 0);
    atomic_set(&router->outbound_packets, 0);
    atomic_set(&router->unknown_packet_ids, 0);
    atomic_set(&router->parse_errors, 0);
    atomic_set(&router->buffer_errors, 0);
}
#endif /* CONFIG_PACKET_IO_STATS */