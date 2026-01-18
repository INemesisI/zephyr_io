"""
Pytest tests for Flow Router sample with TCP server
"""

import pytest


def test_tcp_server_initialization(dut):
    """Test that TCP server starts and listens on correct port"""
    # Wait for server to start
    dut.readlines_until(regex=".*TCP server listening on 127.0.0.1:4242.*", timeout=10.0)


def test_receive_sensor_packets(sample_client):
    """Test receiving sensor data packets"""
    # Start sampling first
    assert sample_client.start_sampling(), "Failed to start sampling"

    # Give sensors time to start generating packets
    import time
    time.sleep(0.5)

    # Receive some packets
    packets = sample_client.receive_packets(count=5, timeout=5.0)

    # Should receive at least a few packets
    assert len(packets) >= 3, f"Expected at least 3 packets, got {len(packets)}"

    # Verify packet structure
    for packet in packets:
        assert packet.packet_id in [1, 2], f"Invalid packet_id: {packet.packet_id}"
        assert packet.content_length > 0, "Packet should have payload"
        assert packet.timestamp_ns > 0, "Timestamp should be set"

        # Sensor 1 sends 256 bytes, Sensor 2 sends 384 bytes
        if packet.packet_id == 1:
            assert packet.content_length == 256, f"Sensor 1 should send 256 bytes, got {packet.content_length}"
        elif packet.packet_id == 2:
            assert packet.content_length == 384, f"Sensor 2 should send 384 bytes, got {packet.content_length}"


def test_stop_start_sampling(sample_client):
    """Test stop and start sampling commands"""
    # Start sampling first
    assert sample_client.start_sampling(), "Failed to start sampling"

    import time
    time.sleep(0.2)  # Give time for packets to start

    # Receive initial packets to confirm sampling is running
    packets_before = sample_client.receive_packets(count=2, timeout=2.0)
    assert len(packets_before) >= 1, "Should receive packets when sampling is active"

    # Send stop command
    assert sample_client.stop_sampling(), "Failed to send stop command"

    # Wait a bit and verify no more packets
    import time
    time.sleep(1.0)
    packets_stopped = sample_client.receive_packets(count=5, timeout=2.0)
    assert len(packets_stopped) == 0, f"Should not receive packets after stop, got {len(packets_stopped)}"

    # Send start command
    assert sample_client.start_sampling(), "Failed to send start command"

    # Verify packets resume
    packets_after = sample_client.receive_packets(count=2, timeout=3.0)
    assert len(packets_after) >= 1, "Should receive packets after restart"


def test_packet_metadata(sample_client):
    """Test that packet metadata fields are correct"""
    # Start sampling first
    assert sample_client.start_sampling(), "Failed to start sampling"

    import time
    time.sleep(0.2)

    packets = sample_client.receive_packets(count=10, timeout=3.0)

    assert len(packets) >= 5, "Should receive multiple packets for metadata test"

    # Check counter increments
    counters = [p.counter for p in packets]
    # Counters should be incrementing (allowing for sensor interleaving)
    assert max(counters) > min(counters), "Counters should increment"

    # Check timestamps are reasonable and incrementing
    timestamps = [p.timestamp_ns for p in packets]
    assert all(t > 0 for t in timestamps), "All timestamps should be positive"
    # Later packets should generally have later timestamps
    assert timestamps[-1] >= timestamps[0], "Timestamps should generally increase"


def test_packet_payload_patterns(sample_client):
    """Test that packet payloads contain expected patterns"""
    # Start sampling first
    assert sample_client.start_sampling(), "Failed to start sampling"

    import time
    time.sleep(0.2)

    packets = sample_client.receive_packets(count=10, timeout=3.0)

    assert len(packets) >= 5, "Should receive multiple packets"

    for packet in packets:
        # Sensor 1 uses pattern 0xA1, Sensor 2 uses pattern 0xB2
        if packet.packet_id == 1:
            # Check first few bytes
            assert all(b == 0xA1 for b in packet.payload[:16]), "Sensor 1 should send 0xA1 pattern"
        elif packet.packet_id == 2:
            assert all(b == 0xB2 for b in packet.payload[:16]), "Sensor 2 should send 0xB2 pattern"


