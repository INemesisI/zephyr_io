# pytest-zephyr-io-harness

Pytest plugin providing fixtures for Zephyr IO project hardware testing.

## Installation

```bash
pip install -e scripts/pylib/pytest-zephyr-io-harness
```

## Fixtures

### TCP Connection

Configure TCP connection in your hardware map:

```yaml
- connected: true
  id: 000683759358
  platform: nrf52840dk/nrf52840
  fixtures:
    - tcp:192.168.1.100:5000
```

Use in tests:

```python
def test_tcp_communication(tcp_connection):
    tcp_connection.send(b'hello')
    response = tcp_connection.recv(1024)
    assert response == b'world'
```

### Available Fixtures

- `tcp_config` - Parsed TCP configuration (ip, port) from device fixtures
- `tcp_connection` - Connected TCP socket to the device
