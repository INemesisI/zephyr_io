# Copyright (c) 2024 Zephyr IO Project
# SPDX-License-Identifier: Apache-2.0

"""UDP server fixtures for receiving packets from devices."""

from __future__ import annotations

import logging
import socket
from dataclasses import dataclass
from typing import Generator

import pytest
from twister_harness import DeviceAdapter

logger = logging.getLogger(__name__)

DEFAULT_PORT = 4243
DEFAULT_BIND = '127.0.0.1'


@dataclass
class UdpConfig:
    """UDP server configuration parsed from fixture string."""
    port: int = DEFAULT_PORT
    bind_address: str = DEFAULT_BIND

    @classmethod
    def from_fixture(cls, fixture_str: str) -> UdpConfig:
        """
        Parse UDP configuration from fixture string.

        Formats:
            udp:5000            -> listen on port 5000, all interfaces
            udp:5000:0.0.0.0    -> explicit bind address
            udp:5000:127.0.0.1  -> localhost only
        """
        parts = fixture_str.split(':')
        if len(parts) < 2:
            raise ValueError(f'Invalid UDP fixture format: {fixture_str}')
        port = int(parts[1])
        bind_address = parts[2] if len(parts) >= 3 else '0.0.0.0'
        return cls(port=port, bind_address=bind_address)


def parse_udp_fixture(fixtures: list[str] | None) -> UdpConfig | None:
    """Parse udp:port:bind_address from fixtures list."""
    if not fixtures:
        return None
    for fixture in fixtures:
        if fixture.startswith('udp:'):
            try:
                return UdpConfig.from_fixture(fixture)
            except (ValueError, IndexError) as e:
                logger.warning('Invalid UDP fixture: %s (%s)', fixture, e)
    return None


@dataclass
class UdpPacket:
    """Received UDP packet with sender information."""
    data: bytes
    address: tuple[str, int]  # (ip, port)

    @property
    def sender_ip(self) -> str:
        return self.address[0]

    @property
    def sender_port(self) -> int:
        return self.address[1]


class UdpServer:
    """UDP server for receiving packets from devices under test."""

    def __init__(
        self,
        port: int,
        bind_address: str = '0.0.0.0',
        buffer_size: int = 65535,
    ):
        self._port = port
        self._bind_address = bind_address
        self._buffer_size = buffer_size
        self._socket: socket.socket | None = None
        self._last_sender: tuple[str, int] | None = None

    @property
    def port(self) -> int:
        return self._port

    @property
    def bind_address(self) -> str:
        return self._bind_address

    @property
    def last_sender(self) -> tuple[str, int] | None:
        """Address of the last packet sender (ip, port)."""
        return self._last_sender

    def start(self) -> None:
        """Start the UDP server and bind to the configured port."""
        if self._socket is not None:
            return

        self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._socket.bind((self._bind_address, self._port))

        # Get actual port if 0 was specified (ephemeral port)
        if self._port == 0:
            self._port = self._socket.getsockname()[1]

        logger.info('UDP server listening on %s:%d', self._bind_address, self._port)

    def stop(self) -> None:
        """Stop the UDP server and close the socket."""
        if self._socket is not None:
            self._socket.close()
            self._socket = None
            logger.info('UDP server stopped')

    def recv(self, timeout: float = 10.0) -> bytes:
        """Receive a UDP packet."""
        packet = self.recv_from(timeout)
        return packet.data

    def recv_from(self, timeout: float = 10.0) -> UdpPacket:
        """Receive a UDP packet with sender information."""
        if self._socket is None:
            raise RuntimeError('UDP server not started')

        self._socket.settimeout(timeout)
        try:
            data, addr = self._socket.recvfrom(self._buffer_size)
            self._last_sender = addr
            logger.debug('Received %d bytes from %s:%d', len(data), addr[0], addr[1])
            return UdpPacket(data=data, address=addr)
        except socket.timeout as exc:
            raise TimeoutError(f'No UDP packet received within {timeout}s') from exc

    def send_to(self, data: bytes, address: tuple[str, int] | None = None) -> int:
        """Send a UDP packet to a specific address."""
        if self._socket is None:
            raise RuntimeError('UDP server not started')

        dest = address or self._last_sender
        if dest is None:
            raise RuntimeError('No destination address (no packets received yet)')

        sent = self._socket.sendto(data, dest)
        logger.debug('Sent %d bytes to %s:%d', sent, dest[0], dest[1])
        return sent

    def reply(self, data: bytes) -> int:
        """Reply to the last sender."""
        return self.send_to(data, self._last_sender)

    def clear(self, timeout: float = 0.1) -> int:
        """Clear any pending packets from the receive buffer."""
        if self._socket is None:
            return 0

        count = 0
        self._socket.settimeout(timeout)
        while True:
            try:
                self._socket.recvfrom(self._buffer_size)
                count += 1
            except socket.timeout:
                break

        if count > 0:
            logger.debug('Cleared %d pending UDP packets', count)
        return count

    def __enter__(self) -> UdpServer:
        self.start()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self.stop()


@pytest.fixture
def udp_config(dut: DeviceAdapter) -> UdpConfig:
    """Extract UDP configuration from device fixtures or use defaults."""
    config = parse_udp_fixture(dut.device_config.fixtures)
    if config is None:
        logger.info('UDP config using defaults: %s:%d', DEFAULT_BIND, DEFAULT_PORT)
        return UdpConfig()

    logger.info('UDP config from hardware map: %s:%d', config.bind_address, config.port)
    return config


@pytest.fixture
def udp_server(udp_config: UdpConfig, request: pytest.FixtureRequest) -> Generator[UdpServer, None, None]:
    """Provide a UDP server for receiving packets from the device."""
    server = UdpServer(port=udp_config.port, bind_address=udp_config.bind_address)
    server.start()
    server.clear()

    try:
        yield server
    finally:
        server.stop()
