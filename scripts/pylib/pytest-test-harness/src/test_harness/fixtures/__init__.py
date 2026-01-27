# Copyright (c) 2024 Zephyr IO Project
# SPDX-License-Identifier: Apache-2.0

"""Pytest fixtures for Zephyr IO hardware testing."""

# Re-export all fixtures for pytest discovery
from test_harness.fixtures.tcp import (
    TcpConfig,
    tcp_config,
    tcp_connection,
)
from test_harness.fixtures.flash import (
    FlashDevice,
    flash_device,
)
from test_harness.fixtures.http import (
    HttpConfig,
    HttpClient,
    http_config,
    http_client,
)
from test_harness.fixtures.udp import (
    UdpConfig,
    UdpServer,
    UdpPacket,
    udp_config,
    udp_server,
)

__all__ = [
    # TCP
    'TcpConfig',
    'tcp_config',
    'tcp_connection',
    # Flash
    'FlashDevice',
    'flash_device',
    # HTTP
    'HttpConfig',
    'HttpClient',
    'http_config',
    'http_client',
    # UDP
    'UdpConfig',
    'UdpServer',
    'UdpPacket',
    'udp_config',
    'udp_server',
]
