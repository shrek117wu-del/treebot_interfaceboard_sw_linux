#!/usr/bin/env python3
"""
mock_jetson_server.py – Simulates the Jetson Orin NX receiving T113i messages.

Features:
- Parses AA55 binary frames with CRC-16/CCITT validation
- Generates handshake ACK and heartbeat responses
- Logs received message IDs and sequence numbers
- Optional packet loss / delay injection via --loss and --delay_ms flags

Usage:
    python3 mock_jetson_server.py [--host HOST] [--port PORT]
                                  [--loss FRACTION] [--delay_ms MS]

Example:
    python3 mock_jetson_server.py --port 9000 --loss 0.05 --delay_ms 10
"""

import argparse
import logging
import random
import socket
import struct
import sys
import time
import threading

# ---------------------------------------------------------------------------
# Protocol constants (mirrors protocol.h)
# ---------------------------------------------------------------------------
PROTO_HEADER_0   = 0xAA
PROTO_HEADER_1   = 0x55
PROTO_HEADER_TOT = 9       # 2 sync + 1 id + 2 seq + 2 len + 2 reserved
PROTO_CRC_SZ     = 2
PROTOCOL_VERSION = 0x0100

MSG_HEARTBEAT      = 0x01
MSG_ACK            = 0x02
MSG_HANDSHAKE_REQ  = 0x03
MSG_HANDSHAKE_ACK  = 0x04
MSG_SENSOR_DATA    = 0x10
MSG_GPIO_STATUS    = 0x11
MSG_BATTERY_STATUS = 0x12
MSG_SYSTEM_STATUS  = 0x13
MSG_DO_COMMAND     = 0x20
MSG_PWM_COMMAND    = 0x21
MSG_POWER_COMMAND  = 0x22
MSG_TASK_COMMAND   = 0x23
MSG_TASK_RESPONSE  = 0x24
MSG_EVENT          = 0x30

FEAT_ALL = 0x1F

MSG_NAMES = {
    MSG_HEARTBEAT:      "HEARTBEAT",
    MSG_ACK:            "ACK",
    MSG_HANDSHAKE_REQ:  "HANDSHAKE_REQ",
    MSG_HANDSHAKE_ACK:  "HANDSHAKE_ACK",
    MSG_SENSOR_DATA:    "SENSOR_DATA",
    MSG_GPIO_STATUS:    "GPIO_STATUS",
    MSG_BATTERY_STATUS: "BATTERY_STATUS",
    MSG_SYSTEM_STATUS:  "SYSTEM_STATUS",
    MSG_DO_COMMAND:     "DO_COMMAND",
    MSG_PWM_COMMAND:    "PWM_COMMAND",
    MSG_POWER_COMMAND:  "POWER_COMMAND",
    MSG_TASK_COMMAND:   "TASK_COMMAND",
    MSG_TASK_RESPONSE:  "TASK_RESPONSE",
    MSG_EVENT:          "EVENT",
}

# ---------------------------------------------------------------------------
# CRC-16/CCITT (polynomial 0x1021, init 0xFFFF)
# ---------------------------------------------------------------------------
def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def build_frame(msg_id: int, seq: int, payload: bytes) -> bytes:
    """Build a complete protocol frame."""
    plen = len(payload)
    header = bytes([
        PROTO_HEADER_0, PROTO_HEADER_1,
        msg_id,
        seq & 0xFF, (seq >> 8) & 0xFF,
        plen & 0xFF, (plen >> 8) & 0xFF,
        0x00, 0x00,  # reserved
    ]) + payload
    crc = crc16_ccitt(header)
    return header + struct.pack("<H", crc)


def parse_frame(data: bytes):
    """Parse and validate a frame. Returns (msg_id, seq, payload) or None."""
    if len(data) < PROTO_HEADER_TOT + PROTO_CRC_SZ:
        return None
    if data[0] != PROTO_HEADER_0 or data[1] != PROTO_HEADER_1:
        return None

    msg_id = data[2]
    seq    = struct.unpack_from("<H", data, 3)[0]
    plen   = struct.unpack_from("<H", data, 5)[0]

    expected_len = PROTO_HEADER_TOT + plen + PROTO_CRC_SZ
    if len(data) < expected_len:
        return None

    payload = data[PROTO_HEADER_TOT:PROTO_HEADER_TOT + plen]
    crc_off = PROTO_HEADER_TOT + plen
    stored_crc = struct.unpack_from("<H", data, crc_off)[0]
    calc_crc   = crc16_ccitt(data[:crc_off])

    if stored_crc != calc_crc:
        logging.warning("CRC mismatch: stored=0x%04X calc=0x%04X", stored_crc, calc_crc)
        return None

    return msg_id, seq, payload, expected_len


