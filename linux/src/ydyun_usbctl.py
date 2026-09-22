#!/usr/bin/env python3
"""Small, least-privilege USB/IP controller for the YDYUN Linux guest.

The Windows payload contains a USB/IP-compatible data plane, but its control
plane is partly vendor-specific.  This first Linux implementation deliberately
uses the mainline usbip/vhci-hcd interface.  It provides a stable control point
for a later ZTE control-plane adapter without carrying Windows monitoring or
security agents into Linux.
"""

from __future__ import annotations

import argparse
import configparser
import ipaddress
import logging
import os
import platform
import queue
import select
import signal
import socket
import struct
import subprocess
import sys
import time
import shutil
import threading
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


LOG = logging.getLogger("ydyun-usbctl")
DEFAULT_CONFIG = "/etc/ydyun-usb/ydyun-usb.conf"
MODULES = ("usbip-core", "vhci-hcd")
EXPORT_MODULES = ("usbip-core", "usbip-host")
USB_SYSFS_DEVICES = Path("/sys/bus/usb/devices")
USB_SYSFS_DRIVERS = Path("/sys/bus/usb/drivers")
USB_SYSFS_DRIVERS_PROBE = Path("/sys/bus/usb/drivers_probe")
USBIP_VERSION = 0x0111
OP_REQ_LINK_INIT = 0x8008
OP_REP_LINK_INIT = 0x0008
OP_REQ_DEVLIST = 0x8005
OP_REP_DEVLIST = 0x0005
OP_REQ_IMPORT = 0x8003
USBIP_IMPORT_BUSID_SIZE = 32
BACKWARD_FRAME_SIZE = 0x210
BACKWARD_ALLOWED_OPS = frozenset({1, 2})
BACKWARD_BUSID_OFFSET = 0x190
BACKWARD_BUSID_END = 0x20F
BACKWARD_COMPRESSION_MODES = frozenset({"off", "lz4"})
BACKWARD_CODEC_HEADER_SIZE = 9
BACKWARD_CODEC_MAX_SIZE = 64 * 1024 * 1024
VENDOR_PROXY_HEADER_SIZE = 0x74
VENDOR_PROXY_HOST_OFFSET = 0x12
VENDOR_PROXY_HOST_LIMIT = 0x21
VENDOR_PROXY_PORT_OFFSET = 0x33
VENDOR_PROXY_PORT_LIMIT = 0x11


class ControllerError(RuntimeError):
    """A user-facing controller failure."""


@dataclass(frozen=True)
class DeviceSpec:
    busid: str
    name: str = ""


@dataclass(frozen=True)
class LocalDeviceSpec:
    busid: str
    name: str = ""


@dataclass(frozen=True)
class Settings:
    remote_host: str
    remote_port: int
    devices: tuple[DeviceSpec, ...]
    reconnect_seconds: float
    detach_on_exit: bool
    command_timeout: float
    local_devices: tuple[LocalDeviceSpec, ...] = ()


@dataclass(frozen=True)
class BackwardSettings:
    listen_host: str
    listen_port: int
    proxy_host: str
    proxy_port: int
    frame_timeout: float
    max_pending: int
    allowed_ops: frozenset[int]
    auto_attach: bool
    attach_timeout: float
    compression: str = "off"


@dataclass(frozen=True)
class PendingLink:
    connection: socket.socket
    command: int
    busid: str


def _parse_bool(value: str, key: str) -> bool:
    value = value.strip().lower()
    if value in {"1", "yes", "true", "on"}:
        return True
    if value in {"0", "no", "false", "off"}:
        return False
    raise ControllerError(f"invalid boolean for {key}: {value!r}")


def _parse_port(value: str, key: str) -> int:
    try:
        port = int(value)
    except ValueError as exc:
        raise ControllerError(f"invalid port for {key}: {value!r}") from exc
    if not 1 <= port <= 65535:
        raise ControllerError(f"port out of range for {key}: {port}")
    return port


def _parse_positive_int(value: str, key: str, maximum: int) -> int:
    try:
        number = int(value)
    except ValueError as exc:
        raise ControllerError(f"invalid integer for {key}: {value!r}") from exc
    if not 1 <= number <= maximum:
        raise ControllerError(f"{key} must be between 1 and {maximum}")
    return number


def _validate_host(host: str) -> str:
    host = host.strip()
    if not host or any(ch.isspace() for ch in host):
        raise ControllerError("remote_host must be a single hostname or IP address")
    # Accept DNS names and IP literals.  Reject URI-like or shell-like values.
    if any(ch in host for ch in "/\\:;|&`$\"'"):
        try:
            ipaddress.ip_address(host)
        except ValueError as exc:
            raise ControllerError(f"invalid remote_host: {host!r}") from exc
    return host


def _validate_busid(busid: str) -> str:
    busid = busid.strip()
    # Linux USB/IP bus IDs are normally 1-2, 2-1.3 or a usbip-vudc name.
    # Keep this conservative: no whitespace or shell/control characters.
    if (
        not busid
        or "/" in busid
        or "\\" in busid
        or any(ch.isspace() or ch in ";|&`$\"'" for ch in busid)
    ):
        raise ControllerError(f"invalid busid: {busid!r}")
    if len(busid) > 128:
        raise ControllerError("busid is too long")
    return busid


def build_vendor_proxy_header(
    destination_host: str, destination_port: int, link_port: int = 3246
) -> bytes:
    """Build only the observed 0x74-byte vendor proxy preamble.

    Live official-client tracing confirmed this fixed preamble immediately
    before the standard USB/IP stream. The destination values are supplied
    by the authenticated session; this helper does not discover a session,
    create credentials, or implement the following private import message.
    It is intentionally a narrow serialization primitive for future
    session-adapter work and is not used by the default USB/IP runtime.
    """
    host = _validate_host(destination_host)
    port = _parse_port(str(destination_port), "destination_port")
    link = _parse_port(str(link_port), "link_port")
    try:
        host_bytes = host.encode("ascii")
    except UnicodeEncodeError as exc:
        raise ControllerError("vendor proxy destination_host must be ASCII") from exc
    port_bytes = str(port).encode("ascii")
    if not 1 <= len(host_bytes) <= VENDOR_PROXY_HOST_LIMIT:
        raise ControllerError("vendor proxy destination_host is too long")
    if len(port_bytes) > VENDOR_PROXY_PORT_LIMIT:
        raise ControllerError("vendor proxy destination_port is too long")
    header = bytearray(VENDOR_PROXY_HEADER_SIZE)
    struct.pack_into("<I", header, 0x00, 1)
    struct.pack_into("<I", header, 0x08, link)
    struct.pack_into("<I", header, 0x0C, 2)
    header[0x10:0x12] = b"u\x02"
    header[VENDOR_PROXY_HOST_OFFSET:VENDOR_PROXY_HOST_OFFSET + len(host_bytes)] = host_bytes
    header[VENDOR_PROXY_PORT_OFFSET:VENDOR_PROXY_PORT_OFFSET + len(port_bytes)] = port_bytes
    return bytes(header)


