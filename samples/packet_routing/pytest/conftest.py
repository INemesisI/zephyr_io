"""
Pytest fixtures for packet_routing sample.

Provides packet parsing on top of tcp_connection fixture.
"""

import struct
from dataclasses import dataclass
from socket import timeout as SocketTimeout
from typing import Generator

import pytest

# Protocol constants
HEADER_SIZE = 14  # packet_id(1) + reserved(1) + counter(2) + content_length(2) + timestamp_ns(8)
CMD_START_SAMPLING = 0x01
CMD_STOP_SAMPLING = 0x02


@dataclass
class SensorPacket:
    """Received sensor packet."""
    packet_id: int
    counter: int
    timestamp_ns: int
    content_length: int
    payload: bytes


class PacketClient:
    """TCP client with packet parsing for the packet_routing sample protocol."""

    def __init__(self, tcp_connection):
        self._sock = tcp_connection

    def send_command(self, cmd: int) -> None:
        """Send a command byte."""
        self._sock.send(bytes([cmd]))

    def start_sampling(self) -> None:
        """Send start sampling command."""
        self.send_command(CMD_START_SAMPLING)

    def stop_sampling(self) -> None:
        """Send stop sampling command."""
        self.send_command(CMD_STOP_SAMPLING)

    def recv_exact(self, n: int, timeout: float = 5.0) -> bytes | None:
        """Receive exactly n bytes."""
        self._sock.settimeout(timeout)
        data = bytearray()
        try:
            while len(data) < n:
                chunk = self._sock.recv(n - len(data))
                if not chunk:
                    return None
                data.extend(chunk)
        except SocketTimeout:
            return None
        return bytes(data)

    def receive_packet(self, timeout: float = 5.0) -> SensorPacket | None:
        """Receive and parse a single sensor packet."""
        header = self.recv_exact(HEADER_SIZE, timeout)
        if not header:
            return None

        packet_id, _, counter, content_length, timestamp_ns = struct.unpack("<BBHHQ", header)

        payload = self.recv_exact(content_length, timeout)
        if not payload:
            return None

        return SensorPacket(
            packet_id=packet_id,
            counter=counter,
            timestamp_ns=timestamp_ns,
            content_length=content_length,
            payload=payload
        )

    def receive_packets(self, count: int, timeout: float = 5.0) -> list[SensorPacket]:
        """Receive multiple packets."""
        packets = []
        for _ in range(count):
            packet = self.receive_packet(timeout)
            if packet is None:
                break
            packets.append(packet)
        return packets


@pytest.fixture
def packet_client(tcp_connection) -> Generator[PacketClient, None, None]:
    """Provide a PacketClient wrapping the TCP connection."""
    yield PacketClient(tcp_connection)
