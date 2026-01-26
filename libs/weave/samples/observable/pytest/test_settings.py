"""Tests for Weave Observable Settings sample."""


def test_sample_initialization(dut):
    """Verify sample starts and shows banner."""
    dut.readlines_until(regex=".*Weave Observable Settings Sample.*", timeout=10.0)
    dut.readlines_until(regex=".*Use 'settings get' to view current settings.*", timeout=2.0)


def test_settings_get(shell):
    """Verify settings get command."""
    lines = shell.exec_command("settings get")
    assert any("sample_rate=" in line for line in lines)


def test_settings_set(shell):
    """Verify settings set notifies all observers."""
    lines = shell.exec_command("settings set 500")

    assert any("Settings updated: sample_rate=500" in line for line in lines)
    assert any("[IMMEDIATE #" in line and "sample_rate=500" in line for line in lines)
    assert any("[SENSOR #" in line and "500 ms" in line for line in lines)
    # Queued observer also fires synchronously (processing thread runs immediately)
    assert any("[QUEUED #" in line and "sample_rate=500" in line for line in lines)


def test_settings_validation(shell):
    """Verify invalid rates are rejected."""
    lines = shell.exec_command("settings set 50")
    assert any("Rate must be between 100 and 10000" in line for line in lines)

    lines = shell.exec_command("settings set 20000")
    assert any("Rate must be between 100 and 10000" in line for line in lines)
