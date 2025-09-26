#!/usr/bin/env python3
"""
IoTSense protocol client for testing the router sample.

This client demonstrates the IoTSense protocol by:
- Sending ping commands
- Toggling LEDs
- Receiving temperature sensor data
"""

import socket
import struct
import time
import argparse
import sys

# Protocol Constants
IOTSENSE_VERSION = 0x01

# Packet IDs (matching the sample implementation)
PKT_ID_SENSOR_TEMP = 0x01    # Temperature sensor data
PKT_ID_ACTUATOR_LED = 0x02   # LED control
PKT_ID_SYSTEM_CONFIG = 0x09  # System configuration

# Commands actually implemented in the sample
PING_CMD = 0x01         # Only ping is implemented
LED_TOGGLE_CMD = 0x02   # Only toggle is implemented


class IoTSenseClient:
    """Simple IoTSense protocol client."""

    def __init__(self, host='127.0.0.1', port=8080):
        self.host = host
        self.port = port
        self.socket = None

    def connect(self):
        """Connect to the router sample TCP server."""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(5.0)
            self.socket.connect((self.host, self.port))
            print(f"✓ Connected to {self.host}:{self.port}")
            return True
        except Exception as e:
            print(f"✗ Connection failed: {e}")
            return False

    def disconnect(self):
        """Close the connection."""
        if self.socket:
            self.socket.close()
            self.socket = None
            print("✓ Disconnected")

    def send_packet(self, packet_id, payload):
        """Send a packet with IoTSense header."""
        # Create 4-byte header: version(1) + packet_id(1) + length(2)
        header = struct.pack('<BBH', IOTSENSE_VERSION, packet_id, len(payload))
        packet = header + payload
        self.socket.sendall(packet)
        return True

    def receive_packet(self, timeout=5.0):
        """Receive a packet with IoTSense header."""
        self.socket.settimeout(timeout)

        try:
            # Read 4-byte header
            header = self._recv_exact(4)
            if not header:
                return None, None

            version, packet_id, length = struct.unpack('<BBH', header)

            # Read payload
            payload = self._recv_exact(length) if length > 0 else b''

            return packet_id, payload
        except socket.timeout:
            return None, None

    def _recv_exact(self, size):
        """Receive exactly size bytes."""
        data = b''
        while len(data) < size:
            chunk = self.socket.recv(size - len(data))
            if not chunk:
                return None
            data += chunk
        return data

    def ping(self, seq_num=1):
        """Send a ping command."""
        print(f"Sending PING (seq={seq_num})...")

        # Ping command format: command(1) + seq_num(2)
        payload = struct.pack('<BH', PING_CMD, seq_num)
        self.send_packet(PKT_ID_SYSTEM_CONFIG, payload)

        # Wait for response
        packet_id, resp = self.receive_packet()
        if packet_id == PKT_ID_SYSTEM_CONFIG and resp:
            cmd, resp_seq, timestamp = struct.unpack('<BHI', resp[:7])
            if cmd == PING_CMD and resp_seq == seq_num:
                print(f"✓ PONG received (seq={resp_seq}, time={timestamp}ms)")
                return True

        print("✗ No valid ping response")
        return False

    def toggle_led(self, led_id=1):
        """Toggle an LED (only toggle is supported)."""
        print(f"Toggling LED {led_id}...")

        # LED toggle format: command(1) + led_id(1)
        payload = struct.pack('BB', LED_TOGGLE_CMD, led_id)
        self.send_packet(PKT_ID_ACTUATOR_LED, payload)

        # LED controller doesn't send responses
        print("✓ Toggle command sent")
        return True

    def monitor(self, duration=10):
        """Monitor incoming sensor data."""
        print(f"Monitoring for {duration} seconds...")
        print("-" * 40)

        start_time = time.time()
        packet_count = 0

        while time.time() - start_time < duration:
            packet_id, payload = self.receive_packet(timeout=1.0)

            if packet_id == PKT_ID_SENSOR_TEMP and payload:
                # Temperature packet format: sensor_id(1) + timestamp(4) + temp_raw(4)
                if len(payload) >= 9:
                    sensor_id, timestamp, temp_raw = struct.unpack('<BII', payload[:9])
                    temp_c = (temp_raw / 100.0) - 273.15  # Convert from Kelvin*100
                    elapsed = time.time() - start_time
                    print(f"[{elapsed:6.1f}s] Sensor {sensor_id}: {temp_c:.1f}°C (ts={timestamp}ms)")
                    packet_count += 1

            elif packet_id is not None:
                elapsed = time.time() - start_time
                print(f"[{elapsed:6.1f}s] Packet ID {packet_id:#04x}: {len(payload)} bytes")
                packet_count += 1

        print("-" * 40)
        print(f"✓ Received {packet_count} packets")


def main():
    parser = argparse.ArgumentParser(
        description='IoTSense Protocol Client',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Commands:
  demo        Run demo sequence (default)
  ping        Send a ping command
  led         Toggle the LED
  monitor     Monitor sensor data

Examples:
  python3 iotsense_client.py                  # Run demo
  python3 iotsense_client.py ping             # Send ping
  python3 iotsense_client.py monitor --time 30  # Monitor for 30 seconds
  python3 iotsense_client.py --port 9000 led  # Toggle LED on port 9000
        """
    )

    parser.add_argument('--host', default='127.0.0.1', help='Server host (default: 127.0.0.1)')
    parser.add_argument('--port', type=int, default=8080, help='Server port (default: 8080)')
    parser.add_argument('command', nargs='?', default='demo',
                       choices=['demo', 'ping', 'led', 'monitor'],
                       help='Command to execute')
    parser.add_argument('--time', type=int, default=10,
                       help='Monitor duration in seconds (default: 10)')

    args = parser.parse_args()

    # Create and connect client
    client = IoTSenseClient(args.host, args.port)
    if not client.connect():
        return 1

    try:
        if args.command == 'demo':
            # Demo sequence
            print("\n=== Router Sample Demo ===\n")

            # Send a few pings
            for i in range(3):
                client.ping(seq_num=i+1)
                time.sleep(0.5)

            # Toggle LED a few times
            for i in range(3):
                client.toggle_led(led_id=1)
                time.sleep(1)

            # Monitor sensor data
            print()
            client.monitor(5)

        elif args.command == 'ping':
            client.ping(seq_num=1)

        elif args.command == 'led':
            client.toggle_led(led_id=1)

        elif args.command == 'monitor':
            client.monitor(args.time)

    except KeyboardInterrupt:
        print("\n\nInterrupted by user")
    except Exception as e:
        print(f"\nError: {e}")
        return 1
    finally:
        client.disconnect()

    return 0


if __name__ == '__main__':
    sys.exit(main())