/*
 * Packet I/O Basic Sample - Packet Routing with Header Addition
 * 
 * This sample demonstrates a simple packet routing scenario:
 * - Two data sources (e.g., sensors) generate packets
 * - A processing node receives packets, adds a custom header, and forwards them
 * - TCP and UDP sinks receive the processed packets for transmission
 * 
 * Packet flow:
 *   sensor1_source ─┐                                      ┌─→ tcp_sink
 *                   ├─→ processor_sink → processor_source ─┤
 *   sensor2_source ─┘                                      └─→ udp_sink
 */

#include <zephyr/kernel.h>
#include <zephyr/net/buf.h>
#include <zephyr/packet_io/packet_io.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(packet_sample, LOG_LEVEL_INF);

/* Define a simple header structure */
struct packet_header {
	uint8_t  source_id;
	uint8_t  packet_type;
	uint16_t sequence;
	uint32_t timestamp;
} __packed;

/* Buffer pool for packet data */
NET_BUF_POOL_DEFINE(packet_pool, 16, 256, 4, NULL);

/* Define packet sources - these could be sensor interfaces */
PACKET_SOURCE_DEFINE(sensor1_source);
PACKET_SOURCE_DEFINE(sensor2_source);

/* Define processor node - receives raw packets, adds header, forwards */
PACKET_SINK_DEFINE(processor_sink, 10, false);  /* Queue 10 packets */
PACKET_SOURCE_DEFINE(processor_source);

/* Define network sinks - receive processed packets for transmission */
PACKET_SINK_DEFINE(tcp_sink, 10, true);   /* Queue 10, drop if full */
PACKET_SINK_DEFINE(udp_sink, 10, false);  /* Queue 10, wait if full */

/* Wire up the connections at compile time */
PACKET_SOURCE_CONNECT(sensor1_source, processor_sink);
PACKET_SOURCE_CONNECT(sensor2_source, processor_sink);
PACKET_SOURCE_CONNECT(processor_source, tcp_sink);
PACKET_SOURCE_CONNECT(processor_source, udp_sink);

/* Packet processor thread */
static void packet_processor(void *p1, void *p2, void *p3)
{
	struct net_buf *in_buf, *out_buf;
	struct packet_header header;
	static uint16_t sequence = 0;
	int ret;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("Packet processor started");

	while (1) {
		/* Wait for incoming packet */
		ret = k_msgq_get(&processor_sink.msgq, &in_buf, K_FOREVER);
		if (ret != 0) {
			continue;
		}

		/* Allocate new buffer with space for header */
		out_buf = net_buf_alloc(&packet_pool, K_NO_WAIT);
		if (!out_buf) {
			LOG_WRN("No buffer for processed packet");
			net_buf_unref(in_buf);
			continue;
		}

		/* Build header based on source (simplified detection) */
		header.source_id = (in_buf->data[0] == 0xA0) ? 1 : 2;
		header.packet_type = 0x01;  /* Data packet */
		header.sequence = sequence++;
		header.timestamp = k_uptime_get_32();

		/* Add header to new buffer */
		net_buf_add_mem(out_buf, &header, sizeof(header));
		
		/* Copy original data */
		net_buf_add_mem(out_buf, in_buf->data, in_buf->len);

		LOG_DBG("Processing packet from sensor %d, seq %d, size %d->%d",
			header.source_id, header.sequence, 
			in_buf->len, out_buf->len);

		/* Send processed packet to both TCP and UDP sinks */
		ret = packet_source_send(&processor_source, out_buf, K_NO_WAIT);
		LOG_DBG("Distributed to %d network sinks", ret);

		/* Clean up */
		net_buf_unref(in_buf);
		net_buf_unref(out_buf);
	}
}

