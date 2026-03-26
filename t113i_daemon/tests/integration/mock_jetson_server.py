#!/usr/bin/env python3
"""
mock_jetson_server.py – Python mock server simulating the Jetson receiver.

Usage:
    python3 mock_jetson_server.py [--host HOST] [--port PORT] [--verbose]

Listens on a TCP port, decodes binary frames from the T113i daemon,
responds with appropriate ACKs and handshake replies, and prints a
summary of received messages.

Binary frame format:
    [0xAA][0x55][msg_id:1][seq:2LE][len:2LE][reserved:2][payload:len][crc16:2LE]

CRC-16/CCITT (poly=0x1021, init=0xFFFF)
"""

import argparse
import socket
import struct
import sys
import threading
import time
from typing import Optional, Tuple

# ---------------------------------------------------------------------------
# Protocol constants (mirror protocol.h)
# ---------------------------------------------------------------------------
PROTO_HEADER_0   = 0xAA
PROTO_HEADER_1   = 0x55
PROTO_HEADER_TOT = 9   # AA55 + id(1) + seq(2) + len(2) + reserved(2)
PROTO_CRC_SZ     = 2
PROTO_MAX_PAYLOAD = 256

PROTOCOL_VERSION = 0x0100

MSG_HEARTBEAT      = 0x01
MSG_ACK            = 0x02
MSG_HANDSHAKE_REQ  = 0x03
MSG_HANDSHAKE_ACK  = 0x04
MSG_SENSOR_DATA    = 0x10
MSG_GPIO_STATUS    = 0x11
MSG_SYSTEM_STATUS  = 0x13
MSG_DO_COMMAND     = 0x20
MSG_PWM_COMMAND    = 0x21
MSG_POWER_COMMAND  = 0x22
MSG_TASK_COMMAND   = 0x23
MSG_TASK_RESPONSE  = 0x24
MSG_EVENT          = 0x30

MSG_NAMES = {
    MSG_HEARTBEAT: "HEARTBEAT",
    MSG_ACK: "ACK",
    MSG_HANDSHAKE_REQ: "HANDSHAKE_REQ",
    MSG_HANDSHAKE_ACK: "HANDSHAKE_ACK",
    MSG_SENSOR_DATA: "SENSOR_DATA",
    MSG_GPIO_STATUS: "GPIO_STATUS",
    MSG_SYSTEM_STATUS: "SYSTEM_STATUS",
    MSG_DO_COMMAND: "DO_COMMAND",
    MSG_PWM_COMMAND: "PWM_COMMAND",
    MSG_POWER_COMMAND: "POWER_COMMAND",
    MSG_TASK_COMMAND: "TASK_COMMAND",
    MSG_TASK_RESPONSE: "TASK_RESPONSE",
    MSG_EVENT: "EVENT",
}

FEAT_ALL = 0x1F


# ---------------------------------------------------------------------------
# CRC-16/CCITT
# ---------------------------------------------------------------------------
def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


# ---------------------------------------------------------------------------
# Frame encoder
# ---------------------------------------------------------------------------
def encode_frame(msg_id: int, seq: int, payload: bytes) -> bytes:
    header = bytes([
        PROTO_HEADER_0, PROTO_HEADER_1, msg_id,
        seq & 0xFF, (seq >> 8) & 0xFF,
        len(payload) & 0xFF, (len(payload) >> 8) & 0xFF,
        0, 0,  # reserved
    ])
    body = header + payload
    crc = crc16_ccitt(body)
    return body + struct.pack('<H', crc)


# ---------------------------------------------------------------------------
# Frame decoder (stateful)
# ---------------------------------------------------------------------------
class FrameDecoder:
    def __init__(self):
        self._buf = bytearray()

    def feed(self, data: bytes):
        """Feed bytes; yields (msg_id, seq, payload) for each valid frame."""
        self._buf.extend(data)
        while True:
            frame = self._try_parse()
            if frame is None:
                break
            yield frame

    def _try_parse(self) -> Optional[Tuple[int, int, bytes]]:
        buf = self._buf
        # Find header
        while len(buf) >= 2 and not (buf[0] == PROTO_HEADER_0 and
                                      buf[1] == PROTO_HEADER_1):
            buf.pop(0)

        if len(buf) < PROTO_HEADER_TOT + PROTO_CRC_SZ:
            return None

        msg_id  = buf[2]
        seq     = buf[3] | (buf[4] << 8)
        pay_len = buf[5] | (buf[6] << 8)

        if pay_len > PROTO_MAX_PAYLOAD:
            # Bad length: skip this header byte
            buf.pop(0)
            return None

        total = PROTO_HEADER_TOT + pay_len + PROTO_CRC_SZ
        if len(buf) < total:
            return None

        frame_bytes = bytes(buf[:total])
        crc_calc = crc16_ccitt(frame_bytes[:-2])
        crc_recv = frame_bytes[-2] | (frame_bytes[-1] << 8)

        del buf[:total]

        if crc_calc != crc_recv:
            print(f"[WARN] CRC mismatch: calc={crc_calc:#06x} recv={crc_recv:#06x}")
            return None

        payload = frame_bytes[PROTO_HEADER_TOT:PROTO_HEADER_TOT + pay_len]
        return msg_id, seq, payload


