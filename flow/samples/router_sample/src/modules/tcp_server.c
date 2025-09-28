/*
 * TCP Server Module Implementation
 *
 * Event-driven TCP server using k_poll for non-blocking I/O.
 * Handles single client connection with simultaneous send/receive.
 */

#include "tcp_server.h"
#include "protocols/iotsense_router.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/buf.h>
#include <zephyr/net/socket.h>
#include <zephyr_io/flow/flow.h>
#include <zephyr/posix/poll.h>

LOG_MODULE_REGISTER(tcp_server, LOG_LEVEL_INF);

/* TCP server state */
static int server_socket = -1;
static int client_socket = -1;
static struct k_mutex client_mutex;
static atomic_t client_connected = ATOMIC_INIT(0);

/* Event signaling for outbound packets */
static struct k_sem tx_notify_sem;

/* Forward declaration for TCP transmit handler */
static void tcp_server_tx_handler(struct flow_sink *sink, struct net_buf *buf);

/* TCP server packet I/O interfaces */
FLOW_SOURCE_DEFINE(tcp_server_source); /* TCP receive - packets from network */
FLOW_SINK_DEFINE_IMMEDIATE(tcp_server_sink,
			     tcp_server_tx_handler); /* TCP transmit - packets to network */

/* Buffer pool for TCP communication */
NET_BUF_POOL_DEFINE(tcp_comm_pool, 16, 1024, 4, NULL);

/**
 * @brief Initialize TCP server
 */
static int tcp_server_init(void)
{
	int ret;
	int opt = 1;
	struct sockaddr_in server_addr;

	k_mutex_init(&client_mutex);
	k_sem_init(&tx_notify_sem, 0, 1);

	/* Create server socket */
	server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (server_socket < 0) {
		LOG_ERR("Failed to create server socket: %d", errno);
		return -errno;
	}

	/* Set socket options */
	ret = setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	if (ret < 0) {
		LOG_WRN("Failed to set SO_REUSEADDR: %d", errno);
		/* Non-fatal, continue */
	}

	/* Configure server address */
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(TCP_SERVER_PORT);

	/* Bind socket */
	ret = bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
	if (ret < 0) {
		LOG_ERR("Failed to bind socket: %d", errno);
		close(server_socket);
		server_socket = -1;
		return -errno;
	}

	/* Start listening */
	ret = listen(server_socket, 1);
	if (ret < 0) {
		LOG_ERR("Failed to listen on socket: %d", errno);
		close(server_socket);
		server_socket = -1;
		return -errno;
	}

	LOG_INF("TCP server listening on port %d", TCP_SERVER_PORT);
	return 0;
}