/* Network transmitter thread - handles both TCP and UDP */
static void network_transmitter(void *p1, void *p2, void *p3)
{
	struct net_buf *buf;
	struct packet_header *header;
	struct k_poll_event events[2];
	int ret;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("Network transmitter started (TCP/UDP)");

	/* Setup poll events for both sinks */
	k_poll_event_init(&events[0],
			  K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
			  K_POLL_MODE_NOTIFY_ONLY,
			  &tcp_sink.msgq);
	
	k_poll_event_init(&events[1],
			  K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
			  K_POLL_MODE_NOTIFY_ONLY,
			  &udp_sink.msgq);

	while (1) {
		/* Wait for data on either sink */
		ret = k_poll(events, 2, K_MSEC(100));

		/* Check TCP sink */
		if (events[0].state == K_POLL_STATE_MSGQ_DATA_AVAILABLE) {
			events[0].state = K_POLL_STATE_NOT_READY;
			while (k_msgq_get(&tcp_sink.msgq, &buf, K_NO_WAIT) == 0) {
				header = (struct packet_header *)buf->data;
				LOG_INF("TCP TX: Sensor %d, Seq %d, %d bytes",
					header->source_id, header->sequence, buf->len);
				/* tcp_send(socket, buf->data, buf->len); */
				k_sleep(K_MSEC(50));  /* Simulate TCP transmission */
				net_buf_unref(buf);
			}
		}

		/* Check UDP sink */
		if (events[1].state == K_POLL_STATE_MSGQ_DATA_AVAILABLE) {
			events[1].state = K_POLL_STATE_NOT_READY;
			while (k_msgq_get(&udp_sink.msgq, &buf, K_NO_WAIT) == 0) {
				header = (struct packet_header *)buf->data;
				LOG_INF("UDP TX: Sensor %d, Seq %d, %d bytes (fast)",
					header->source_id, header->sequence, buf->len);
				/* udp_send(socket, buf->data, buf->len); */
				k_sleep(K_MSEC(10));  /* UDP is faster */
				net_buf_unref(buf);
			}
		}
	}
}

/* Simulate sensor data generation */
static void generate_sensor_data(void)
{
	struct net_buf *buf;
	uint8_t sensor1_data[] = {0xA0, 0x01, 0x02, 0x03, 0x04};
	uint8_t sensor2_data[] = {0xB0, 0x11, 0x12, 0x13, 0x14, 0x15};
	int ret;

	while (1) {
		/* Sensor 1 packet */
		buf = net_buf_alloc(&packet_pool, K_NO_WAIT);
		if (buf) {
			net_buf_add_mem(buf, sensor1_data, sizeof(sensor1_data));
			ret = packet_source_send(&sensor1_source, buf, K_NO_WAIT);
			LOG_DBG("Sensor 1 sent: %d sinks", ret);
			net_buf_unref(buf);
		}

		k_sleep(K_SECONDS(1));

		/* Sensor 2 packet */
		buf = net_buf_alloc(&packet_pool, K_NO_WAIT);
		if (buf) {
			net_buf_add_mem(buf, sensor2_data, sizeof(sensor2_data));
			ret = packet_source_send(&sensor2_source, buf, K_NO_WAIT);
			LOG_DBG("Sensor 2 sent: %d sinks", ret);
			net_buf_unref(buf);
		}

		k_sleep(K_SECONDS(1));
	}
}

/* Thread stacks */
K_THREAD_STACK_DEFINE(processor_stack, 2048);
K_THREAD_STACK_DEFINE(network_stack, 2048);

static struct k_thread processor_thread;
static struct k_thread network_thread;

int main(void)
{
	LOG_INF("Packet I/O Routing Sample");
	LOG_INF("==========================");
	LOG_INF("Demonstrating packet flow with header addition");
	LOG_INF("Routing to both TCP and UDP endpoints");

	/* Start processor thread */
	k_thread_create(&processor_thread, processor_stack,
			K_THREAD_STACK_SIZEOF(processor_stack),
			packet_processor, NULL, NULL, NULL,
			5, 0, K_NO_WAIT);
	k_thread_name_set(&processor_thread, "processor");

	/* Start network transmitter thread (handles both TCP and UDP) */
	k_thread_create(&network_thread, network_stack,
			K_THREAD_STACK_SIZEOF(network_stack),
			network_transmitter, NULL, NULL, NULL,
			5, 0, K_NO_WAIT);
	k_thread_name_set(&network_thread, "net_tx");

	/* Main thread generates sensor data */
	generate_sensor_data();

	return 0;
}