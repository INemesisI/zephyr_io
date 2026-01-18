/*
 * Flow Router Sample - TCP Server Implementation
 *
 * Generic TCP server that forwards packets bidirectionally
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/buf.h>
#include <zephyr/net/socket.h>
#include <zephyr_io/flow/flow.h>

#include "tcp_server.h"

LOG_MODULE_REGISTER(tcp_server, LOG_LEVEL_INF);

/* Event queue for TCP sink */
FLOW_EVENT_QUEUE_DEFINE(tcp_queue, 10);

/* Buffer pool for TCP receive */
FLOW_BUF_POOL_DEFINE(tcp_rx_pool, 3, 256, NULL);

/* Handler for TCP sink - receives packets to send to TCP client */
static void tcp_sink_handler(struct flow_sink *sink, struct net_buf *buf_ref);

/* Define TCP sink with queued handler */
FLOW_SINK_DEFINE_QUEUED(tcp_sink, tcp_sink_handler, &tcp_queue);

/* Define TCP source - forwards incoming packets from TCP client */
FLOW_SOURCE_DEFINE(tcp_rx_source);

/* TCP server state */
static int server_sock = -1;
static int client_sock = -1;
static bool client_connected = false;

/* Statistics */
static uint32_t packets_sent;
static uint32_t bytes_sent;
static uint32_t packets_received;

/* TCP sink handler - sends packets to connected client */
static void tcp_sink_handler(struct flow_sink *sink, struct net_buf *buf_ref)
{
	LOG_INF("TCP sink handler called, client_connected=%d, client_sock=%d", client_connected,
		client_sock);

	if (!client_connected || client_sock < 0) {
		LOG_WRN("No client connected, dropping packet");
		return;
	}

	/* Send the complete packet (with protocol header) to TCP client */
	struct net_buf *frag = buf_ref;
	ssize_t sent_total = 0;

	while (frag) {
		ssize_t sent = send(client_sock, frag->data, frag->len, 0);
		if (sent < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				LOG_WRN("TCP send would block");
				break;
			}
			LOG_ERR("TCP send failed: %d", errno);
			client_connected = false;
			close(client_sock);
			client_sock = -1;
			return;
		}
		sent_total += sent;
		frag = frag->frags;
	}

	if (sent_total > 0) {
		packets_sent++;
		bytes_sent += sent_total;
		LOG_DBG("Sent %zd bytes to TCP client (total: %u packets, %u bytes)", sent_total,
			packets_sent, bytes_sent);
	}
}

/* TCP server thread */
static void tcp_server_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct sockaddr_in addr;
	int ret;

	LOG_INF("TCP server starting on port %d", TCP_SERVER_PORT);

	/* Create TCP socket */
	server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (server_sock < 0) {
		LOG_ERR("Failed to create socket: %d", errno);
		return;
	}

	/* Enable address reuse */
	int optval = 1;
	setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

	/* Bind to port */
	addr.sin_family = AF_INET;
	addr.sin_port = htons(TCP_SERVER_PORT);
	addr.sin_addr.s_addr = INADDR_ANY;

	ret = bind(server_sock, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		LOG_ERR("Failed to bind socket: %d", errno);
		close(server_sock);
		return;
	}

	/* Listen for connections */
	ret = listen(server_sock, 1);
	if (ret < 0) {
		LOG_ERR("Failed to listen: %d", errno);
		close(server_sock);
		return;
	}

	/* Set accept timeout to allow cooperative scheduling */
	struct timeval accept_timeout;
	accept_timeout.tv_sec = 0;
	accept_timeout.tv_usec = 100000; /* 100ms */
	setsockopt(server_sock, SOL_SOCKET, SO_RCVTIMEO, &accept_timeout, sizeof(accept_timeout));

	LOG_INF("TCP server listening on 127.0.0.1:%d", TCP_SERVER_PORT);

	while (1) {
		struct sockaddr_in client_addr;
		socklen_t client_addr_len = sizeof(client_addr);

		/* Accept client connection (with timeout for native_sim compatibility) */
		client_sock =
			accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_len);
		if (client_sock < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				/* Timeout - yield to other threads and retry */
				k_sleep(K_MSEC(10));
				continue;
			}
			LOG_ERR("Failed to accept connection: %d", errno);
			k_sleep(K_SECONDS(1));
			continue;
		}

		client_connected = true;
		char addr_str[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &client_addr.sin_addr, addr_str, sizeof(addr_str));
		LOG_INF("Client connected from %s:%d", addr_str, ntohs(client_addr.sin_port));

		/* Handle client communication */
		while (client_connected) {
			/* Allocate buffer from pool */
			struct net_buf *rx_buf = flow_buf_alloc(&tcp_rx_pool, K_NO_WAIT);
			if (!rx_buf) {
				LOG_WRN("No buffer available for TCP RX");
				k_sleep(K_MSEC(10));
				continue;
			}

			/* Read directly into buffer */
			ssize_t received =
				recv(client_sock, rx_buf->data, net_buf_tailroom(rx_buf), 0);
			if (received < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					net_buf_unref(rx_buf);
					k_sleep(K_MSEC(10));
					continue;
				}
				LOG_ERR("TCP recv failed: %d", errno);
				net_buf_unref(rx_buf);
				break;
			} else if (received == 0) {
				LOG_INF("Client disconnected");
				net_buf_unref(rx_buf);
				break;
			}

			/* Update buffer length */
			net_buf_add(rx_buf, received);
			packets_received++;

			LOG_DBG("Received %zd bytes from TCP client (total: %u packets)", received,
				packets_received);

			/* Forward packet through flow system */
			int ret = flow_source_send(&tcp_rx_source, rx_buf, K_NO_WAIT);
			if (ret <= 0) {
				LOG_WRN("Failed to forward packet from TCP client");
			}
		}

		/* Client disconnected */
		client_connected = false;
		close(client_sock);
		client_sock = -1;
		LOG_INF("Client session ended");
	}
}

/* Event processing thread */
static void tcp_event_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("TCP event processor started");

	while (1) {
		/* Process events from the TCP queue */
		int ret = flow_event_process(&tcp_queue, K_FOREVER);
		if (ret != 0 && ret != -EAGAIN) {
			LOG_ERR("Failed to process TCP event: %d", ret);
		}
	}
}

/* Auto-start threads */
K_THREAD_DEFINE(tcp_server_thread, 2048, tcp_server_thread_fn, NULL, NULL, NULL, 7, 0, 0);
K_THREAD_DEFINE(tcp_event_thread, 1024, tcp_event_thread_fn, NULL, NULL, NULL, 7, 0, 0);
