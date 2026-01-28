"""Tests for Settings Table API shell commands."""


def test_sample_initialization(dut):
    """Verify sample starts and shows expected banner."""
    dut.readlines_until(regex=".*Settings Table Sample.*", timeout=10.0)
    dut.readlines_until(regex=".*Use 'settings' shell commands.*", timeout=2.0)


def test_dump_shows_areas(shell):
    """Verify settings dump shows both motor and sensor areas."""
    lines = shell.exec_command("settings dump")
    assert any("motor" in line and "0x0100" in line for line in lines)
    assert any("sensor" in line and "0x0200" in line for line in lines)


def test_reg_read_motor_speed(shell):
    """Verify reg_read shows motor speed with default value."""
    lines = shell.exec_command("settings reg_read 0x0100")
    assert any("speed" in line and "1000" in line for line in lines)


def test_reg_read_sensor_rate(shell):
    """Verify reg_read shows sensor sample_rate with default value."""
    lines = shell.exec_command("settings reg_read 0x0200")
    assert any("sample_rate" in line and "100" in line for line in lines)


def test_reg_write_motor_speed(shell):
    """Verify reg_write updates motor speed."""
    shell.exec_command("settings reg_write 0x0100 5000")
    lines = shell.exec_command("settings reg_read 0x0100")
    assert any("5000" in line for line in lines)


def test_reg_write_validation_max(shell):
    """Verify reg_write rejects value exceeding max."""
    lines = shell.exec_command("settings reg_write 0x0100 10001")
    assert any("failed" in line.lower() for line in lines)


def test_reg_write_validation_min(shell):
    """Verify reg_write rejects value below min."""
    lines = shell.exec_command("settings reg_write 0x0200 0")  # sample_rate min is 1
    assert any("failed" in line.lower() for line in lines)


def test_read_bytes(shell):
    """Verify settings read returns raw bytes."""
    lines = shell.exec_command("settings read 0x0100 2")
    # Should return 2 bytes as hex dump
    assert any(line.strip() for line in lines)


def test_write_bytes(shell):
    """Verify settings write writes raw bytes."""
    # Write 0x1234 (4660) little-endian to motor speed
    lines = shell.exec_command("settings write 0x0100 0x34 0x12")
    assert any("Wrote 2 bytes" in line for line in lines)
    # Verify the value
    lines = shell.exec_command("settings reg_read 0x0100")
    assert any("4660" in line for line in lines)


def test_reset_defaults(shell):
    """Verify settings reset restores default values."""
    # Change motor speed
    shell.exec_command("settings reg_write 0x0100 9999")
    lines = shell.exec_command("settings reg_read 0x0100")
    assert any("9999" in line for line in lines)
    # Reset
    shell.exec_command("settings reset")
    # Verify default restored
    lines = shell.exec_command("settings reg_read 0x0100")
    assert any("1000" in line for line in lines)


def test_read_only_register(shell):
    """Verify write to read-only register fails."""
    lines = shell.exec_command("settings reg_write 0x0106 1")  # motor.status is RO
    assert any("failed" in line.lower() for line in lines)


def test_invalid_address(shell):
    """Verify reg_read fails for invalid address."""
    lines = shell.exec_command("settings reg_read 0x9999")
    assert any("No area" in line for line in lines)


def test_float_register(shell):
    """Verify float register works correctly."""
    # sensor.gain at 0x0204 is f32, default 1.0
    lines = shell.exec_command("settings reg_read 0x0204")
    assert any("1.00" in line or "1.0" in line for line in lines)
    # Write new value
    shell.exec_command("settings reg_write 0x0204 2.5")
    lines = shell.exec_command("settings reg_read 0x0204")
    assert any("2.5" in line for line in lines)


def test_signed_register(shell):
    """Verify signed register handles negative values."""
    # sensor.temp_offset at 0x0202 is i16, range -50 to 50
    shell.exec_command("settings reg_write 0x0202 -25")
    lines = shell.exec_command("settings reg_read 0x0202")
    assert any("-25" in line for line in lines)


def test_motor_observer_triggered(shell):
    """Verify motor observer is triggered on change."""
    lines = shell.exec_command("settings reg_write 0x0100 7777")
    # The motor_on_change callback logs the new values inline with shell output
    assert any("speed=7777" in line for line in lines)


def test_sensor_observer_triggered(shell):
    """Verify sensor observer is triggered on change."""
    lines = shell.exec_command("settings reg_write 0x0200 500")
    # The sensor_on_change callback logs the new values inline with shell output
    assert any("rate=500" in line for line in lines)
