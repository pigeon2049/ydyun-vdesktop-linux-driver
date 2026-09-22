#!/usr/bin/env python3
"""Test-only cloud-side proxy for the proven backward USB control boundary.

It sends the statically verified 0x0202/command=1 control frame to the
backward bridge, then relays the resulting USB/IP byte stream to a real local
usbipd.  It never handles credentials, vendor authentication, or physical USB.
"""

from __future__ import annotations

import argparse
import selectors
import socket
import struct
import sys
import time


FRAME_SIZE = 0x210
BUSID_OFFSET = 0x190
USBIP_HEADER_SIZE = 8


def connect(host: str, port: int, timeout: float = 10.0) -> socket.socket:
    deadline = time.monotonic() + timeout
    last_error: OSError | None = None
    while time.monotonic() < deadline:
        try:
            sock = socket.create_connection((host, port), 0.25)
            sock.setblocking(False)
            return sock
        except OSError as exc:
            last_error = exc
            time.sleep(0.02)
    raise OSError(f"could not connect to {host}:{port}: {last_error}")


def send_control(sock: socket.socket, busid: str) -> None:
    frame = bytearray(FRAME_SIZE)
    frame[:2] = b"\x02\x02"
    struct.pack_into("<I", frame, 8, 1)
    encoded = busid.encode("ascii")
    if not encoded or len(encoded) > 31:
        raise ValueError("test busid must be 1..31 ASCII bytes")
    frame[BUSID_OFFSET:BUSID_OFFSET + len(encoded)] = encoded
    sock.sendall(frame)


def relay(left: socket.socket, right: socket.socket) -> None:
    selector = selectors.DefaultSelector()
    selector.register(left, selectors.EVENT_READ, right)
    selector.register(right, selectors.EVENT_READ, left)
    try:
        while True:
            events = selector.select(10.0)
            if not events:
                raise TimeoutError("backward test relay timed out")
            for key, _ in events:
                data = key.fileobj.recv(64 * 1024)
                if not data:
                    return
                key.data.sendall(data)
    finally:
        selector.close()
        for sock in (left, right):
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            sock.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("control_port", type=int)
    parser.add_argument("usbip_port", type=int)
    parser.add_argument("busid")
    args = parser.parse_args()
    control = connect("127.0.0.1", args.control_port)
    upstream = connect("127.0.0.1", args.usbip_port)
    send_control(control, args.busid)
    relay(control, upstream)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, TimeoutError, ValueError, UnicodeError) as exc:
        print(f"backward_cloud_proxy: {exc}", file=sys.stderr)
        raise SystemExit(1)
