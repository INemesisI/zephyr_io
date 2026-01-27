# Copyright (c) 2024 Zephyr IO Project
# SPDX-License-Identifier: Apache-2.0

"""Pytest plugin for hardware testing fixtures."""

__version__ = '0.1.0'


def __getattr__(name):
    """Lazy import to avoid requiring twister_harness at module load time."""
    if name == 'TcpConfig':
        from test_harness.fixtures.tcp import TcpConfig
        return TcpConfig
    if name == 'FlashDevice':
        from test_harness.fixtures.flash import FlashDevice
        return FlashDevice
    if name == 'HttpConfig':
        from test_harness.fixtures.http import HttpConfig
        return HttpConfig
    if name == 'HttpClient':
        from test_harness.fixtures.http import HttpClient
        return HttpClient
    if name == 'UdpConfig':
        from test_harness.fixtures.udp import UdpConfig
        return UdpConfig
    if name == 'UdpServer':
        from test_harness.fixtures.udp import UdpServer
        return UdpServer
    if name == 'UdpPacket':
        from test_harness.fixtures.udp import UdpPacket
        return UdpPacket
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
