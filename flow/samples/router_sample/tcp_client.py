#!/usr/bin/env python3
"""
TCP Client for Flow Router Sample

Can be used as:
1. A library: from tcp_client import SampleClient
2. A CLI tool: python tcp_client.py [start|stop]
"""

import socket
import struct
import sys
import threading
import time
from dataclasses import dataclass
from typing import Optional, Callable


# Command definitions
CMD_START_SAMPLING = 0x01
CMD_STOP_SAMPLING = 0x02


@dataclass
class SensorPacket:
    """Represents a received sensor packet"""
    packet_id: int
    counter: int
    timestamp_ns: int
    content_length: int
    payload: bytes

    @property
    def sensor_name(self) -> str:
        return f"SENSOR{self.packet_id}"

    @property
    def timestamp_ms(self) -> float:
        return self.timestamp_ns / 1_000_000


class SampleClient:
    """TCP client for Flow Router sample"""

    def __init__(self, host: str = "127.0.0.1", port: int = 4242, timeout: float = 5.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock: Optional[socket.socket] = None
        self.connected = False
        self.packet_count = 0
        self.bytes_received = 0

    def connect(self) -> bool:
        """Connect to TCP server"""
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(self.timeout)
            self.sock.connect((self.host, self.port))
            self.connected = True
            return True
        except Exception as e:
            print(f"Connection failed: {e}")
            return False

    def disconnect(self):
        """Disconnect from server"""
        self.connected = False
        if self.sock:
            self.sock.close()
            self.sock = None

    def send_command(self, cmd: int) -> bool:
        """Send a command byte to the server"""
        if not self.sock:
            return False

        try:
            self.sock.send(bytes([cmd]))
            return True
        except Exception as e:
            print(f"Failed to send command: {e}")
            return False

    def start_sampling(self) -> bool:
        """Send START sampling command"""
        return self.send_command(CMD_START_SAMPLING)

    def stop_sampling(self) -> bool:
        """Send STOP sampling command"""
        return self.send_command(CMD_STOP_SAMPLING)

    def receive_packet(self) -> Optional[SensorPacket]:
        """
        Receive a single packet from the server.
        Returns SensorPacket or None on error/timeout.
        """
        if not self.sock:
            return None

        try:
            # Read packet header (14 bytes)
            header_data = self._recv_exact(14)
            if not header_data:
                return None

            # Unpack header: packet_id, reserved, counter, content_length, timestamp_ns
            packet_id, reserved, counter, content_length, timestamp_ns = struct.unpack("<BBHHQ", header_data)

            # Read payload
            payload = self._recv_exact(content_length)
            if not payload:
                return None

            self.packet_count += 1
            self.bytes_received += 14 + content_length

            return SensorPacket(
                packet_id=packet_id,
                counter=counter,
                timestamp_ns=timestamp_ns,
                content_length=content_length,
                payload=payload
            )

        except socket.timeout:
            return None
        except Exception as e:
            if self.connected:
                print(f"Error receiving packet: {e}")
            return None

    def receive_packets(self, count: int, timeout: float = None) -> list[SensorPacket]:
        """
        Receive a specific number of packets.
        Returns list of received packets (may be less than count if timeout).
        """
        packets = []
        old_timeout = self.sock.gettimeout() if self.sock else None

        try:
            if timeout is not None and self.sock:
                self.sock.settimeout(timeout)

            for _ in range(count):
                packet = self.receive_packet()
                if packet is None:
                    break
                packets.append(packet)

        finally:
            if old_timeout is not None and self.sock:
                self.sock.settimeout(old_timeout)

        return packets

    def receive_with_callback(self, callback: Callable[[SensorPacket], bool], max_packets: int = 100):
        """
        Receive packets and call callback for each.
        Callback should return False to stop receiving.
        """
        for _ in range(max_packets):
            packet = self.receive_packet()
            if packet is None:
                break

            if not callback(packet):
                break

    def _recv_exact(self, n: int) -> Optional[bytes]:
        """Receive exactly n bytes from socket"""
        data = bytearray()
        while len(data) < n:
            try:
                chunk = self.sock.recv(n - len(data))
                if not chunk:
                    return None
                data.extend(chunk)
            except socket.timeout:
                return None
        return bytes(data)

    def __enter__(self):
        """Context manager entry"""
        self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit"""
        self.disconnect()


# CLI functionality when run as main
def print_usage():
    """Print usage information"""
    print("""
Flow Router Sample - TCP Client
================================

Usage: python tcp_client.py [command]

Commands:
  start   - Start sensor sampling
  stop    - Stop sensor sampling
  (none)  - Interactive mode

Interactive mode commands:
  s - Start sampling
  t - Stop sampling
  q - Quit

The client receives and displays sensor data packets with metadata.
""")


def receive_packets_thread(client: SampleClient):
    """Background thread to receive and display packets"""
    def display_packet(packet: SensorPacket) -> bool:
        print(f"\n[{packet.sensor_name}] Packet #{packet.counter}")
        print(f"  Timestamp: {packet.timestamp_ms:.3f} ms")
        print(f"  Size: {packet.content_length} bytes")

        # Show first 16 bytes of payload
        if packet.payload:
            preview = packet.payload[:min(16, len(packet.payload))]
            hex_str = " ".join(f"{b:02x}" for b in preview)
            if len(packet.payload) > 16:
                hex_str += "..."
            print(f"  Data: {hex_str}")

        return True  # Continue receiving

    try:
        client.receive_with_callback(display_packet, max_packets=10000)
    except KeyboardInterrupt:
        pass


def main():
    """Main entry point for CLI"""
    if len(sys.argv) > 1 and sys.argv[1] in ["-h", "--help", "help"]:
        print_usage()
        return

    with SampleClient() as client:
        if not client.connected:
            print("Failed to connect to server")
            return

        print(f"Connected to {client.host}:{client.port}")

        # Handle command line arguments
        if len(sys.argv) > 1:
            cmd = sys.argv[1].lower()
            if cmd == "start":
                client.start_sampling()
                print("Sent START command")
            elif cmd == "stop":
                client.stop_sampling()
                print("Sent STOP command")
                return
            else:
                print(f"Unknown command: {cmd}")
                print_usage()
                return

        # Start receiving packets in background
        recv_thread = threading.Thread(target=receive_packets_thread, args=(client,), daemon=True)
        recv_thread.start()

        # Interactive command loop
        print("\nInteractive commands: 's'=start, 't'=stop, 'q'=quit")
        try:
            while client.connected:
                cmd = input("> ").strip().lower()
                if cmd == 's':
                    if client.start_sampling():
                        print("Started sampling")
                elif cmd == 't':
                    if client.stop_sampling():
                        print("Stopped sampling")
                elif cmd == 'q':
                    break
                elif cmd:
                    print("Unknown command. Use 's', 't', or 'q'")
        except (KeyboardInterrupt, EOFError):
            print("\nExiting...")

        print(f"\nReceived {client.packet_count} packets ({client.bytes_received} bytes)")


if __name__ == "__main__":
    main()