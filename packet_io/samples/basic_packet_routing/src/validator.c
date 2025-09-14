/*
 * Packet I/O Sample - Validator Module Implementation
 *
 * Self-contained packet validator checking integrity and content
 */

#include <zephyr/kernel.h>
#include <zephyr/net/buf.h>
#include <zephyr/packet_io/packet_io.h>
#include <zephyr/logging/log.h>

#include "validator.h"
#include "packet_defs.h"

LOG_MODULE_REGISTER(validator, LOG_LEVEL_INF);

/* Define validator sink */
PACKET_SINK_DEFINE(validator_sink, 10, false);  /* Queue 10, wait if full */

/* Statistics tracking */
static uint32_t packets_validated;
static uint32_t packets_failed;
static uint32_t last_sequence = UINT32_MAX;  /* Global sequence tracking */

static bool validate_packet_integrity(struct net_buf *buf)
{
	struct packet_header *header = (struct packet_header *)buf->data;
	size_t total_len = net_buf_frags_len(buf);
	size_t expected_len = sizeof(struct packet_header) + header->content_length;

	/* Check size integrity */
	if (total_len != expected_len) {
		LOG_ERR("Size mismatch! Total=%d, Expected=%d (hdr=%d + content=%d)",
			total_len, expected_len,
			sizeof(struct packet_header), header->content_length);
		return false;
	}

	/* Validate source ID */
	if (header->source_id != SOURCE_ID_SENSOR1 &&
	    header->source_id != SOURCE_ID_SENSOR2) {
		LOG_ERR("Invalid source ID: %d", header->source_id);
		return false;
	}

	/* Validate packet type */
	if (header->packet_type != PACKET_TYPE_DATA &&
	    header->packet_type != PACKET_TYPE_CONTROL) {
		LOG_ERR("Invalid packet type: 0x%02x", header->packet_type);
		return false;
	}

	/* Check sequence number progression (global sequence from processor) */
	if (last_sequence != UINT32_MAX) {
		uint32_t expected_seq = last_sequence + 1;
		if (header->sequence != expected_seq) {
			LOG_WRN("Sequence gap detected! Expected %d, Got %d (Source %d)",
				expected_seq, header->sequence, header->source_id);
		}
	}
	last_sequence = header->sequence;

	/* Validate content (check for expected patterns) */
	if (buf->frags) {
		uint8_t first_byte = buf->frags->data[0];
		uint8_t expected = (header->source_id == SOURCE_ID_SENSOR1) ? 0xA0 : 0xB0;
		if (first_byte != expected) {
			LOG_ERR("Content validation failed! Source %d: Expected 0x%02x, Got 0x%02x",
				header->source_id, expected, first_byte);
			return false;
		}
	}

	LOG_DBG("Packet validated: Source %d, Seq %d, Type 0x%02x, %d bytes",
		header->source_id, header->sequence, header->packet_type,
		total_len);

	return true;
}

static void validator_thread_fn(void *p1, void *p2, void *p3)
{
	struct net_buf *buf;
	struct packet_header *header;
	int ret;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("Packet validator started");

	while (1) {
		/* Wait for packet to validate */
		ret = k_msgq_get(&validator_sink.msgq, &buf, K_FOREVER);
		if (ret != 0) {
			continue;
		}

		header = (struct packet_header *)buf->data;

		/* Validate packet */
		if (validate_packet_integrity(buf)) {
			packets_validated++;
		} else {
			packets_failed++;
			LOG_ERR("INVALID: Sensor %d, Seq %d [Total failed: %d]",
				header->source_id, header->sequence, packets_failed);
		}

		/* Clean up */
		net_buf_unref(buf);

		/* Report statistics every 10 packets (~2 seconds) */
		if ((packets_validated + packets_failed) % 10 == 0) {
			uint32_t total = packets_validated + packets_failed;
			uint32_t rate = (packets_validated * 100) / total;
			LOG_INF("Validator: Checked %d packets - Valid=%d, Failed=%d, Success rate=%d%%",
				total, packets_validated, packets_failed, rate);
		}
	}
}

/* Static thread initialization - starts automatically */
K_THREAD_DEFINE(validator_thread, 2048,
		validator_thread_fn, NULL, NULL, NULL,
		6, 0, 0);