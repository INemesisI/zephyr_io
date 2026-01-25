"""Tests for Weave Observable Register-Based Settings sample."""


def test_sample_initialization(dut):
    """Verify sample starts and shows banner."""
    dut.readlines_until(regex=".*Settings Register Sample.*", timeout=10.0)
    dut.readlines_until(regex=".*Settings initialized.*", timeout=2.0)


def test_reg_list(shell):
    """Verify reg list shows all registers programmatically."""
    lines = shell.exec_command("reg list")
    assert any("motor (base 0x100):" in line for line in lines)
    assert any("0x100: speed (u16)" in line for line in lines)
    assert any("0x106: status (u8)" in line for line in lines)
    assert any("0x107: temp (i8)" in line for line in lines)
    assert any("0x108: position (i16)" in line for line in lines)
    assert any("network (base 0x200):" in line for line in lines)
    assert any("0x200: ip_addr (u32)" in line for line in lines)


def test_get_all(shell):
    """Verify get shows all settings."""
    lines = shell.exec_command("get")
    assert any("motor:" in line and "speed=" in line for line in lines)
    assert any("network:" in line and "port=" in line for line in lines)


def test_reg_write_and_read(shell):
    """Verify write then read works."""
    shell.exec_command("reg write 0x100 1234")
    lines = shell.exec_command("reg read 0x100")
    assert any("motor.speed" in line and "= 1234" in line for line in lines)


def test_reg_write_motor_speed(shell):
    """Verify reg write updates motor speed and triggers observer."""
    lines = shell.exec_command("reg write 0x100 2000")
    assert any("motor.speed" in line and "= 2000" in line for line in lines)
    assert any("[MOTOR]" in line and "speed=2000" in line for line in lines)


def test_reg_write_motor_enabled(shell):
    """Verify reg write updates motor enabled and triggers observer."""
    lines = shell.exec_command("reg write 0x104 1")
    assert any("motor.enabled" in line and "= true" in line for line in lines)
    assert any("[MOTOR]" in line and "enabled=1" in line for line in lines)


def test_reg_write_network_port(shell):
    """Verify reg write updates network port and triggers observer."""
    lines = shell.exec_command("reg write 0x204 443")
    assert any("network.port" in line and "= 443" in line for line in lines)
    assert any("[NETWORK]" in line and "port=443" in line for line in lines)


def test_reg_read_invalid(shell):
    """Verify reg read fails for invalid register."""
    lines = shell.exec_command("reg read 0x999")
    assert any("Read failed" in line for line in lines)


def test_motor_speed_validation(shell):
    """Verify motor speed validation rejects values > 10000."""
    lines = shell.exec_command("reg write 0x100 10001")
    assert any("Write failed" in line for line in lines)


def test_motor_speed_valid(shell):
    """Verify motor speed validation accepts values <= 10000."""
    lines = shell.exec_command("reg write 0x100 10000")
    assert any("motor.speed" in line and "= 10000" in line for line in lines)


def test_motor_accel_validation(shell):
    """Verify motor accel validation rejects values > 5000."""
    lines = shell.exec_command("reg write 0x102 5001")
    assert any("Write failed" in line for line in lines)


def test_motor_direction_validation(shell):
    """Verify motor direction validation rejects values > 1."""
    lines = shell.exec_command("reg write 0x105 2")
    assert any("Write failed" in line for line in lines)


def test_network_port_validation(shell):
    """Verify network port validation rejects 0."""
    lines = shell.exec_command("reg write 0x204 0")
    assert any("Write failed" in line for line in lines)


# ==================== Branch Coverage Tests ====================


def test_read_only_field_write_rejected(shell):
    """Verify writing to read-only field fails with EACCES."""
    lines = shell.exec_command("reg write 0x106 1")
    assert any("Write failed" in line for line in lines)


def test_read_only_field_read(shell):
    """Verify reading read-only field works."""
    lines = shell.exec_command("reg read 0x106")
    assert any("motor.status" in line and "(u8)" in line for line in lines)


def test_signed_i8_type(shell):
    """Verify signed i8 type displays correctly."""
    shell.exec_command("reg write 0x107 200")  # 200 unsigned = -56 signed
    lines = shell.exec_command("reg read 0x107")
    assert any("motor.temp" in line and "(i8)" in line for line in lines)


def test_signed_i16_type(shell):
    """Verify signed i16 type displays correctly."""
    shell.exec_command("reg write 0x108 1000")
    lines = shell.exec_command("reg read 0x108")
    assert any("motor.position" in line and "(i16)" in line for line in lines)


def test_u8_type_direction(shell):
    """Verify u8 type displays correctly."""
    shell.exec_command("reg write 0x105 1")
    lines = shell.exec_command("reg read 0x105")
    assert any("motor.direction" in line and "(u8)" in line for line in lines)


def test_bulk_read_max_exceeded(shell):
    """Verify bulk read fails when requesting > 16 bytes."""
    lines = shell.exec_command("reg bulkr 0x100 20")
    assert any("Max 16 bytes" in line for line in lines)


def test_bulk_read_mid_struct(shell):
    """Verify bulk read from middle of struct works."""
    lines = shell.exec_command("reg bulkr 0x102 4")
    assert any("reg 0x0102" in line for line in lines)


def test_bulk_write(shell):
    """Verify bulk write with hex string works."""
    lines = shell.exec_command("reg bulkw 0x100 e803")  # 0x03e8 = 1000 little-endian
    assert any("Wrote 2 bytes" in line for line in lines)
    lines = shell.exec_command("reg read 0x100")
    assert any("= 1000" in line for line in lines)


def test_bulk_write_max_exceeded(shell):
    """Verify bulk write fails when sending > 16 bytes."""
    # 17 bytes = 34 hex chars
    lines = shell.exec_command("reg bulkw 0x100 0102030405060708090a0b0c0d0e0f1011")
    assert any("Max 16 bytes" in line for line in lines)


def test_bulk_write_odd_length(shell):
    """Verify bulk write fails with odd hex string length."""
    lines = shell.exec_command("reg bulkw 0x100 abc")
    assert any("even length" in line for line in lines)