# ---------------------------------------------------------------------------
# Client handler
# ---------------------------------------------------------------------------
def handle_client(conn: socket.socket, addr, loss_rate: float, delay_ms: float):
    logging.info("Client connected from %s:%d", addr[0], addr[1])
    buf = b""
    seq_counter = 0
    stats = {"rx": 0, "tx": 0, "dropped": 0, "crc_err": 0}

    try:
        while True:
            chunk = conn.recv(4096)
            if not chunk:
                break
            buf += chunk

            while True:
                # Find sync bytes
                idx = buf.find(bytes([PROTO_HEADER_0, PROTO_HEADER_1]))
                if idx < 0:
                    buf = b""
                    break
                if idx > 0:
                    buf = buf[idx:]

                if len(buf) < PROTO_HEADER_TOT + PROTO_CRC_SZ:
                    break

                result = parse_frame(buf)
                if result is None:
                    # Skip past the bad sync bytes
                    buf = buf[1:]
                    stats["crc_err"] += 1
                    continue

                msg_id, seq, payload, frame_len = result
                buf = buf[frame_len:]
                stats["rx"] += 1

                name = MSG_NAMES.get(msg_id, f"0x{msg_id:02X}")
                logging.debug("RX seq=%d  msg=%s  plen=%d", seq, name, len(payload))

                # Packet loss injection
                if loss_rate > 0.0 and random.random() < loss_rate:
                    logging.debug("  → dropped (simulated loss)")
                    stats["dropped"] += 1
                    continue

                # Delay injection
                if delay_ms > 0.0:
                    time.sleep(delay_ms / 1000.0)

                # Generate response
                response = None
                seq_counter += 1

                if msg_id == MSG_HANDSHAKE_REQ:
                    if len(payload) >= 10:
                        client_ver, client_feat, _ = struct.unpack_from("<HIH", payload)
                    else:
                        client_ver, client_feat = PROTOCOL_VERSION, FEAT_ALL
                    negotiated = client_feat & FEAT_ALL
                    ack_payload = struct.pack("<HIB",
                                             PROTOCOL_VERSION,
                                             negotiated,
                                             1)  # role=1 (Jetson)
                    response = build_frame(MSG_HANDSHAKE_ACK, seq_counter, ack_payload)
                    logging.info("  → HANDSHAKE_ACK ver=0x%04X feat=0x%08X",
                                 PROTOCOL_VERSION, negotiated)

                elif msg_id == MSG_HEARTBEAT:
                    ts_ms = int(time.monotonic() * 1000) & 0xFFFFFFFF
                    hb_payload = struct.pack("<IB", ts_ms, 1)  # role=1 Jetson
                    response = build_frame(MSG_HEARTBEAT, seq_counter, hb_payload)

                elif msg_id in (MSG_DO_COMMAND, MSG_PWM_COMMAND,
                                MSG_POWER_COMMAND, MSG_TASK_COMMAND):
                    ack_payload = struct.pack("<BHB", msg_id, seq, 0)
                    response = build_frame(MSG_ACK, seq_counter, ack_payload)

                if response:
                    conn.sendall(response)
                    stats["tx"] += 1

    except (ConnectionResetError, BrokenPipeError):
        pass
    finally:
        logging.info("Client %s:%d disconnected  rx=%d tx=%d dropped=%d crc_err=%d",
                     addr[0], addr[1],
                     stats["rx"], stats["tx"], stats["dropped"], stats["crc_err"])
        conn.close()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="Mock Jetson server for T113i testing")
    parser.add_argument("--host",     default="127.0.0.1")
    parser.add_argument("--port",     type=int, default=9000)
    parser.add_argument("--loss",     type=float, default=0.0,
                        help="Packet loss rate [0.0 .. 1.0]")
    parser.add_argument("--delay_ms", type=float, default=0.0,
                        help="Artificial response delay in milliseconds")
    parser.add_argument("--verbose",  action="store_true")
    args = parser.parse_args()

    level = logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(level=level,
                        format="%(asctime)s [%(levelname)s] %(message)s",
                        datefmt="%H:%M:%S")

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((args.host, args.port))
        srv.listen(5)
        logging.info("Mock Jetson server listening on %s:%d  loss=%.0f%%  delay=%.0f ms",
                     args.host, args.port, args.loss * 100, args.delay_ms)
        try:
            while True:
                conn, addr = srv.accept()
                t = threading.Thread(target=handle_client,
                                     args=(conn, addr, args.loss, args.delay_ms),
                                     daemon=True)
                t.start()
        except KeyboardInterrupt:
            logging.info("Server shutting down")


if __name__ == "__main__":
    main()
