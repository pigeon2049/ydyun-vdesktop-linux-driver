#!/usr/bin/env python3
"""Capture only metadata from the vendor viewer's local USB IPC socket.

This is a laboratory helper, not a production service.  It binds loopback
only, never enumerates USB, never forwards bytes, and never writes payloads to
disk.  The output contains connection counts, byte counts, SHA-256 digests,
and a short protocol prefix so the private local control boundary can be
identified without retaining credentials or session material.
"""

from __future__ import annotations

import argparse
import hashlib
import socket
import struct
import time


def decode_message_header(prefix: bytes) -> tuple[int, int, int, int] | None:
    """Decode the observed 16-byte little-endian local IPC header."""
    if len(prefix) < 16:
        return None
    return struct.unpack_from("<IIII", prefix, 0)


def inspect_connection(conn: socket.socket, peer: object, deadline: float) -> None:
    conn.settimeout(0.25)
    digest = hashlib.sha256()
    prefix = bytearray()
    total = 0
    while time.monotonic() < deadline:
        try:
            chunk = conn.recv(64 * 1024)
        except socket.timeout:
            continue
        if not chunk:
            break
        total += len(chunk)
        digest.update(chunk)
        if len(prefix) < 16:
            prefix.extend(chunk[: 16 - len(prefix)])
    print(
        "connection peer=%s bytes=%d sha256=%s prefix16=%s header=%s"
        % (peer, total, digest.hexdigest(), prefix.hex(), decode_message_header(prefix)),
        flush=True,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=3246)
    parser.add_argument("--duration", type=float, default=30.0)
    args = parser.parse_args()
    if not 1024 <= args.port <= 65535:
        raise SystemExit("port must be in 1024..65535")
    if not 0 < args.duration <= 300:
        raise SystemExit("duration must be in 0..300 seconds")

    deadline = time.monotonic() + args.duration
    accepted = 0
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("127.0.0.1", args.port))
        listener.listen(16)
        listener.settimeout(0.25)
        print("listening loopback port=%d" % args.port, flush=True)
        while time.monotonic() < deadline:
            try:
                conn, peer = listener.accept()
            except socket.timeout:
                continue
            accepted += 1
            with conn:
                inspect_connection(conn, peer, min(deadline, time.monotonic() + 2.0))
        print("accepted=%d" % accepted, flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
