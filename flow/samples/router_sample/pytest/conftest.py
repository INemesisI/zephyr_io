"""
Pytest configuration and fixtures for Flow Router sample
"""

import pytest
import sys
from pathlib import Path

# Add parent directory to path to import sample_client
sys.path.insert(0, str(Path(__file__).parent.parent))

from tcp_client import SampleClient


@pytest.fixture
def sample_client(dut):
    """
    Pytest fixture that provides a SampleClient connected to the device under test.

    The DUT (device under test) is provided by pytest-twister-harness.
    """
    # Wait for device to be ready
    dut.readlines_until(regex=".*TCP server listening.*", timeout=10.0)

    # Create and connect client
    client = SampleClient(host="127.0.0.1", port=4242, timeout=2.0)

    if not client.connect():
        pytest.fail("Failed to connect to TCP server")

    yield client

    # Cleanup
    client.disconnect()
