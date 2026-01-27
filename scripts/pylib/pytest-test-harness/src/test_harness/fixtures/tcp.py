# Copyright (c) 2024 Zephyr IO Project
# SPDX-License-Identifier: Apache-2.0

"""TCP connection fixtures for hardware testing."""

from __future__ import annotations

import logging
import socket
import time
from dataclasses import dataclass
from typing import Generator

import pytest
from twister_harness import DeviceAdapter

logger = logging.getLogger(__name__)

DEFAULT_IP = '127.0.0.1'
DEFAULT_PORT = 4242


@dataclass
class TcpConfig:
    """TCP connection configuration parsed from fixture string."""
    ip: str = DEFAULT_IP
    port: int = DEFAULT_PORT

    @classmethod
    def from_fixture(cls, fixture_str: str) -> TcpConfig:
        """
        Parse TCP configuration from fixture string.

        Format: tcp:ip:port
        Example: tcp:192.168.1.100:5000
        """
        parts = fixture_str.split(':')
        if len(parts) < 3:
            raise ValueError(f'Invalid TCP fixture format: {fixture_str}')
        return cls(ip=parts[1], port=int(parts[2]))


def parse_tcp_fixture(fixtures: list[str] | None) -> TcpConfig | None:
    """Parse tcp:ip:port from fixtures list."""
    if not fixtures:
        return None
    for fixture in fixtures:
        if fixture.startswith('tcp:'):
            try:
                return TcpConfig.from_fixture(fixture)
            except (ValueError, IndexError) as e:
                logger.warning('Invalid TCP fixture: %s (%s)', fixture, e)
    return None


@pytest.fixture
def tcp_config(dut: DeviceAdapter) -> TcpConfig:
    """Extract TCP configuration from device fixtures or use defaults."""
    config = parse_tcp_fixture(dut.device_config.fixtures)
    if config is None:
        logger.info('TCP config using defaults: %s:%d', DEFAULT_IP, DEFAULT_PORT)
        return TcpConfig()

    logger.info('TCP config from hardware map: %s:%d', config.ip, config.port)
    return config


@pytest.fixture
def tcp_connection(
    tcp_config: TcpConfig,
    request: pytest.FixtureRequest
) -> Generator[socket.socket, None, None]:
    """
    Provide a TCP socket connection to the device.

    Retries connection up to 10 times with 200ms delay to wait for DUT startup.

    Timeout priority (highest to lowest):
        1. @pytest.mark.tcp_timeout(seconds) on test
        2. --tcp-timeout CLI option
        3. Default 10.0 seconds
    """
    marker = request.node.get_closest_marker('tcp_timeout')
    if marker and marker.args:
        timeout = float(marker.args[0])
    else:
        timeout = request.config.getoption('--tcp-timeout', default=10.0)

    logger.info('Connecting to %s:%d', tcp_config.ip, tcp_config.port)

    for attempt in range(10):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        try:
            sock.connect((tcp_config.ip, tcp_config.port))
            logger.info('TCP connection established')
            try:
                yield sock
            finally:
                sock.close()
                logger.info('TCP connection closed')
            return
        except (ConnectionRefusedError, OSError):
            sock.close()
            time.sleep(0.2)

    raise ConnectionError(f'Failed to connect to {tcp_config.ip}:{tcp_config.port}')
