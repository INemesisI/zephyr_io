# pytest-test-harness

Pytest plugin providing hardware testing fixtures for the Zephyr IO project. Extends pytest-twister-harness with TCP connections, HTTP client, and custom binary flashing.

## Installation

```bash
pip install -e scripts/pylib/pytest-test-harness
```

**Requirements:**
- Python >= 3.10
- pytest >= 7.0.0
- pytest-twister-harness (installed from Zephyr tree)

## Fixtures

### TCP Connection

Connect to devices over TCP for network-based testing.

#### Hardware Map Configuration

```yaml
- connected: true
  id: 000683759358
  platform: nrf52840dk/nrf52840
  product: J-Link
  runner: nrfjprog
  serial: /dev/ttyACM0
  fixtures:
    - tcp:192.168.1.100:5000
```

The `tcp:ip:port` fixture format specifies the device's TCP endpoint.

#### Available Fixtures

| Fixture | Scope | Description |
|---------|-------|-------------|
| `tcp_config` | function | Parsed `TcpConfig(ip, port)` from device fixtures |
| `tcp_connection` | function | Connected TCP socket, auto-closed after test |

#### Usage

```python
def test_tcp_echo(tcp_connection):
    """Basic TCP communication test."""
    tcp_connection.send(b'hello')
    response = tcp_connection.recv(1024)
    assert response == b'hello'


def test_with_config(tcp_config):
    """Access IP/port directly."""
    print(f"Device at {tcp_config.ip}:{tcp_config.port}")
```

#### Timeout Configuration

Timeout priority (highest to lowest):
1. `@pytest.mark.tcp_timeout(seconds)` marker on test
2. `--tcp-timeout` CLI option
3. Default: 10.0 seconds

```python
@pytest.mark.tcp_timeout(30.0)
def test_slow_operation(tcp_connection):
    """This test has a 30 second timeout."""
    tcp_connection.send(b'long_operation')
    response = tcp_connection.recv(1024, timeout=25)
    assert response == b'done'
```

```bash
# Set timeout for all tests
pytest --tcp-timeout=60
```

---

### Flash Device

Flash custom binaries to test firmware updates, bootloaders, or recovery scenarios.

#### Hardware Map Configuration

Uses existing device configuration from the hardware map:

```yaml
- connected: true
  id: 000683759358
  platform: nrf52840dk/nrf52840
  product: J-Link
  runner: nrfjprog
  serial: /dev/ttyACM0
  runner_params:
    - --softreset
```

The `flash_device` fixture automatically uses:
- `runner` - Flash tool (nrfjprog, jlink, pyocd, openocd, etc.)
- `id` - Board identifier for multi-device setups
- `runner_params` - Additional runner arguments
- `product` - Used for runner-specific board ID handling

#### Available Fixtures

| Fixture | Scope | Description |
|---------|-------|-------------|
| `flash_device` | function | Callable to flash custom binaries |

#### Usage

```python
def test_firmware_update(dut, flash_device):
    """Test firmware upgrade path."""
    # Flash initial version
    flash_device("binaries/firmware_v1.hex")
    dut.readlines_until(regex="v1.0.0 running", timeout=10.0)

    # Flash updated version
    flash_device("binaries/firmware_v2.hex")
    dut.readlines_until(regex="v2.0.0 running", timeout=10.0)

    # Verify upgrade succeeded
    dut.readlines_until(regex="Migration complete", timeout=5.0)


def test_bootloader_recovery(dut, flash_device):
    """Test recovery from corrupted firmware."""
    # Flash known-bad firmware
    flash_device("binaries/corrupted.bin")

    # Bootloader should detect and wait for recovery
    dut.readlines_until(regex="Recovery mode", timeout=30.0)

    # Flash good firmware
    flash_device("binaries/firmware_good.bin")
    dut.readlines_until(regex="Boot successful", timeout=10.0)
```

#### Supported File Types

The fixture auto-detects file type by extension:

| Extension | West Flash Flag | Notes |
|-----------|-----------------|-------|
| `.bin` | `--bin-file` | Requires `offset` parameter |
| `.hex` | `--hex-file` | Contains address info |
| `.elf` | `--elf-file` | Contains address info |

#### Flash Options

