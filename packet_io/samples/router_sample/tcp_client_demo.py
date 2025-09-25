#!/usr/bin/env python3
import socket
import struct
import time
import argparse
import sys

# Protocol constants
IOTSENSE_VERSION = 0x01  # Simplified protocol version

# Packet IDs (must fit in 8 bits now)
PKT_ID_SYSTEM_CONFIG = 0x09
PKT_ID_SENSOR_TEMP = 0x01
PKT_ID_ACTUATOR_LED = 0x02

# System commands
SYS_CMD_PING = 0x01
SYS_CMD_GET_VERSION = 0x02
SYS_CMD_GET_STATUS = 0x03
SYS_CMD_SET_CONFIG = 0x04
SYS_CMD_RESET = 0x05
SYS_CMD_GET_STATS = 0x06
SYS_CMD_FLAG_ACK_REQUIRED = 0x80

# LED commands
LED_CMD_OFF = 0x00
LED_CMD_ON = 0x01
LED_CMD_TOGGLE = 0x02
LED_CMD_BLINK = 0x03

# LED IDs
LED_ID_USER1 = 0x01
LED_ID_USER2 = 0x02
LED_ID_ALL = 0xFF


class IoTSenseClient:
    def __init__(self, host='127.0.0.1', port=8080):
        self.host = host
        self.port = port
        self.socket = None

    def connect(self):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.settimeout(5.0)
        try:
            self.socket.connect((self.host, self.port))
            print(f"Connected to {self.host}:{self.port}")
            return True
        except Exception as e:
            print(f"Connection failed: {e}")
            return False

    def disconnect(self):
        if self.socket:
            self.socket.close()
            self.socket = None

    def create_packet(self, packet_id, payload):
        # Simple 4-byte header: version(8), packet_id(8), payload_len(16)
        header = struct.pack('<BBH',
            IOTSENSE_VERSION,
            packet_id,
            len(payload)
        )
        return header + payload

    def send_packet(self, packet_id, payload):
        packet = self.create_packet(packet_id, payload)
        try:
            self.socket.send(packet)
            return True
        except Exception as e:
            print(f"Send failed: {e}")
            return False

    def receive_packets(self, timeout=1.0):
        packets = []
        self.socket.settimeout(timeout)

        try:
            data = self.socket.recv(1024)
            if not data:
                return packets

            offset = 0
            while offset + 4 <= len(data):  # 4-byte header
                ver, pkt_id, payload_len = struct.unpack_from(
                    '<BBH', data, offset
                )

                if ver != IOTSENSE_VERSION:
                    offset += 1
                    continue

                total_size = 4 + payload_len
                if offset + total_size <= len(data):
                    payload = data[offset + 4:offset + total_size]
                    packets.append({
                        'id': pkt_id,
                        'payload': payload,
                        'size': total_size
                    })
                    offset += total_size
                else:
                    break

        except socket.timeout:
            pass
        except Exception as e:
            print(f"Receive error: {e}")

        return packets

    def send_get_status(self):
        payload = struct.pack('<BBHI',
            SYS_CMD_GET_STATUS,
            SYS_CMD_FLAG_ACK_REQUIRED,
            0,
            0
        )

        if self.send_packet(PKT_ID_SYSTEM_CONFIG, payload):
            packets = self.receive_packets(timeout=1.0)
            for pkt in packets:
                if pkt['id'] == PKT_ID_SYSTEM_CONFIG:
                    if len(pkt['payload']) >= 8:
                        cmd, status, result_len, result_code = struct.unpack_from('<BBHI', pkt['payload'], 0)
                        print(f"Status response: cmd=0x{cmd:02X}, status=0x{status:02X}")
                        if result_len >= 12 and len(pkt['payload']) >= 20:
                            uptime, heap, threads, cpu, temp = struct.unpack_from('<IIHBB', pkt['payload'], 8)
                            print(f"  Uptime: {uptime}s, Heap: {heap}B, Threads: {threads}, CPU: {cpu}%, Temp: {temp}°C")
                    return True
            print("No status response")
            return False

    def send_led_blink(self, led_id=LED_ID_USER1, duration_ms=2000):
        payload = struct.pack('<BBBBHHI',
            led_id, LED_CMD_BLINK, 200, 0,
            duration_ms, 0, 0x00FF00
        )
        return self.send_packet(PKT_ID_ACTUATOR_LED, payload)

    def send_led_on(self, led_id=LED_ID_USER2):
        payload = struct.pack('<BBBBHHI',
            led_id, LED_CMD_ON, 255, 0, 0, 0, 0x0000FF
        )
        return self.send_packet(PKT_ID_ACTUATOR_LED, payload)

    def send_led_off(self, led_id=LED_ID_ALL):
        payload = struct.pack('<BBBBHHI',
            led_id, LED_CMD_OFF, 0, 0, 0, 0, 0
        )
        return self.send_packet(PKT_ID_ACTUATOR_LED, payload)

    def receive_temperature_data(self, duration=3):
        start = time.time()
        count = 0
        self.socket.settimeout(0.2)

        while time.time() - start < duration:
            try:
                data = self.socket.recv(1024)
                if data:
                    offset = 0
                    while offset + 4 <= len(data):  # 4-byte header
                        ver, pkt_id, payload_len = struct.unpack_from(
                            '<BBH', data, offset
                        )

                        if ver != IOTSENSE_VERSION:
                            offset += 1
                            continue

                        total_size = 4 + payload_len
                        if offset + total_size <= len(data):
                            if pkt_id == PKT_ID_SENSOR_TEMP:
                                count += 1
                                payload = data[offset + 4:offset + total_size]
                                if len(payload) >= 8:
                                    temp_c, humidity_rh, sensor_id, sample_count = struct.unpack_from('<hhHH', payload, 0)
                                    print(f"Temp: {temp_c/100:.2f}°C, Humidity: {humidity_rh/100:.2f}%")
                            offset += total_size
                        else:
                            break
            except socket.timeout:
                pass
            except Exception as e:
                print(f"Receive error: {e}")
                break

        return count


def run_demo(client):
    print("\nReceiving temperature...")
    count = client.receive_temperature_data(2)
    print(f"Got {count} packets")

    print("\nStatus check...")
    client.send_get_status()

    print("\nLED sequence...")
    client.send_led_blink(LED_ID_USER1, 1500)
    time.sleep(0.5)
    client.send_led_on(LED_ID_USER2)
    time.sleep(0.5)
    client.send_led_off(LED_ID_ALL)

    print("\nFinal temperature check...")
    count = client.receive_temperature_data(2)
    print(f"Got {count} packets")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=8080)
    parser.add_argument('--demo', action='store_true')
    args = parser.parse_args()

    client = IoTSenseClient(args.host, args.port)
    if not client.connect():
        sys.exit(1)

    try:
        if args.demo:
            run_demo(client)
        else:
            print("\n1=status, 2=blink, 3=on, 4=off, 5=temp, q=quit")
            while True:
                cmd = input("> ").strip()
                if cmd == '1':
                    client.send_get_status()
                elif cmd == '2':
                    client.send_led_blink()
                    print("LED blink sent")
                elif cmd == '3':
                    client.send_led_on()
                    print("LED on sent")
                elif cmd == '4':
                    client.send_led_off()
                    print("LED off sent")
                elif cmd == '5':
                    count = client.receive_temperature_data(3)
                    print(f"Received {count} temperature packets")
                elif cmd == 'q':
                    break

    except KeyboardInterrupt:
        pass
    finally:
        client.disconnect()


if __name__ == '__main__':
    main()