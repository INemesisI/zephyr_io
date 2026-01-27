"""
Pytest tests for Flow Router sample with TCP server.

Uses packet_client fixture which wraps tcp_connection with protocol parsing.
"""

import time

from conftest import PacketClient


def test_tcp_server_initialization(dut):
    """Test that TCP server starts and listens on correct port."""
    dut.readlines_until(regex=".*TCP server listening on 127.0.0.1:4242.*", timeout=10.0)


def test_tcp_connection(dut, packet_client: PacketClient):
    """Test basic TCP connectivity."""
    packet_client.start_sampling()
    time.sleep(0.1)
    packet_client.stop_sampling()


def test_receive_sensor_packets(dut, packet_client: PacketClient):
    """Test receiving sensor data packets."""
    packet_client.start_sampling()
    time.sleep(0.5)

    packets = packet_client.receive_packets(count=5, timeout=5.0)

    packet_client.stop_sampling()

    assert len(packets) >= 3, f"Expected at least 3 packets, got {len(packets)}"

    for packet in packets:
        assert packet.packet_id in [1, 2], f"Invalid packet_id: {packet.packet_id}"
        assert packet.content_length > 0, "Packet should have payload"
        assert packet.timestamp_ns > 0, "Timestamp should be set"

        # Sensor 1 sends 256 bytes, Sensor 2 sends 384 bytes
        if packet.packet_id == 1:
            assert packet.content_length == 256
        elif packet.packet_id == 2:
            assert packet.content_length == 384


def test_stop_start_sampling(dut, packet_client: PacketClient):
    """Test stop and start sampling commands."""
    packet_client.start_sampling()
    time.sleep(0.2)

    packets_before = packet_client.receive_packets(count=2, timeout=2.0)
    assert len(packets_before) >= 1, "Should receive packets when sampling is active"

    packet_client.stop_sampling()
    time.sleep(1.0)

    packets_stopped = packet_client.receive_packets(count=5, timeout=2.0)
    assert len(packets_stopped) == 0, "Should not receive packets after stop"

    packet_client.start_sampling()

    packets_after = packet_client.receive_packets(count=2, timeout=3.0)
    assert len(packets_after) >= 1, "Should receive packets after restart"

    packet_client.stop_sampling()


def test_packet_metadata(dut, packet_client: PacketClient):
    """Test that packet metadata fields are correct."""
    packet_client.start_sampling()
    time.sleep(0.2)

    packets = packet_client.receive_packets(count=10, timeout=3.0)

    packet_client.stop_sampling()

    assert len(packets) >= 4, "Should receive multiple packets"

    counters = [p.counter for p in packets]
    assert max(counters) > min(counters), "Counters should increment"

    timestamps = [p.timestamp_ns for p in packets]
    assert all(t > 0 for t in timestamps), "All timestamps should be positive"
    assert timestamps[-1] >= timestamps[0], "Timestamps should generally increase"


def test_packet_payload_patterns(dut, packet_client: PacketClient):
    """Test that packet payloads contain expected patterns."""
    packet_client.start_sampling()
    time.sleep(0.2)

    packets = packet_client.receive_packets(count=10, timeout=3.0)

    packet_client.stop_sampling()

    assert len(packets) >= 4, "Should receive multiple packets"

    for packet in packets:
        # Sensor 1 uses pattern 0xA1, Sensor 2 uses pattern 0xB2
        if packet.packet_id == 1:
            assert all(b == 0xA1 for b in packet.payload[:16])
        elif packet.packet_id == 2:
            assert all(b == 0xB2 for b in packet.payload[:16])
