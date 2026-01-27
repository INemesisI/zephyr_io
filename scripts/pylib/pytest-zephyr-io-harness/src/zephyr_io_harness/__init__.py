# Copyright (c) 2024 Zephyr IO Project
# SPDX-License-Identifier: Apache-2.0

"""Pytest plugin for Zephyr IO project hardware testing fixtures."""

__version__ = '0.1.0'


def __getattr__(name):
    """Lazy import to avoid requiring twister_harness at module load time."""
    if name == 'TcpConfig':
        from zephyr_io_harness.fixtures import TcpConfig
        return TcpConfig
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
