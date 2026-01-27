# Copyright (c) 2024 Zephyr IO Project
# SPDX-License-Identifier: Apache-2.0

"""Pytest plugin for Zephyr IO hardware testing fixtures."""

from __future__ import annotations

import logging

import pytest

logger = logging.getLogger(__name__)

# Register fixtures module
pytest_plugins = ('test_harness.fixtures',)


def pytest_addoption(parser: pytest.Parser):
    """Add plugin-specific command line options."""
    group = parser.getgroup('Zephyr IO harness')
    group.addoption(
        '--tcp-timeout',
        type=float,
        default=10.0,
        help='Timeout for TCP connections in seconds (default: 10.0).'
    )
    group.addoption(
        '--http-timeout',
        type=float,
        default=10.0,
        help='Timeout for HTTP requests in seconds (default: 10.0).'
    )
    group.addoption(
        '--udp-timeout',
        type=float,
        default=10.0,
        help='Timeout for UDP receive in seconds (default: 10.0).'
    )


def pytest_configure(config: pytest.Config):
    """Configure plugin when pytest starts."""
    if config.getoption('help'):
        return

    config.addinivalue_line(
        'markers',
        'tcp_timeout(seconds): Override TCP connection timeout for this test'
    )
    config.addinivalue_line(
        'markers',
        'http_timeout(seconds): Override HTTP request timeout for this test'
    )
    config.addinivalue_line(
        'markers',
        'udp_timeout(seconds): Override UDP receive timeout for this test'
    )

    logger.debug('Zephyr IO harness plugin configured')