```python
# Basic usage (hex/elf files contain address info)
flash_device("firmware.hex")

# Binary files require a load address offset
flash_device("firmware.bin", offset=0x8000)
flash_device("firmware.bin", offset="0x08000000")  # String also works

# With custom build directory (for runner config)
flash_device("firmware.hex", build_dir="path/to/build")

# With custom timeout
flash_device("firmware.hex", timeout=120.0)

# Combined options
flash_device("app.bin", offset=0x10000, timeout=120.0)
```

#### Supported Runners

Board ID handling matches pytest-twister-harness for these runners:
- `pyocd` - `--board-id`
- `nrfjprog` / `nrfutil` - `--dev-id`
- `jlink` - `-SelectEmuBySN`
- `openocd` - Various `--cmd-pre-init` options based on product
- `stm32cubeprogrammer` - `sn=`
- `linkserver` - `--probe`

---

### HTTP Client

Test REST APIs and web interfaces on devices.

#### Hardware Map Configuration

```yaml
- connected: true
  id: 000683759358
  platform: nrf52840dk/nrf52840
  fixtures:
    - http:192.168.1.100:8080
    # Or for HTTPS:
    # - https:192.168.1.100:443
```

#### Available Fixtures

| Fixture | Scope | Description |
|---------|-------|-------------|
| `http_config` | function | Parsed `HttpConfig(host, port, scheme)` from device fixtures |
| `http_client` | function | `HttpClient` instance with GET/POST/PUT/DELETE/PATCH methods |

#### Basic Usage

```python
def test_api_status(http_client):
    """Test REST API endpoint."""
    response = http_client.get('/api/status')
    assert response.status_code == 200
    assert response.json()['state'] == 'running'


def test_post_config(http_client):
    """Send JSON configuration."""
    response = http_client.post('/api/config', json={
        'setting': 'value',
        'enabled': True
    })
    assert response.status_code == 200
```

#### File Upload

```python
def test_firmware_upload(http_client):
    """Upload firmware via HTTP."""
    response = http_client.upload_file(
        '/api/firmware/upload',
        file_path='binaries/firmware_v2.bin',
        field_name='firmware',  # Form field name (default: 'file')
        extra_data={'version': '2.0.0'}  # Additional form fields
    )
    assert response.status_code == 200


def test_upload_with_post(http_client):
    """Upload using raw post with files parameter."""
    with open('config.json', 'rb') as f:
        response = http_client.post(
            '/api/config/import',
            files={'config': ('config.json', f, 'application/json')}
        )
    assert response.status_code == 200
```

#### File Download

```python
def test_log_download(http_client, tmp_path):
    """Download file from device."""
    response = http_client.download_file(
        '/api/logs/system.log',
        save_path=tmp_path / 'system.log'
    )
    assert response.status_code == 200
    assert (tmp_path / 'system.log').exists()
```

#### All HTTP Methods

```python
def test_crud_operations(http_client):
    # Create
    response = http_client.post('/api/items', json={'name': 'test'})
    item_id = response.json()['id']

    # Read
    response = http_client.get(f'/api/items/{item_id}')
    assert response.json()['name'] == 'test'

    # Update (full)
    response = http_client.put(f'/api/items/{item_id}', json={'name': 'updated'})

    # Update (partial)
    response = http_client.patch(f'/api/items/{item_id}', json={'status': 'active'})

    # Delete
    response = http_client.delete(f'/api/items/{item_id}')
    assert response.status_code == 204
```

#### Timeout Configuration

Timeout priority (highest to lowest):
1. `@pytest.mark.http_timeout(seconds)` marker on test
2. `--http-timeout` CLI option
3. Default: 10.0 seconds

```python
@pytest.mark.http_timeout(60.0)
def test_slow_upload(http_client):
    """Large file upload with extended timeout."""
    response = http_client.upload_file('/api/upload', 'large_file.bin')
    assert response.status_code == 200
```

---

### UDP Server

Receive UDP packets from devices for testing broadcast, multicast, or unicast communication.

#### Hardware Map Configuration

```yaml
- connected: true
  id: 000683759358
  platform: nrf52840dk/nrf52840
  fixtures:
    - udp:5000
    # Or with explicit bind address:
    # - udp:5000:0.0.0.0    # all interfaces (default)
    # - udp:5000:127.0.0.1  # localhost only
```

