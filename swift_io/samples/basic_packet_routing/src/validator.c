/*
 * Packet I/O Sample - Validator Module Implementation
 *
 * Self-contained packet validator checking integrity and content
 */

#include <zephyr/kernel.h>
#include <zephyr/net/buf.h>
#include <zephyr_io/swift_io/swift_io.h>
#include <zephyr/logging/log.h>

#include "validator.h"
#include "packet_defs.h"

LOG_MODULE_REGISTER(validator, LOG_LEVEL_INF);

/* Handler for validation */
static void validator_handler(struct swift_io_sink *sink, struct net_buf *buf);

/* Define validator sink with immediate execution */
SWIFT_IO_SINK_DEFINE_IMMEDIATE(validator_sink, validator_handler);

/* Statistics tracking */
static uint32_t packets_validated;
static uint32_t packets_failed;
static uint32_t last_sequence = UINT32_MAX; /* Global sequence tracking */

static bool validate_packet_integrity(struct net_buf *buf)
{
	struct packet_header *header = (struct packet_header *)buf->data;
	size_t total_len = net_buf_frags_len(buf);
	size_t expected_len = sizeof(struct packet_header) + header->content_length;

	/* Check size integrity */
	if (total_len != expected_len) {
		LOG_ERR("Size mismatch! Total=%d, Expected=%d (hdr=%d + content=%d)", total_len,
			expected_len, sizeof(struct packet_header), header->content_length);
		return false;
	}

	/* Validate source ID */
	if (header->source_id != SOURCE_ID_SENSOR1 && header->source_id != SOURCE_ID_SENSOR2) {
		LOG_ERR("Invalid source ID: %d", header->source_id);
		return false;
	}

	/* Validate packet type */
	if (header->packet_type != PACKET_TYPE_DATA && header->packet_type != PACKET_TYPE_CONTROL) {
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
	/* Content is in the chained buffer after the header */
	if (buf->frags) {
		/* The content starts in the second buffer of the chain */
		uint8_t first_byte = buf->frags->data[0];
		uint8_t expected = (header->source_id == SOURCE_ID_SENSOR1) ? 0xA0 : 0xB0;
		if (first_byte != expected) {
			LOG_ERR("Content validation failed! Source %d: Expected 0x%02x, Got 0x%02x",
				header->source_id, expected, first_byte);
			return false;
		}
	}

	LOG_DBG("Packet validated: Source %d, Seq %d, Type 0x%02x, %d bytes", header->source_id,
		header->sequence, header->packet_type, total_len);

	return true;
}

static void validator_handler(struct swift_io_sink *sink, struct net_buf *buf)
{
	struct packet_header *header;
	static bool first_packet = true;
	static uint32_t packets_received = 0;

	packets_received++;

	if (first_packet) {
		LOG_INF("Packet validator started (immediate mode)");
		first_packet = false;
	}

	/* Check if buffer has enough data for header */
	if (buf->len < sizeof(struct packet_header)) {
		LOG_ERR("Buffer too small for header: %d bytes", buf->len);
		packets_failed++;
		/* Buffer unref handled by framework */
		return;
	}

	header = (struct packet_header *)buf->data;

	/* Validate packet */
	if (validate_packet_integrity(buf)) {
		packets_validated++;
		LOG_INF("VALID: Sensor %d, Seq %d, Type 0x%02x [Total valid: %d]",
			header->source_id, header->sequence, header->packet_type,
			packets_validated);
	} else {
		packets_failed++;
		LOG_ERR("INVALID: Sensor %d, Seq %d [Total failed: %d]", header->source_id,
			header->sequence, packets_failed);
	}

	/* Report statistics with every packet */
	uint32_t total = packets_validated + packets_failed;
	uint32_t rate = (total > 0) ? (packets_validated * 100) / total : 0;
	LOG_INF("Validator: Checked %d packets - Valid=%d, Failed=%d, Success rate=%d%%", total,
		packets_validated, packets_failed, rate);

	/* Buffer unref handled by framework */
}

/* No thread needed for immediate handler - executes in source context */