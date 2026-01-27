# Copyright (c) 2024 Zephyr IO Project
# SPDX-License-Identifier: Apache-2.0

"""Shared pytest fixtures for Zephyr IO hardware testing."""

from __future__ import annotations

import logging
import socket
from dataclasses import dataclass
from typing import TYPE_CHECKING, Generator

import pytest

if TYPE_CHECKING:
    from twister_harness import DeviceAdapter

logger = logging.getLogger(__name__)


@dataclass
class TcpConfig:
    """TCP connection configuration parsed from fixture string."""
    ip: str
    port: int


def parse_tcp_fixture(fixtures: list[str] | None) -> TcpConfig | None:
    """
    Parse tcp:ip:port from fixtures list.

    Args:
        fixtures: List of fixture strings from device config

    Returns:
        TcpConfig if tcp fixture found, None otherwise
    """
    if not fixtures:
        return None

    for fixture in fixtures:
        if fixture.startswith('tcp:'):
            parts = fixture.split(':')
            if len(parts) >= 3:
                try:
                    return TcpConfig(ip=parts[1], port=int(parts[2]))
                except ValueError:
                    logger.warning('Invalid TCP port in fixture: %s', fixture)
    return None


@pytest.fixture(scope='session')
def tcp_config(dut: DeviceAdapter) -> TcpConfig:
    """
    Extract TCP configuration from device fixtures.

    Expects a fixture in format: tcp:ip:port
    Example: tcp:192.168.1.100:5000

    Args:
        dut: Device adapter from twister harness

    Returns:
        TcpConfig with ip and port

    Raises:
        pytest.skip: If no TCP fixture is configured
    """
    config = parse_tcp_fixture(dut.device_config.fixtures)
    if config is None:
        pytest.skip('No tcp:ip:port fixture configured for this device')
    logger.info('TCP config: %s:%d', config.ip, config.port)
    return config


@pytest.fixture
def tcp_connection(
    tcp_config: TcpConfig,
    request: pytest.FixtureRequest
) -> Generator[socket.socket, None, None]:
    """
    Provide a TCP socket connection to the device.

    Timeout priority (highest to lowest):
        1. @pytest.mark.tcp_timeout(seconds) on test
        2. --tcp-timeout CLI option
        3. Default 10.0 seconds

    Args:
        tcp_config: TCP configuration from device fixtures
        request: Pytest fixture request for accessing options

    Yields:
        Connected TCP socket

    Raises:
        ConnectionError: If connection fails
    """
    marker = request.node.get_closest_marker('tcp_timeout')
    if marker and marker.args:
        timeout = float(marker.args[0])
    else:
        timeout = request.config.getoption('--tcp-timeout', default=10.0)

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)

    logger.info('Connecting to %s:%d', tcp_config.ip, tcp_config.port)
    try:
        sock.connect((tcp_config.ip, tcp_config.port))
        logger.info('TCP connection established')
        yield sock
    finally:
        sock.close()
        logger.info('TCP connection closed')