def load_settings(path: str) -> Settings:
    parser = configparser.ConfigParser(interpolation=None)
    if not parser.read(path):
        raise ControllerError(f"configuration file not found: {path}")
    if not parser.has_section("connection"):
        raise ControllerError("configuration is missing [connection]")

    section = parser["connection"]
    host = _validate_host(section.get("remote_host", ""))
    port = _parse_port(section.get("remote_port", "3240"), "remote_port")
    try:
        reconnect = float(section.get("reconnect_seconds", "5"))
    except ValueError as exc:
        raise ControllerError("reconnect_seconds must be a number") from exc
    if not 0.5 <= reconnect <= 3600:
        raise ControllerError("reconnect_seconds must be between 0.5 and 3600")
    try:
        timeout = float(section.get("command_timeout", "30"))
    except ValueError as exc:
        raise ControllerError("command_timeout must be a number") from exc
    if not 1 <= timeout <= 300:
        raise ControllerError("command_timeout must be between 1 and 300")

    devices: list[DeviceSpec] = []
    if parser.has_section("devices"):
        for key, value in parser.items("devices"):
            busid = _validate_busid(key)
            devices.append(DeviceSpec(busid=busid, name=value.strip()))
    local_devices: list[LocalDeviceSpec] = []
    if parser.has_section("local_devices"):
        for key, value in parser.items("local_devices"):
            local_devices.append(LocalDeviceSpec(busid=_validate_busid(key), name=value.strip()))
    detach = _parse_bool(section.get("detach_on_exit", "yes"), "detach_on_exit")
    return Settings(host, port, tuple(devices), reconnect, detach, timeout, tuple(local_devices))


def _validate_bind_host(host: str, key: str) -> str:
    host = host.strip()
    try:
        ipaddress.ip_address(host)
    except ValueError as exc:
        raise ControllerError(f"{key} must be an IPv4 or IPv6 literal") from exc
    return host


def load_backward_settings(path: str) -> BackwardSettings:
    parser = configparser.ConfigParser(interpolation=None)
    if not parser.read(path):
        raise ControllerError(f"configuration file not found: {path}")
    if not parser.has_section("backward"):
        raise ControllerError("configuration is missing [backward]")
    section = parser["backward"]
    if not _parse_bool(section.get("enabled", "no"), "backward.enabled"):
        raise ControllerError("backward bridge is disabled; set backward.enabled=yes explicitly")
    listen_host = _validate_bind_host(
        section.get("listen_host", "0.0.0.0"), "backward.listen_host"
    )
    proxy_host = _validate_bind_host(
        section.get("proxy_host", "127.0.0.1"), "backward.proxy_host"
    )
    listen_port = _parse_port(section.get("listen_port", "3246"), "backward.listen_port")
    proxy_port = _parse_port(section.get("proxy_port", "3247"), "backward.proxy_port")
    try:
        frame_timeout = float(section.get("frame_timeout", "10"))
    except ValueError as exc:
        raise ControllerError("backward.frame_timeout must be a number") from exc
    if not 1 <= frame_timeout <= 300:
        raise ControllerError("backward.frame_timeout must be between 1 and 300")
    max_pending = _parse_positive_int(
        section.get("max_pending", "16"), "backward.max_pending", 256
    )
    raw_ops = section.get("allowed_ops", "1,2")
    try:
        allowed_ops = frozenset(int(item.strip(), 10) for item in raw_ops.split(",") if item.strip())
    except ValueError as exc:
        raise ControllerError("backward.allowed_ops must be comma-separated integers") from exc
    if not allowed_ops or not allowed_ops.issubset(BACKWARD_ALLOWED_OPS):
        raise ControllerError("backward.allowed_ops may contain only USB operations 1 and 2")
    auto_attach = _parse_bool(section.get("auto_attach", "no"), "backward.auto_attach")
    try:
        attach_timeout = float(section.get("attach_timeout", "30"))
    except ValueError as exc:
        raise ControllerError("backward.attach_timeout must be a number") from exc
    if not 1 <= attach_timeout <= 300:
        raise ControllerError("backward.attach_timeout must be between 1 and 300")
    compression = section.get("compression", "off").strip().lower()
    if compression not in BACKWARD_COMPRESSION_MODES:
        raise ControllerError(
            "backward.compression must be one of: off, lz4"
        )
    if listen_host == proxy_host and listen_port == proxy_port:
        raise ControllerError("backward listen and proxy endpoints must differ")
    return BackwardSettings(
        listen_host,
        listen_port,
        proxy_host,
        proxy_port,
        frame_timeout,
        max_pending,
        allowed_ops,
        auto_attach,
        attach_timeout,
        compression,
    )


def find_usbip() -> str:
    candidates = (shutil.which("usbip"), "/usr/sbin/usbip", "/usr/bin/usbip")
    for candidate in candidates:
        if candidate and os.access(candidate, os.X_OK):
            return candidate
    raise ControllerError(
        "usbip executable not found; install the Debian linux-tools/usbip package"
    )