The `udp:port:bind_address` fixture format specifies:
- `port`: Port to listen on
- `bind_address`: Interface to bind to (default: 0.0.0.0)

#### Available Fixtures

| Fixture | Scope | Description |
|---------|-------|-------------|
| `udp_config` | function | Parsed `UdpConfig(port, bind_address)` from device fixtures |
| `udp_server` | function | `UdpServer` instance, auto-started and stopped |

#### Basic Usage

```python
def test_device_broadcast(udp_server, dut):
    """Receive broadcast packet from device."""
    # Trigger device to send
    dut.write(b'send_status\n')

    # Receive the packet
    data = udp_server.recv(timeout=5.0)
    assert b'status:ok' in data


def test_with_sender_info(udp_server, dut):
    """Receive packet with sender address."""
    dut.write(b'ping\n')

    packet = udp_server.recv_from(timeout=5.0)
    print(f"From {packet.sender_ip}:{packet.sender_port}")
    assert packet.data == b'pong'
```

#### Replying to Device

```python
def test_request_response(udp_server, dut):
    """Test request/response over UDP."""
    # Device sends request
    dut.write(b'start_discovery\n')

    # Receive discovery packet
    packet = udp_server.recv_from(timeout=5.0)
    assert b'discover' in packet.data

    # Reply to the device
    udp_server.reply(b'server_here')

    # Or send to specific address
    udp_server.send_to(b'response', ('192.168.1.100', 5001))

    # Verify device received reply
    dut.readlines_until(regex="server found")
```

#### Multiple Packets

```python
def test_packet_stream(udp_server, dut):
    """Receive multiple packets."""
    dut.write(b'stream_data\n')

    packets = []
    for _ in range(10):
        packet = udp_server.recv_from(timeout=2.0)
        packets.append(packet.data)

    assert len(packets) == 10


def test_clear_stale_packets(udp_server, dut):
    """Clear any pending packets before test."""
    # Server auto-clears on start, but can clear manually
    cleared = udp_server.clear(timeout=0.1)
    print(f"Cleared {cleared} stale packets")

    # Now run the actual test
    dut.write(b'send_once\n')
    data = udp_server.recv(timeout=5.0)
```

#### Timeout Configuration

Timeout priority (highest to lowest):
1. `timeout` parameter on `recv()` / `recv_from()`
2. `@pytest.mark.udp_timeout(seconds)` marker on test
3. `--udp-timeout` CLI option
4. Default: 10.0 seconds

```python
@pytest.mark.udp_timeout(30.0)
def test_slow_device(udp_server, dut):
    """Device takes a while to respond."""
    dut.write(b'long_operation\n')
    data = udp_server.recv()  # Uses 30s timeout from marker
    assert b'done' in data
```

---

## CLI Options

| Option | Default | Description |
|--------|---------|-------------|
| `--tcp-timeout` | 10.0 | TCP connection timeout in seconds |
| `--http-timeout` | 10.0 | HTTP request timeout in seconds |
| `--udp-timeout` | 10.0 | UDP receive timeout in seconds |

## Markers

| Marker | Description |
|--------|-------------|
| `@pytest.mark.tcp_timeout(seconds)` | Override TCP timeout for a specific test |
| `@pytest.mark.http_timeout(seconds)` | Override HTTP timeout for a specific test |
| `@pytest.mark.udp_timeout(seconds)` | Override UDP timeout for a specific test |

## Test Configuration

In your `testcase.yaml`:

```yaml
tests:
  myapp.hardware_test:
    harness: pytest
    harness_config:
      fixture: tcp              # Require TCP fixture
      pytest_root:
        - "pytest/"
```

## Project Structure

```
test_harness/
├── __init__.py           # Package version, lazy exports
├── plugin.py             # Pytest hooks, CLI options, markers
└── fixtures/
    ├── __init__.py       # Re-exports all fixtures
    ├── tcp.py            # TCP client fixtures
    ├── udp.py            # UDP server fixtures
    ├── http.py           # HTTP client fixtures
    └── flash.py          # Flash device fixtures
```

## Adding New Fixtures

1. Create a new file in `fixtures/` (e.g., `fixtures/uart.py`)
2. Define your fixtures with `@pytest.fixture`
3. Add exports to `fixtures/__init__.py`
4. Add any CLI options or markers to `plugin.py`

## License

Apache-2.0