# ---------------------------------------------------------------------------
# Client handler
# ---------------------------------------------------------------------------
class ClientHandler(threading.Thread):
    def __init__(self, conn: socket.socket, addr, verbose: bool):
        super().__init__(daemon=True)
        self._conn    = conn
        self._addr    = addr
        self._verbose = verbose
        self._decoder = FrameDecoder()
        self._seq     = 0
        self._stats   = {name: 0 for name in MSG_NAMES.values()}
        self._stats['UNKNOWN'] = 0

    def run(self):
        print(f"[Server] Client connected from {self._addr}")
        try:
            while True:
                data = self._conn.recv(4096)
                if not data:
                    break
                for msg_id, seq, payload in self._decoder.feed(data):
                    self._handle(msg_id, seq, payload)
        except Exception as e:
            print(f"[Server] Client error: {e}")
        finally:
            print(f"[Server] Client disconnected. Stats: {self._stats}")
            self._conn.close()

    def _send(self, msg_id: int, payload: bytes):
        frame = encode_frame(msg_id, self._seq, payload)
        self._seq = (self._seq + 1) & 0xFFFF
        try:
            self._conn.sendall(frame)
        except Exception:
            pass

    def _handle(self, msg_id: int, seq: int, payload: bytes):
        name = MSG_NAMES.get(msg_id, f"0x{msg_id:02X}")
        self._stats[name] = self._stats.get(name, 0) + 1

        if self._verbose:
            print(f"[RX] {name} seq={seq} len={len(payload)}")

        if msg_id == MSG_HANDSHAKE_REQ:
            if len(payload) >= 10:
                version, features, ts = struct.unpack_from('<HII', payload)
                if self._verbose:
                    print(f"  Handshake: version=0x{version:04X} features=0x{features:08X}")
                # Send ACK: version(2) + negotiated(4) + role(1)
                ack = struct.pack('<HIB',
                    PROTOCOL_VERSION,
                    features & FEAT_ALL,
                    1)  # role=1 (Jetson)
                self._send(MSG_HANDSHAKE_ACK, ack)
                print(f"[Server] Handshake ACK sent (version=0x{PROTOCOL_VERSION:04X})")

        elif msg_id == MSG_HEARTBEAT:
            # Echo heartbeat back
            hb = struct.pack('<IB', int(time.time() * 1000) & 0xFFFFFFFF, 1)
            self._send(MSG_HEARTBEAT, hb)

        elif msg_id == MSG_TASK_COMMAND:
            if self._verbose and len(payload) >= 5:
                task_id = payload[0]
                arg = struct.unpack_from('<I', payload, 1)[0]
                print(f"  Task: id=0x{task_id:02X} arg={arg}")
            # Send ACK
            ack = struct.pack('<BBH', msg_id, seq & 0xFF, seq)
            self._send(MSG_ACK, ack[:4])

        elif msg_id == MSG_DO_COMMAND:
            ack = struct.pack('<BBH', msg_id, seq & 0xFF, 0)
            self._send(MSG_ACK, ack)

        elif msg_id == MSG_POWER_COMMAND:
            ack = struct.pack('<BBH', msg_id, seq & 0xFF, 0)
            self._send(MSG_ACK, ack)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="Mock Jetson TCP server")
    parser.add_argument('--host', default='0.0.0.0', help='Bind address')
    parser.add_argument('--port', type=int, default=9000, help='TCP port')
    parser.add_argument('--verbose', '-v', action='store_true')
    args = parser.parse_args()

    server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_sock.bind((args.host, args.port))
    server_sock.listen(5)

    print(f"[Server] Listening on {args.host}:{args.port} (CTRL+C to stop)")

    try:
        while True:
            conn, addr = server_sock.accept()
            handler = ClientHandler(conn, addr, args.verbose)
            handler.start()
    except KeyboardInterrupt:
        print("\n[Server] Shutting down")
    finally:
        server_sock.close()


if __name__ == '__main__':
    main()