def run_checked(command: list[str], timeout: float) -> str:
    LOG.debug("running: %s", " ".join(command))
    try:
        result = subprocess.run(
            command,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as exc:
        raise ControllerError(f"command timed out: {command[0]}") from exc
    output = result.stdout or ""
    if result.returncode:
        raise ControllerError(output.strip() or f"command failed with {result.returncode}")
    return output


def ensure_modules() -> None:
    if os.geteuid() != 0:
        raise ControllerError("loading vhci-hcd requires root or CAP_SYS_MODULE")
    for module in MODULES:
        run_checked(["/sbin/modprobe", module], 30)


def ensure_export_modules() -> None:
    """Load only the kernel pieces needed to export explicitly selected USB devices."""
    if os.geteuid() != 0:
        raise ControllerError("exporting USB devices requires root or CAP_SYS_MODULE")
    for module in EXPORT_MODULES:
        run_checked(["/sbin/modprobe", module], 30)


def check_remote(host: str, port: int) -> None:
    try:
        addresses = socket.getaddrinfo(host, port, type=socket.SOCK_STREAM)
    except OSError as exc:
        raise ControllerError(f"cannot resolve {host}:{port}: {exc}") from exc
    last_error: OSError | None = None
    for family, socktype, proto, _, sockaddr in addresses:
        sock = socket.socket(family, socktype, proto)
        sock.settimeout(3)
        try:
            sock.connect(sockaddr)
            return
        except OSError as exc:
            last_error = exc
        finally:
            sock.close()
    raise ControllerError(f"cannot connect to {host}:{port}: {last_error}")


def _recv_exact(sock: socket.socket, size: int) -> bytes:
    chunks: list[bytes] = []
    remaining = size
    while remaining:
        chunk = sock.recv(remaining)
        if not chunk:
            raise ControllerError("remote closed the USB/IP probe connection")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def _lz4_length(data: bytes, index: int, length: int) -> tuple[int, int]:
    """Read an LZ4 block extended length, with bounded parsing."""
    while index < len(data) and data[index] == 255:
        length += 255
        index += 1
    if index >= len(data):
        raise ControllerError("truncated LZ4 length")
    return length + data[index], index + 1


def _lz4_decompress_block(data: bytes, expected_size: int) -> bytes:
    """Decode a raw LZ4 block without relying on a native library."""
    if not 1 <= expected_size <= BACKWARD_CODEC_MAX_SIZE:
        raise ControllerError("LZ4 uncompressed size is out of bounds")
    output = bytearray()
    index = 0
    while index < len(data):
        token = data[index]
        index += 1
        literal_length = token >> 4
        if literal_length == 15:
            literal_length, index = _lz4_length(data, index, literal_length)
        if index + literal_length > len(data):
            raise ControllerError("LZ4 literal section exceeds frame")
        output.extend(data[index:index + literal_length])
        index += literal_length
        if len(output) > expected_size:
            raise ControllerError("LZ4 output exceeds advertised size")
        if index == len(data):
            break
        if index + 2 > len(data):
            raise ControllerError("truncated LZ4 match offset")
        offset = data[index] | (data[index + 1] << 8)
        index += 2
        if offset == 0 or offset > len(output):
            raise ControllerError("invalid LZ4 match offset")
        match_length = (token & 0x0F) + 4
        if (token & 0x0F) == 15:
            match_length, index = _lz4_length(data, index, match_length)
        if len(output) + match_length > expected_size:
            raise ControllerError("LZ4 match exceeds advertised size")
        for _ in range(match_length):
            output.append(output[-offset])
    if len(output) != expected_size:
        raise ControllerError(
            f"LZ4 output size mismatch: expected {expected_size}, got {len(output)}"
        )
    return bytes(output)


def decode_backward_compressed_frame(frame: bytes) -> bytes:
    """Decode the official library's 9-byte type/length/payload wrapper."""
    if len(frame) < BACKWARD_CODEC_HEADER_SIZE:
        raise ControllerError("compressed USB frame header is truncated")
    frame_type, uncompressed_size, compressed_size = struct.unpack(
        "!BII", frame[:BACKWARD_CODEC_HEADER_SIZE]
    )
    if not 1 <= uncompressed_size <= BACKWARD_CODEC_MAX_SIZE:
        raise ControllerError("compressed USB uncompressed size is out of bounds")
    if not 1 <= compressed_size <= BACKWARD_CODEC_MAX_SIZE:
        raise ControllerError("compressed USB payload size is out of bounds")
    payload = frame[BACKWARD_CODEC_HEADER_SIZE:]
    if len(payload) != compressed_size:
        raise ControllerError("compressed USB payload length does not match header")
    if frame_type == 0:
        if compressed_size != uncompressed_size:
            raise ControllerError("raw compressed USB frame has mismatched sizes")
        return payload
    if frame_type == 1:
        return _lz4_decompress_block(payload, uncompressed_size)
    raise ControllerError(f"unsupported compressed USB frame type: {frame_type}")


def encode_backward_compressed_frame(payload: bytes) -> bytes:
    """Encode an uncompressed payload as the peer-compatible type-0 frame."""
    if not 1 <= len(payload) <= BACKWARD_CODEC_MAX_SIZE:
        raise ControllerError("USB payload size is out of bounds")
    return struct.pack("!BII", 0, len(payload), len(payload)) + payload


def _recv_backward_compressed_frame(sock: socket.socket) -> bytes:
    header = _recv_exact(sock, BACKWARD_CODEC_HEADER_SIZE)
    _, _, compressed_size = struct.unpack("!BII", header)
    if not 1 <= compressed_size <= BACKWARD_CODEC_MAX_SIZE:
        raise ControllerError("compressed USB payload size is out of bounds")
    return decode_backward_compressed_frame(
        header + _recv_exact(sock, compressed_size)
    )


def parse_backward_frame(frame: bytes, allowed_ops: frozenset[int] = BACKWARD_ALLOWED_OPS) -> int:
    """Validate the proven fixed prefix of the ZTE backward-link message.

    The Windows listener checks a two-byte 0x0202 version marker and reads the
    command from offset +8.  The remainder contains vendor-specific extension
    fields, so this function deliberately does not interpret or execute them.
    """
    if len(frame) != BACKWARD_FRAME_SIZE:
        raise ControllerError(
            f"backward-link frame must be {BACKWARD_FRAME_SIZE} bytes, got {len(frame)}"
        )
    if frame[:2] != b"\x02\x02":
        raise ControllerError("backward-link version marker is not 0x0202")
    command = struct.unpack_from("<I", frame, 8)[0]
    if command not in allowed_ops:
        raise ControllerError(f"backward-link command {command} is not an allowed USB operation")
    return command


def backward_frame_busid(frame: bytes) -> str:
    """Extract the bus ID field used by the proven op 1/2 handler.

    Static analysis shows that the Windows handler NUL-terminates byte
    ``0x20f`` and logs the string beginning at ``0x190`` as ``busid``.  Keep
    this helper observational: it does not turn the value into a command or
    silently attach a device.
    """
    parse_backward_frame(frame)
    raw = frame[BACKWARD_BUSID_OFFSET:BACKWARD_BUSID_END]
    value = raw.split(b"\x00", 1)[0]
    if any(byte < 0x21 or byte > 0x7E for byte in value):
        raise ControllerError("backward-link busid contains non-printable bytes")
    try:
        busid = value.decode("ascii")
    except UnicodeDecodeError as exc:
        raise ControllerError("backward-link busid is not ASCII") from exc
    if not busid:
        raise ControllerError("backward-link busid is empty")
    return _validate_busid(busid)


def _close_socket(sock: socket.socket) -> None:
    try:
        sock.shutdown(socket.SHUT_RDWR)
    except OSError:
        pass
    try:
        sock.close()
    except OSError:
        pass


def _relay_sockets(
    left: socket.socket,
    right: socket.socket,
    initial_left: bytes = b"",
    compression: str = "off",
) -> None:
    """Forward standard USB/IP bytes, optionally translating the vendor wrapper."""
    if compression not in BACKWARD_COMPRESSION_MODES:
        raise ControllerError(f"unsupported backward compression mode: {compression}")
    if compression == "off":
        sockets = (left, right)
        try:
            if initial_left:
                right.sendall(initial_left)
            while True:
                readable, _, _ = select.select(list(sockets), [], [], 1.0)
                if not readable:
                    continue
                for source in readable:
                    target = right if source is left else left
                    data = source.recv(64 * 1024)
                    if not data:
                        return
                    target.sendall(data)
        except (OSError, ValueError) as exc:
            LOG.debug("backward-link relay ended: %s", exc)
        finally:
            _close_socket(left)
            _close_socket(right)
        return

    stop = threading.Event()

    def plain_to_wrapped() -> None:
        try:
            if initial_left:
                right.sendall(encode_backward_compressed_frame(initial_left))
            while not stop.is_set():
                data = left.recv(64 * 1024)
                if not data:
                    return
                right.sendall(encode_backward_compressed_frame(data))
        except (ControllerError, OSError, ValueError) as exc:
            LOG.debug("backward-link compressed upstream ended: %s", exc)
        finally:
            stop.set()

    def wrapped_to_plain() -> None:
        try:
            while not stop.is_set():
                data = _recv_backward_compressed_frame(right)
                left.sendall(data)
        except (ControllerError, OSError, ValueError) as exc:
            LOG.debug("backward-link compressed downstream ended: %s", exc)
        finally:
            stop.set()

    upstream = threading.Thread(target=plain_to_wrapped, name="ydyun-usbip-encode")
    downstream = threading.Thread(target=wrapped_to_plain, name="ydyun-usbip-decode")
    upstream.start()
    downstream.start()
    sockets = (left, right)
    try:
        while not stop.wait(0.1):
            pass
    finally:
        stop.set()
        _close_socket(left)
        _close_socket(right)
        upstream.join(timeout=1)
        downstream.join(timeout=1)


class BackwardBridge:
    """Bridge validated 3246 control connections to local standard USB/IP."""

    def __init__(self, settings: BackwardSettings):
        self.settings = settings
        self.pending: queue.Queue[PendingLink] = queue.Queue(settings.max_pending)

    def _make_listener(self, host: str, port: int) -> socket.socket:
        family = socket.AF_INET6 if ":" in host else socket.AF_INET
        listener = socket.socket(family, socket.SOCK_STREAM)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind((host, port))
        listener.listen(self.settings.max_pending)
        listener.setblocking(False)
        return listener

    def _remove_pending(self, connection: socket.socket) -> None:
        """Remove a failed auto-attach socket without disturbing other links."""
        with self.pending.mutex:
            for item in list(self.pending.queue):
                if item.connection is connection:
                    self.pending.queue.remove(item)
                    self.pending.not_full.notify()
                    return

    def _take_pending(self, busid: str | None) -> PendingLink | None:
        with self.pending.mutex:
            selected: PendingLink | None = None
            if busid:
                for item in self.pending.queue:
                    if item.busid == busid:
                        selected = item
                        break
            if selected is None and not busid and self.pending.queue:
                selected = self.pending.queue[0]
            if selected is None:
                return None
            self.pending.queue.remove(selected)
            self.pending.not_full.notify()
            return selected

    def _take_pending_wait(
        self, busid: str | None, timeout: float
    ) -> PendingLink | None:
        """Wait briefly for the remote control-frame worker to enqueue a link."""
        deadline = time.monotonic() + max(0.0, timeout)
        while True:
            selected = self._take_pending(busid)
            if selected is not None:
                return selected
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            time.sleep(min(0.01, remaining))

    def _auto_attach(self, connection: socket.socket, busid: str) -> None:
        try:
            output = attach_to_proxy(
                self.settings.proxy_host,
                self.settings.proxy_port,
                busid,
                self.settings.attach_timeout,
            )
            LOG.info("auto-attached backward-link busid=%s: %s", busid, output.strip())
        except ControllerError as exc:
            LOG.warning("auto-attach busid=%s failed: %s", busid, exc)
            self._remove_pending(connection)
            _close_socket(connection)

    def _auto_detach(self, busid: str) -> None:
        settings = Settings(
            self.settings.proxy_host,
            self.settings.proxy_port,
            (),
            5,
            False,
            self.settings.attach_timeout,
        )
        output = list_ports(settings)
        ports = port_indexes_for_busids(output, [busid])
        if not ports:
            LOG.info("backward-link detach busid=%s: no local VHCI port found", busid)
            return
        for port in ports:
            LOG.info("auto-detaching backward-link busid=%s from VHCI port %s", busid, port)
            LOG.info(detach(settings, port).strip())

    def _prepare_remote(self, connection: socket.socket, address: object) -> None:
        try:
            connection.settimeout(self.settings.frame_timeout)
            frame = _recv_exact(connection, BACKWARD_FRAME_SIZE)
            command = parse_backward_frame(frame, self.settings.allowed_ops)
            busid = ""
            try:
                busid = backward_frame_busid(frame)
            except ControllerError as exc:
                LOG.info("backward-link USB operation %d has no usable busid: %s", command, exc)
            connection.settimeout(None)
            if command == 2:
                if not busid:
                    raise ControllerError("backward-link detach has no usable busid")
                if self.settings.auto_attach:
                    self._auto_detach(busid)
                else:
                    LOG.info(
                        "validated backward-link detach busid=%s; auto_attach is disabled",
                        busid,
                    )
                _close_socket(connection)
                return
            self.pending.put_nowait(PendingLink(connection, command, busid))
            LOG.info(
                "validated backward-link USB operation %d busid=%s from %s",
                command,
                busid or "<unknown>",
                address,
            )
            if self.settings.auto_attach and command == 1 and busid:
                threading.Thread(
                    target=self._auto_attach,
                    args=(connection, busid),
                    name="ydyun-backward-auto-attach",
                    daemon=True,
                ).start()
        except (ControllerError, OSError, queue.Full) as exc:
            LOG.warning("rejecting backward-link connection from %s: %s", address, exc)
            _close_socket(connection)

    def _prepare_local(self, local: socket.socket, address: object) -> None:
        """Match a local USB/IP import request to the corresponding bus ID."""
        initial = b""
        requested_busid: str | None = None
        try:
            local.settimeout(self.settings.frame_timeout)
            header = _recv_exact(local, 8)
            initial += header
            version, operation, status = struct.unpack("!HHI", header)
            if operation == OP_REQ_IMPORT and version == USBIP_VERSION and status == 0:
                body = _recv_exact(local, USBIP_IMPORT_BUSID_SIZE)
                initial += body
                raw_busid = body.split(b"\x00", 1)[0]
                try:
                    requested_busid = _validate_busid(raw_busid.decode("ascii"))
                except (UnicodeDecodeError, ControllerError) as exc:
                    raise ControllerError("local USB/IP import has an invalid busid") from exc
            local.settimeout(None)
            link = self._take_pending_wait(
                requested_busid, self.settings.frame_timeout
            )
            if link is None:
                raise ControllerError(
                    f"no validated cloud link for local busid {requested_busid or '<unspecified>'}"
                )
            LOG.info(
                "bridging local USB/IP client %s to command %d busid=%s",
                address,
                link.command,
                link.busid or requested_busid or "<unknown>",
            )
            threading.Thread(
                target=_relay_sockets,
                args=(local, link.connection, initial, self.settings.compression),
                name="ydyun-usbip-relay",
                daemon=True,
            ).start()
        except (ControllerError, OSError, struct.error) as exc:
            LOG.warning("closing local USB/IP client %s: %s", address, exc)
            _close_socket(local)

    def serve(self, stop_event: threading.Event) -> None:
        remote_listener = self._make_listener(
            self.settings.listen_host, self.settings.listen_port
        )
        proxy_listener = self._make_listener(
            self.settings.proxy_host, self.settings.proxy_port
        )
        LOG.info(
            "backward bridge listening on %s:%d; local USB/IP proxy on %s:%d",
            self.settings.listen_host,
            self.settings.listen_port,
            self.settings.proxy_host,
            self.settings.proxy_port,
        )
        try:
            while not stop_event.is_set():
                readable, _, _ = select.select(
                    [remote_listener, proxy_listener], [], [], 0.5
                )
                if remote_listener in readable:
                    connection, address = remote_listener.accept()
                    threading.Thread(
                        target=self._prepare_remote,
                        args=(connection, address),
                        name="ydyun-backward-frame",
                        daemon=True,
                    ).start()
                if proxy_listener in readable:
                    local, address = proxy_listener.accept()
                    threading.Thread(
                        target=self._prepare_local,
                        args=(local, address),
                        name="ydyun-backward-local-client",
                        daemon=True,
                    ).start()
        finally:
            _close_socket(remote_listener)
            _close_socket(proxy_listener)
            while True:
                try:
                    _close_socket(self.pending.get_nowait().connection)
                except queue.Empty:
                    break


def backward_bridge(settings: BackwardSettings) -> int:
    stop_event = threading.Event()

    def request_stop(signum: int, _frame: object) -> None:
        LOG.info("received signal %s; stopping backward bridge", signum)
        stop_event.set()

    signal.signal(signal.SIGTERM, request_stop)
    signal.signal(signal.SIGINT, request_stop)
    BackwardBridge(settings).serve(stop_event)
    return 0


def probe_protocol(settings: Settings) -> str:
    """Probe whether a TCP endpoint speaks the standard USB/IP DEVLIST PDU."""
    try:
        addresses = socket.getaddrinfo(
            settings.remote_host, settings.remote_port, type=socket.SOCK_STREAM
        )
    except OSError as exc:
        raise ControllerError(f"cannot resolve {settings.remote_host}: {exc}") from exc
    last_error: OSError | None = None
    for family, socktype, proto, _, sockaddr in addresses:
        sock = socket.socket(family, socktype, proto)
        sock.settimeout(settings.command_timeout)
        try:
            sock.connect(sockaddr)
            # op_common(version, code, status) followed by the 4-byte
            # OP_REQ_DEVLIST request body.  No device data is accepted here.
            sock.sendall(struct.pack("!HHII", USBIP_VERSION, OP_REQ_DEVLIST, 0, 0))
            version, code, status = struct.unpack("!HHI", _recv_exact(sock, 8))
            if version != USBIP_VERSION:
                raise ControllerError(f"USB/IP version mismatch: 0x{version:04x}")
            if code != OP_REP_DEVLIST:
                raise ControllerError(f"unexpected USB/IP reply code: 0x{code:04x}")
            if status:
                raise ControllerError(f"USB/IP peer returned status {status}")
            device_count = struct.unpack("!I", _recv_exact(sock, 4))[0]
            return f"standard-usbip version=0x{version:04x} devices={device_count}"
        except (OSError, struct.error) as exc:
            last_error = exc
        finally:
            sock.close()
    raise ControllerError(f"USB/IP probe failed for {settings.remote_host}:{settings.remote_port}: {last_error}")


def probe_link_protocol(settings: Settings) -> str:
    """Probe the confirmed vendor USB/IP link-init followed by DEVLIST.

    The official Linux USB/IP library sends this common-header handshake on an
    already authenticated JWAE/SCG link.  After the link-init response, the
    peer drives the command loop by sending OP_REQ_DEVLIST; this side returns
    the standard OP_REP_DEVLIST header and device count.  This function
    intentionally does not create that link, negotiate TLS/KCP, or send
    credentials; it is a read-only compatibility probe for an endpoint
    supplied by that session.
    """
    try:
        addresses = socket.getaddrinfo(
            settings.remote_host, settings.remote_port, type=socket.SOCK_STREAM
        )
    except OSError as exc:
        raise ControllerError(f"cannot resolve {settings.remote_host}: {exc}") from exc
    last_error: OSError | ControllerError | None = None
    for family, socktype, proto, _, sockaddr in addresses:
        sock = socket.socket(family, socktype, proto)
        sock.settimeout(settings.command_timeout)
        try:
            sock.connect(sockaddr)
            # Unlike OP_REQ_DEVLIST, the vendor link-init has only the
            # 8-byte op_common header.
            sock.sendall(struct.pack("!HHI", USBIP_VERSION, OP_REQ_LINK_INIT, 0))
            version, code, status = struct.unpack("!HHI", _recv_exact(sock, 8))
            if version != USBIP_VERSION:
                raise ControllerError(f"link-init version mismatch: 0x{version:04x}")
            if code != OP_REP_LINK_INIT:
                raise ControllerError(f"unexpected link-init reply code: 0x{code:04x}")
            if status:
                raise ControllerError(f"link-init peer returned status {status}")
            version, code, status = struct.unpack("!HHI", _recv_exact(sock, 8))
            if version != USBIP_VERSION:
                raise ControllerError(f"DEVLIST version mismatch: 0x{version:04x}")
            if code != OP_REQ_DEVLIST:
                raise ControllerError(f"unexpected DEVLIST request code: 0x{code:04x}")
            if status:
                raise ControllerError(f"DEVLIST request returned status {status}")
            sock.sendall(struct.pack("!HHI", USBIP_VERSION, OP_REP_DEVLIST, 0))
            device_count = struct.unpack("!I", _recv_exact(sock, 4))[0]
            return f"official-usbip-link version=0x{version:04x} devices={device_count}"
        except (OSError, struct.error, ControllerError) as exc:
            last_error = exc
        finally:
            sock.close()
    raise ControllerError(
        f"official USB/IP link probe failed for {settings.remote_host}:{settings.remote_port}: {last_error}"
    )


def list_remote(settings: Settings) -> str:
    usbip = find_usbip()
    return run_checked(
        [usbip, "--tcp-port", str(settings.remote_port), "list", "--remote", settings.remote_host],
        settings.command_timeout,
    )


def list_ports(settings: Settings) -> str:
    return run_checked([find_usbip(), "port"], settings.command_timeout)


def list_local(settings: Settings) -> str:
    """List local USB devices without binding or changing their kernel drivers."""
    return run_checked([find_usbip(), "list", "--local"], settings.command_timeout)


def _module_available(module: str, timeout: float) -> bool:
    """Check module resolution without loading or changing kernel state."""
    result = subprocess.run(
        ["/sbin/modprobe", "-n", "-q", module],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=timeout,
    )
    return result.returncode == 0


def doctor(settings: Settings) -> int:
    """Run read-only deployment checks; never connects or changes USB state."""
    failures = 0

    def report(label: str, ok: bool, detail: str) -> None:
        nonlocal failures
        status = "PASS" if ok else "FAIL"
        if not ok:
            failures += 1
        print(f"{status} {label}: {detail}")

    report("architecture", platform.machine() in {"x86_64", "amd64"}, platform.machine())
    try:
        usbip = find_usbip()
        report("usbip", True, usbip)
    except ControllerError as exc:
        usbip = ""
        report("usbip", False, str(exc))

    for module in MODULES:
        try:
            available = _module_available(module, settings.command_timeout)
        except (OSError, subprocess.TimeoutExpired) as exc:
            available = False
            detail = str(exc)
        else:
            detail = "available for service startup" if available else "not resolvable"
        report(f"module {module}", available, detail)

    if settings.local_devices:
        try:
            available = _module_available("usbip-host", settings.command_timeout)
        except (OSError, subprocess.TimeoutExpired) as exc:
            available = False
            detail = str(exc)
        else:
            detail = "available for explicit local export" if available else "not resolvable"
        report("module usbip-host", available, detail)

    if usbip:
        try:
            output = run_checked([usbip, "port"], settings.command_timeout)
            imported = sum(1 for line in output.splitlines() if "Port " in line)
            report("vhci", True, f"read-only port query completed ({imported} port lines)")
        except ControllerError as exc:
            report("vhci", False, str(exc))
        try:
            output = run_checked([usbip, "list", "--local"], settings.command_timeout)
            devices = sum(1 for line in output.splitlines() if "busid" in line.lower())
            report("local-usb", True, f"read-only enumeration completed ({devices} busid lines)")
        except ControllerError as exc:
            report("local-usb", False, str(exc))
    else:
        report("vhci", False, "usbip check skipped")
        report("local-usb", False, "usbip check skipped")
    return 1 if failures else 0


def _write_sysfs(path: Path, value: str) -> None:
    try:
        path.write_text(value, encoding="ascii")
    except OSError as exc:
        raise ControllerError(f"cannot write sysfs {path}: {exc}") from exc


def _local_interface_bindings(busid: str) -> list[tuple[Path, Path]]:
    """Return (interface, driver-directory) pairs currently bound to a device."""
    busid = _validate_busid(busid)
    device = USB_SYSFS_DEVICES / busid
    if not device.is_dir():
        raise ControllerError(f"local USB device is not present: {busid}")
    bindings: list[tuple[Path, Path]] = []
    for interface in sorted(USB_SYSFS_DEVICES.glob(f"{busid}:*")):
        driver_link = interface / "driver"
        if not interface.is_dir() or not driver_link.is_symlink():
            continue
        try:
            driver = driver_link.resolve(strict=True)
        except OSError as exc:
            raise ControllerError(f"cannot resolve USB driver for {interface.name}: {exc}") from exc
        if driver.parent != USB_SYSFS_DRIVERS:
            raise ControllerError(f"unexpected USB driver path for {interface.name}: {driver}")
        bindings.append((interface, driver))
    return bindings


def _ensure_exportable_usb_device(busid: str) -> Path:
    """Reject USB hubs before any class driver or usbip-host change."""
    busid = _validate_busid(busid)
    device = USB_SYSFS_DEVICES / busid
    if not device.is_dir():
        raise ControllerError(f"local USB device is not present: {busid}")
    class_path = device / "bDeviceClass"
    try:
        device_class = class_path.read_text(encoding="ascii").strip().lower()
    except OSError as exc:
        raise ControllerError(f"cannot read USB device class for {busid}: {exc}") from exc
    if device_class in {"09", "0x09", "09\n"}:
        raise ControllerError(f"refusing to export USB hub/root hub: {busid}")
    return device


def _release_local_usb_drivers(busid: str) -> list[tuple[Path, Path]]:
    """Detach class drivers so usbip-host can claim the selected device only."""
    bindings = _local_interface_bindings(busid)
    released: list[tuple[Path, Path]] = []
    try:
        for interface, driver in bindings:
            if driver.name == "usbip-host":
                continue
            _write_sysfs(driver / "unbind", interface.name)
            released.append((interface, driver))
            LOG.info("unbound local USB interface %s from %s", interface.name, driver.name)
    except ControllerError:
        # A composite device must not be left half-unbound when one interface
        # disappears or rejects the unbind.  Restore only interfaces that this
        # call actually released; callers still handle a later usbip bind
        # failure with the complete original binding list.
        _restore_local_usb_drivers(busid, released)
        raise
    return bindings


def _restore_local_usb_drivers(busid: str, bindings: Iterable[tuple[Path, Path]]) -> None:
    """Best-effort restoration after an export failure or USB/IP unbind."""
    for interface, driver in bindings:
        if driver.name == "usbip-host" or not interface.exists():
            continue
        bind_path = driver / "bind"
        if not bind_path.exists():
            continue
        try:
            _write_sysfs(bind_path, interface.name)
            LOG.info("restored local USB interface %s to %s", interface.name, driver.name)
        except ControllerError as exc:
            LOG.warning("could not restore %s to %s: %s", interface.name, driver.name, exc)


def _probe_local_usb_device(busid: str) -> None:
    """Ask the kernel to reprobe a device after usbip-host releases it."""
    if not USB_SYSFS_DRIVERS_PROBE.exists():
        raise ControllerError("kernel does not expose /sys/bus/usb/drivers_probe")
    _write_sysfs(USB_SYSFS_DRIVERS_PROBE, _validate_busid(busid))


def attach(settings: Settings, spec: DeviceSpec) -> str:
    ensure_modules()
    check_remote(settings.remote_host, settings.remote_port)
    return run_checked(
        [
            find_usbip(),
            "--tcp-port",
            str(settings.remote_port),
            "attach",
            "--remote",
            settings.remote_host,
            "--busid",
            spec.busid,
        ],
        settings.command_timeout,
    )


def attach_to_proxy(host: str, port: int, busid: str, timeout: float) -> str:
    """Attach through a local backward-link proxy.

    Unlike :func:`attach`, this deliberately does not perform a preliminary
    reachability probe. The proxy pairs the attach socket with a validated
    cloud connection; probing first would consume that one-shot pairing.
    """
    ensure_modules()
    busid = _validate_busid(busid)
    return run_checked(
        [
            find_usbip(),
            "--tcp-port",
            str(port),
            "attach",
            "--remote",
            host,
            "--busid",
            busid,
        ],
        timeout,
    )


def detach(settings: Settings, port: str) -> str:
    if not port.isdigit():
        raise ControllerError("vhci port must be a numeric port index")
    return run_checked([find_usbip(), "detach", "--port", port], settings.command_timeout)


def export_device(settings: Settings, spec: LocalDeviceSpec) -> str:
    """Export one explicitly configured local bus ID through usbip-host."""
    _ensure_exportable_usb_device(spec.busid)
    ensure_export_modules()
    bindings = _release_local_usb_drivers(spec.busid)
    try:
        return run_checked(
            [find_usbip(), "bind", "--busid", spec.busid], settings.command_timeout
        )
    except ControllerError:
        _restore_local_usb_drivers(spec.busid, bindings)
        raise


def unexport_device(settings: Settings, spec: LocalDeviceSpec) -> str:
    """Release one explicitly configured local bus ID from usbip-host."""
    output = run_checked(
        [find_usbip(), "unbind", "--busid", spec.busid], settings.command_timeout
    )
    _probe_local_usb_device(spec.busid)
    return output


def port_indexes_for_busids(output: str, busids: Iterable[str]) -> list[str]:
    """Extract matching VHCI port indexes from `usbip port` output.

    The parser is intentionally best-effort.  If a distribution changes its
    human-readable format, the service can still stop safely; it will not guess
    a port and detach an unrelated device.
    """
    wanted = set(busids)
    current: str | None = None
    matches: list[str] = []
    for line in output.splitlines():
        stripped = line.strip()
        if stripped.startswith("Port "):
            current = stripped.split()[1].rstrip(":")
        if current is not None and any(busid in line for busid in wanted):
            if current.isdigit() and current not in matches:
                matches.append(current)
    return matches


def busids_in_ports(output: str, busids: Iterable[str]) -> set[str]:
    """Return configured bus IDs visibly present in a `usbip port` listing."""
    wanted = set(busids)
    return {busid for busid in wanted if busid in output}


def watch(settings: Settings, once: bool = False) -> int:
    if not settings.devices:
        raise ControllerError("[devices] is empty; add remote USB/IP bus IDs")
    stop = False

    def request_stop(signum: int, _frame: object) -> None:
        nonlocal stop
        LOG.info("received signal %s; stopping", signum)
        stop = True

    signal.signal(signal.SIGTERM, request_stop)
    signal.signal(signal.SIGINT, request_stop)
    attached: set[str] = set()
    while not stop:
        # A remote disconnect can leave the controller process alive.  Refresh
        # the human-readable VHCI state so a later pass can attach again.
        try:
            port_output = list_ports(settings)
            present = busids_in_ports(port_output, attached)
            detached = attached - present
            for busid in detached:
                LOG.info("remote device %s is no longer attached; will retry", busid)
            attached.intersection_update(present)
        except ControllerError as exc:
            LOG.debug("could not refresh VHCI state: %s", exc)
        for spec in settings.devices:
            if spec.busid in attached:
                continue
            try:
                LOG.info("attaching %s%s", spec.busid, f" ({spec.name})" if spec.name else "")
                LOG.info(attach(settings, spec).strip())
                attached.add(spec.busid)
            except ControllerError as exc:
                LOG.warning("attach %s failed: %s", spec.busid, exc)
        if once:
            break
        time.sleep(settings.reconnect_seconds)

    if settings.detach_on_exit and attached:
        try:
            output = list_ports(settings)
            for port in port_indexes_for_busids(output, attached):
                try:
                    LOG.info("detaching VHCI port %s", port)
                    LOG.info(detach(settings, port).strip())
                except ControllerError as exc:
                    LOG.warning("detach port %s failed: %s", port, exc)
        except ControllerError as exc:
            LOG.warning("could not enumerate VHCI ports during shutdown: %s", exc)
    return 0


def export_watch(settings: Settings, once: bool = False) -> int:
    """Keep only configured local USB devices exported for a remote USB/IP peer."""
    if not settings.local_devices:
        raise ControllerError("[local_devices] is empty; add local bus IDs explicitly")
    stop = False

    def request_stop(signum: int, _frame: object) -> None:
        nonlocal stop
        LOG.info("received signal %s; stopping local USB export", signum)
        stop = True

    signal.signal(signal.SIGTERM, request_stop)
    signal.signal(signal.SIGINT, request_stop)
    exported: set[str] = set()
    while not stop:
        for spec in settings.local_devices:
            if spec.busid in exported:
                continue
            try:
                LOG.info("exporting local USB %s%s", spec.busid, f" ({spec.name})" if spec.name else "")
                LOG.info(export_device(settings, spec).strip())
                exported.add(spec.busid)
            except ControllerError as exc:
                LOG.warning("export %s failed: %s", spec.busid, exc)
        if once:
            break
        time.sleep(settings.reconnect_seconds)

    if exported:
        for busid in sorted(exported):
            spec = next(item for item in settings.local_devices if item.busid == busid)
            try:
                LOG.info("unexporting local USB %s", busid)
                LOG.info(unexport_device(settings, spec).strip())
            except ControllerError as exc:
                LOG.warning("unexport %s failed: %s", busid, exc)
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default=DEFAULT_CONFIG, help="INI configuration path")
    parser.add_argument("--verbose", action="store_true", help="enable debug logging")
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("check", help="validate config and verify remote TCP reachability")
    subparsers.add_parser("probe", help="probe the standard USB/IP DEVLIST handshake")
    subparsers.add_parser(
        "probe-link",
        help="probe confirmed vendor link-init then standard USB/IP DEVLIST",
    )
    subparsers.add_parser("list", help="list devices exported by the remote USB/IP server")
    subparsers.add_parser("local-list", help="list local USB devices without changing bindings")
    subparsers.add_parser("doctor", help="run read-only local deployment checks")
    subparsers.add_parser("port", help="list locally imported VHCI devices")
    subparsers.add_parser(
        "backward",
        help="bridge validated ZTE 3246 USB control links to local standard USB/IP",
    )
    attach_parser = subparsers.add_parser("attach", help="attach one configured remote bus ID")
    attach_parser.add_argument("busid")
    detach_parser = subparsers.add_parser("detach", help="detach one local VHCI port")
    detach_parser.add_argument("port")
    watch_parser = subparsers.add_parser("watch", help="continuously attach configured bus IDs")
    watch_parser.add_argument("--once", action="store_true", help="attempt one attach pass")
    export_parser = subparsers.add_parser("export", help="export one configured local USB bus ID")
    export_parser.add_argument("busid")
    unexport_parser = subparsers.add_parser("unexport", help="release one configured local USB bus ID")
    unexport_parser.add_argument("busid")
    export_watch_parser = subparsers.add_parser(
        "export-watch", help="continuously export configured local USB bus IDs"
    )
    export_watch_parser.add_argument("--once", action="store_true", help="attempt one export pass")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    try:
        settings = load_settings(args.config)
        if args.command == "backward":
            return backward_bridge(load_backward_settings(args.config))
        if args.command == "check":
            check_remote(settings.remote_host, settings.remote_port)
            print(f"reachable: {settings.remote_host}:{settings.remote_port}")
        elif args.command == "probe":
            print(probe_protocol(settings))
        elif args.command == "probe-link":
            print(probe_link_protocol(settings))
        elif args.command == "list":
            print(list_remote(settings), end="")
        elif args.command == "local-list":
            print(list_local(settings), end="")
        elif args.command == "doctor":
            return doctor(settings)
        elif args.command == "port":
            print(list_ports(settings), end="")
        elif args.command == "attach":
            configured = {item.busid: item for item in settings.devices}
            if args.busid not in configured:
                raise ControllerError("busid is not present in [devices]")
            print(attach(settings, configured[args.busid]), end="")
        elif args.command == "detach":
            print(detach(settings, args.port), end="")
        elif args.command == "watch":
            return watch(settings, once=args.once)
        elif args.command == "export":
            configured = {item.busid: item for item in settings.local_devices}
            if args.busid not in configured:
                raise ControllerError("busid is not present in [local_devices]")
            print(export_device(settings, configured[args.busid]), end="")
        elif args.command == "unexport":
            configured = {item.busid: item for item in settings.local_devices}
            if args.busid not in configured:
                raise ControllerError("busid is not present in [local_devices]")
            print(unexport_device(settings, configured[args.busid]), end="")
        elif args.command == "export-watch":
            return export_watch(settings, once=args.once)
        return 0
    except ControllerError as exc:
        LOG.error("%s", exc)
        return 1
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