/* Initialize during application phase */
SYS_INIT(tcp_server_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

/**
 * @brief Handle packets going to network via TCP
 */
static void tcp_server_tx_handler(struct flow_sink *sink, struct net_buf *buf)
{
	const struct iotsense_header *hdr;
	ssize_t total_sent = 0;
	ssize_t sent_bytes;
	struct net_buf *frag;
	int client;

	/* Validate packet */
	if (buf->len < sizeof(struct iotsense_header)) {
		LOG_WRN("TCP TX: Packet too small (%d bytes)", buf->len);
		return;
	}

	hdr = (const struct iotsense_header *)buf->data;
	LOG_INF("TCP TX: packet_id=0x%04X, %zu bytes", hdr->packet_id, net_buf_frags_len(buf));

	/* Check if client connected */
	if (!atomic_get(&client_connected)) {
		/* Silently drop - no client connected */
		return;
	}

	/* Get client socket safely */
	k_mutex_lock(&client_mutex, K_FOREVER);
	client = client_socket;
	k_mutex_unlock(&client_mutex);

	if (client < 0) {
		return;
	}

	/* Send all buffer fragments */
	frag = buf;
	while (frag) {
		sent_bytes = send(client, frag->data, frag->len, 0);
		if (sent_bytes < 0) {
			if (errno == EPIPE || errno == ECONNRESET) {
				LOG_INF("TCP client disconnected during send");
			} else {
				LOG_WRN("Failed to send to client: %d", errno);
			}
			/* Mark as disconnected, RX thread will handle cleanup */
			atomic_set(&client_connected, 0);
			return;
		} else if (sent_bytes != frag->len) {
			LOG_WRN("Partial send: %zd/%u bytes", sent_bytes, frag->len);
			/* For simplicity, treat partial send as failure */
			atomic_set(&client_connected, 0);
			return;
		}
		total_sent += sent_bytes;
		frag = frag->frags;
	}

	LOG_DBG("TCP TX: %zd bytes", total_sent);
}

/**
 * @brief Process received TCP packet
 */
static void process_tcp_packet(const uint8_t *data, size_t len)
{
	struct net_buf *buf;
	const struct iotsense_header *hdr;

	/* Verify minimum packet size */
	if (len < sizeof(struct iotsense_header)) {
		LOG_WRN("TCP RX: Packet too small (%zu bytes)", len);
		return;
	}

	/* Allocate buffer for packet */
	buf = net_buf_alloc(&tcp_comm_pool, K_NO_WAIT);
	if (!buf) {
		LOG_ERR("No buffer for TCP packet");
		return;
	}

	/* Copy packet data */
	memcpy(net_buf_add(buf, len), data, len);

	/* Parse header for logging */
	hdr = (const struct iotsense_header *)buf->data;
	LOG_INF("TCP RX: packet_id=0x%04X, %zu bytes", hdr->packet_id, len);

	/* Send to router for processing (consumes buffer) */
	flow_source_send_consume(&tcp_server_source, buf, K_NO_WAIT);
}

/**
 * @brief TCP server accept thread
 *
 * Handles accepting new client connections.
 */
static void tcp_accept_thread(void *p1, void *p2, void *p3)
{
	struct sockaddr_in client_addr;
	socklen_t client_len;
	int new_client;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("TCP accept thread started");

	while (1) {
		/* Wait for client connection (blocking) */
		if (!atomic_get(&client_connected)) {
			LOG_DBG("Waiting for client...");

			client_len = sizeof(client_addr);
			new_client =
				accept(server_socket, (struct sockaddr *)&client_addr, &client_len);

			if (new_client < 0) {
				LOG_DBG("Accept failed: %d", errno);
				k_msleep(1000); /* Wait before retrying */
				continue;
			}

			/* Set client socket to non-blocking for poll() */
			int flags = fcntl(new_client, F_GETFL, 0);
			if (flags >= 0) {
				fcntl(new_client, F_SETFL, flags | O_NONBLOCK);
			}

			/* Update client state */
			k_mutex_lock(&client_mutex, K_FOREVER);
			if (client_socket >= 0) {
				close(client_socket);
			}
			client_socket = new_client;
			k_mutex_unlock(&client_mutex);
			atomic_set(&client_connected, 1);

			LOG_INF("Client connected");
		} else {
			/* Already have a client, wait a bit */
			k_msleep(100);
		}
	}
}

/**
 * @brief TCP client handler thread
 *
 * Event-driven handler using poll() for non-blocking I/O.
 */
static void tcp_client_thread(void *p1, void *p2, void *p3)
{
	uint8_t recv_buffer[RECV_BUFFER_SIZE];
	ssize_t received_bytes;
	struct pollfd fds[1];
	int ret;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("TCP client handler thread started");

	while (1) {
		if (!atomic_get(&client_connected)) {
			/* No client connected, wait */
			k_msleep(10);
			continue;
		}

		/* Get client socket */
		k_mutex_lock(&client_mutex, K_FOREVER);
		int client = client_socket;
		k_mutex_unlock(&client_mutex);

		if (client < 0) {
			k_msleep(10);
			continue;
		}

		/* Set up poll for reading */
		fds[0].fd = client;
		fds[0].events = POLLIN;
		fds[0].revents = 0;

		/* Poll with short timeout to allow periodic processing */
		ret = poll(fds, 1, 10); /* 10ms timeout */

		if (ret > 0 && (fds[0].revents & POLLIN)) {
			/* Data available to read */
			received_bytes =
				recv(client, recv_buffer, sizeof(recv_buffer), MSG_DONTWAIT);

			if (received_bytes > 0) {
				/* Process received packet */
				process_tcp_packet(recv_buffer, received_bytes);
			} else if (received_bytes == 0) {
				/* Client disconnected gracefully */
				LOG_INF("Client disconnected");
				k_mutex_lock(&client_mutex, K_FOREVER);
				close(client_socket);
				client_socket = -1;
				k_mutex_unlock(&client_mutex);
				atomic_set(&client_connected, 0);
			} else if (errno != EAGAIN && errno != EWOULDBLOCK) {
				/* Real error occurred */
				LOG_DBG("Recv error: %d", errno);
				k_mutex_lock(&client_mutex, K_FOREVER);
				close(client_socket);
				client_socket = -1;
				k_mutex_unlock(&client_mutex);
				atomic_set(&client_connected, 0);
			}
		} else if (ret < 0) {
			/* Poll error */
			LOG_DBG("Poll error: %d", errno);
			k_msleep(10);
		}
		/* On timeout (ret == 0), loop continues to allow outbound processing */
	}
}

/* Create TCP server threads */
K_THREAD_DEFINE(tcp_accept_tid, 1024, tcp_accept_thread, NULL, NULL, NULL, 8, 0, 0);
K_THREAD_DEFINE(tcp_client_tid, 2048, tcp_client_thread, NULL, NULL, NULL, 8, 0, 0);
