#!/usr/bin/env python3
"""Bounded parser for the SPICE wire headers used by the ICE binaries.

This module deliberately parses only standard SPICE link/data framing.  It does
not open sockets, terminate TLS/KCP, or guess vendor-specific ICE messages.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import ipaddress
import re
import struct
from typing import List, Optional
from urllib.parse import urlsplit


SPICE_MAGIC = 0x51444552  # SPICE_MAGIC_CONST("REDQ") on little endian hosts.
SPICE_VERSION_MAJOR = 2
SPICE_VERSION_MINOR = 2

MAX_FRAME_SIZE = 64 * 1024 * 1024
ICE_LINK_MESSAGE_MAX = 0x2800
LINK_HEADER = struct.Struct("<IIII")
DATA_HEADER = struct.Struct("<QHII")
MINI_DATA_HEADER = struct.Struct("<HI")

CHANNEL_NAMES = {
    1: "main",
    2: "display",
    3: "inputs",
    4: "cursor",
    5: "playback",
    6: "record",
    7: "tunnel",
    8: "smartcard",
    9: "usbredir",
    10: "port",
    11: "webdav",
}

DISPLAY_MESSAGE_NAMES = {
    101: "display_mode",
    102: "display_mark",
    103: "display_reset",
    104: "display_copy_bits",
    105: "display_inval_list",
    106: "display_inval_all_pixmaps",
    107: "display_inval_palette",
    108: "display_inval_all_palettes",
    122: "display_stream_create",
    123: "display_stream_data",
    124: "display_stream_clip",
    125: "display_stream_destroy",
    126: "display_stream_destroy_all",
    302: "display_draw_fill",
    303: "display_draw_opaque",
    304: "display_draw_copy",
    305: "display_draw_blend",
    306: "display_draw_blackness",
    307: "display_draw_whiteness",
    308: "display_draw_invers",
    309: "display_draw_rop3",
    310: "display_draw_stroke",
    311: "display_draw_text",
    312: "display_draw_transparent",
    313: "display_draw_alpha_blend",
    314: "display_surface_create",
    315: "display_surface_destroy",
    316: "display_stream_data_sized",
    317: "display_monitors_config",
    318: "display_draw_composite",
    319: "display_stream_activate_report",
    320: "display_gl_scanout_unix",
    321: "display_gl_draw",
    322: "display_quality_indicator",
}

INPUTS_MESSAGE_NAMES = {
    101: "inputs_key_down",
    102: "inputs_key_up",
    103: "inputs_key_modifiers",
    104: "inputs_key_scancode",
    111: "inputs_mouse_motion",
    112: "inputs_mouse_position",
    113: "inputs_mouse_press",
    114: "inputs_mouse_release",
}

DISPLAY_CODEC_NAMES = {
    1: "mjpeg",
    2: "vp8",
    3: "h264",
    4: "vp9",
    5: "h265",
}

DISPLAY_SAMPLE_LAYOUTS = {
    123: (12, 8),  # SPICE_MSG_DISPLAY_STREAM_DATA
    316: (36, 32),  # SPICE_MSG_DISPLAY_STREAM_DATA_SIZED
}

INPUTS_SCANCODE_MAX = 2048
VIEWER_URL_MAX = 16 * 1024
VIEWER_URL_FIELD_MAX = 64
_VIEWER_SCHEMES = frozenset({"spice", "spice-conn"})
_SCHEME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*$")


class SpiceProtocolError(ValueError):
    """Raised when a SPICE header is malformed or exceeds configured limits."""


class NeedMoreData(Exception):
    """Internal signal used by the incremental parser."""


@dataclass(frozen=True)
class SpiceLinkHeader:
    magic: int
    major_version: int
    minor_version: int
    size: int

    @property
    def magic_text(self) -> str:
        return struct.pack("<I", self.magic).decode("ascii", errors="replace")


@dataclass(frozen=True)
class SpiceDataFrame:
    serial: Optional[int]
    message_type: int
    size: int
    sub_list: Optional[int]
    payload: bytes
    mini_header: bool = False

    @property
    def message_name(self) -> str:
        return DISPLAY_MESSAGE_NAMES.get(self.message_type, "unknown")


@dataclass(frozen=True)
class ViewerConnectionUrl:
    """The bounded, redacted shape of the URL handed to ``chuanyun-view``.

    The official viewer splits the part after ``://`` on ``+``.  Only the
    first field is an endpoint in the statically verified command
    ``spice://127.0.0.1:10800+...``; the remaining fields belong to the
    vendor session/config ABI and may contain credentials.  They are kept
    opaque here and are never included in ``repr``/JSON output.
    """

    scheme: str
    host: str
    port: int
    field_count: int
    extra_fields: tuple[str, ...] = field(repr=False)

    def redacted(self) -> dict[str, object]:
        return {
            "scheme": self.scheme,
            "host": self.host,
            "port": self.port,
            "field_count": self.field_count,
            "extra_field_count": len(self.extra_fields),
        }


def _parse_viewer_endpoint(value: str) -> tuple[str, int]:
    """Parse the first official viewer field without accepting shell syntax."""
    if not value or any(ord(ch) < 0x20 or ch.isspace() for ch in value):
        raise SpiceProtocolError("viewer endpoint contains whitespace/control data")
    try:
        parsed = urlsplit("//" + value, allow_fragments=False)
        host = parsed.hostname
        port = parsed.port
    except ValueError as exc:
        raise SpiceProtocolError("viewer endpoint is malformed") from exc
    if host is None or port is None:
        raise SpiceProtocolError("viewer endpoint must include host and port")
    if parsed.username is not None or parsed.password is not None:
        raise SpiceProtocolError("viewer endpoint must not include userinfo")
    if parsed.path or parsed.query or parsed.fragment:
        raise SpiceProtocolError("viewer endpoint must not include path/query/fragment")
    try:
        normalized_host = str(ipaddress.ip_address(host))
    except ValueError:
        if not re.fullmatch(r"[A-Za-z0-9](?:[A-Za-z0-9.-]{0,252}[A-Za-z0-9])?", host):
            raise SpiceProtocolError("viewer endpoint host is malformed")
        normalized_host = host
    return normalized_host, port


def parse_viewer_connection_url(value: str) -> ViewerConnectionUrl:
    """Validate the URL boundary used by the official Linux viewer.

    This is deliberately not a cloud connector.  It accepts the statically
    verified ``spice://host:port+opaque...`` form and returns only a safe
    summary plus opaque fields for an explicitly authorized caller.
    """
    if not isinstance(value, str) or not value:
        raise SpiceProtocolError("viewer URL must be a non-empty string")
    if len(value) > VIEWER_URL_MAX:
        raise SpiceProtocolError("viewer URL exceeds the size limit")
    if any(ord(ch) < 0x20 or ch.isspace() for ch in value):
        raise SpiceProtocolError("viewer URL contains whitespace/control data")
    separator = value.find("://")
    if separator <= 0:
        raise SpiceProtocolError("viewer URL must contain scheme://")
    scheme = value[:separator].lower()
    if not _SCHEME_RE.fullmatch(scheme) or scheme not in _VIEWER_SCHEMES:
        raise SpiceProtocolError("unsupported viewer URL scheme")
    fields = tuple(value[separator + 3 :].split("+"))
    if not fields or not fields[0] or len(fields) > VIEWER_URL_FIELD_MAX:
        raise SpiceProtocolError("viewer URL has an invalid field count")
    host, port = _parse_viewer_endpoint(fields[0])
    return ViewerConnectionUrl(scheme, host, port, len(fields), fields[1:])


def _check_size(size: int, max_size: int) -> None:
    if size < 0 or size > max_size:
        raise SpiceProtocolError(
            f"SPICE payload size {size} exceeds limit {max_size}"
        )


def parse_link_header(data: bytes, *, max_size: int = ICE_LINK_MESSAGE_MAX) -> SpiceLinkHeader:
    if len(data) < LINK_HEADER.size:
        raise NeedMoreData
    magic, major, minor, size = LINK_HEADER.unpack_from(data)
    if magic != SPICE_MAGIC:
        raise SpiceProtocolError(
            f"invalid SPICE magic 0x{magic:08x} ({struct.pack('<I', magic)!r})"
        )
    _check_size(size, max_size)
    return SpiceLinkHeader(magic, major, minor, size)


def parse_data_frame(
    data: bytes,
    *,
    mini_header: bool = False,
    max_size: int = MAX_FRAME_SIZE,
) -> SpiceDataFrame:
    header = MINI_DATA_HEADER if mini_header else DATA_HEADER
    if len(data) < header.size:
        raise NeedMoreData
    if mini_header:
        message_type, size = header.unpack_from(data)
        serial = None
        sub_list = None
    else:
        serial, message_type, size, sub_list = header.unpack_from(data)
    _check_size(size, max_size)
    end = header.size + size
    if len(data) < end:
        raise NeedMoreData
    return SpiceDataFrame(
        serial=serial,
        message_type=message_type,
        size=size,
        sub_list=sub_list,
        payload=bytes(data[header.size:end]),
        mini_header=mini_header,
    )


class SpiceDataParser:
    """Incrementally parse standard SPICE data frames with a hard size limit."""

    def __init__(self, *, mini_header: bool = False, max_size: int = MAX_FRAME_SIZE):
        self.mini_header = mini_header
        self.max_size = max_size
        self._buffer = bytearray()

    def feed(self, data: bytes) -> List[SpiceDataFrame]:
        if data:
            self._buffer.extend(data)
        frames: List[SpiceDataFrame] = []
        while self._buffer:
            try:
                frame = parse_data_frame(
                    self._buffer,
                    mini_header=self.mini_header,
                    max_size=self.max_size,
                )
            except NeedMoreData:
                break
            header_size = MINI_DATA_HEADER.size if self.mini_header else DATA_HEADER.size
            del self._buffer[: header_size + frame.size]
            frames.append(frame)
        return frames

    @property
    def buffered_bytes(self) -> int:
        return len(self._buffer)


def _read_clip_metadata(payload: bytes, offset: int) -> tuple[dict[str, object], int]:
    """Read the standard variable-length SpiceClip prefix."""
    if len(payload) < offset + 1:
        raise SpiceProtocolError("display stream-create payload ends before clip")
    clip_type = payload[offset]
    offset += 1
    result: dict[str, object] = {
        "clip_type": "none" if clip_type == 0 else ("rects" if clip_type == 1 else "unknown"),
        "clip_type_value": clip_type,
    }
    if clip_type == 1:
        if len(payload) < offset + 4:
            raise SpiceProtocolError("display rectangle clip ends before count")
        count = struct.unpack_from("<I", payload, offset)[0]
        offset += 4
        rect_bytes = count * 16
        if count > (MAX_FRAME_SIZE - offset) // 16 or len(payload) < offset + rect_bytes:
            raise SpiceProtocolError("display rectangle clip exceeds message bounds")
        result["clip_rect_count"] = count
        offset += rect_bytes
    return result, offset


def decode_display_message(frame: SpiceDataFrame) -> dict[str, object] | None:
    """Decode fixed metadata for standard SPICE display messages.

    This is not a video decoder and does not interpret ZTE ICE extensions.  For
    stream-data messages it exposes the codec/stream/time fields and encoded
    byte count, leaving the encoded sample in ``frame.payload``.
    """
    payload = frame.payload
    message_type = frame.message_type
    if message_type == 122:  # SPICE_MSG_DISPLAY_STREAM_CREATE
        if len(payload) < 50:
            raise SpiceProtocolError("stream-create payload is shorter than its fixed header")
        surface_id, stream_id = struct.unpack_from("<II", payload, 0)
        flags, codec_type = struct.unpack_from("<BB", payload, 8)
        stamp = struct.unpack_from("<Q", payload, 10)[0]
        stream_width, stream_height, src_width, src_height = struct.unpack_from(
            "<IIII", payload, 18
        )
        dest = struct.unpack_from("<iiii", payload, 34)
        clip, _ = _read_clip_metadata(payload, 50)
        return {
            "kind": "stream_create",
            "surface_id": surface_id,
            "stream_id": stream_id,
            "flags": flags,
            "codec": DISPLAY_CODEC_NAMES.get(codec_type, "unknown"),
            "codec_value": codec_type,
            "stamp": stamp,
            "stream_width": stream_width,
            "stream_height": stream_height,
            "src_width": src_width,
            "src_height": src_height,
            "dest": {"top": dest[0], "left": dest[1], "bottom": dest[2], "right": dest[3]},
            **clip,
        }
    if message_type == 123:  # SPICE_MSG_DISPLAY_STREAM_DATA
        if len(payload) < 12:
            raise SpiceProtocolError("stream-data payload is shorter than its fixed header")
        stream_id, multimedia_time, data_size = struct.unpack_from("<III", payload, 0)
        available = len(payload) - 12
        if data_size > available:
            raise SpiceProtocolError("stream-data sample exceeds message bounds")
        return {
            "kind": "stream_data",
            "stream_id": stream_id,
            "multimedia_time": multimedia_time,
            "encoded_size": data_size,
            "sample_offset": 12,
            "sample_size": data_size,
            "trailing_size": available - data_size,
        }
    if message_type == 125:  # SPICE_MSG_DISPLAY_STREAM_DESTROY
        if len(payload) < 4:
            raise SpiceProtocolError("stream-destroy payload is shorter than its fixed header")
        return {"kind": "stream_destroy", "stream_id": struct.unpack_from("<I", payload)[0]}
    if message_type == 314:  # SPICE_MSG_DISPLAY_SURFACE_CREATE
        if len(payload) < 20:
            raise SpiceProtocolError("surface-create payload is shorter than its fixed header")
        surface_id, width, height, format_value, flags = struct.unpack_from("<IIIII", payload)
        return {
            "kind": "surface_create",
            "surface_id": surface_id,
            "width": width,
            "height": height,
            "format": format_value,
            "flags": flags,
        }
    if message_type == 316:  # SPICE_MSG_DISPLAY_STREAM_DATA_SIZED
        if len(payload) < 36:
            raise SpiceProtocolError("sized stream-data payload is shorter than its fixed header")
        stream_id, multimedia_time = struct.unpack_from("<II", payload, 0)
        width, height = struct.unpack_from("<II", payload, 8)
        dest = struct.unpack_from("<iiii", payload, 16)
        data_size = struct.unpack_from("<I", payload, 32)[0]
        available = len(payload) - 36
        if data_size > available:
            raise SpiceProtocolError("sized stream-data sample exceeds message bounds")
        return {
            "kind": "stream_data_sized",
            "stream_id": stream_id,
            "multimedia_time": multimedia_time,
            "width": width,
            "height": height,
            "dest": {"top": dest[0], "left": dest[1], "bottom": dest[2], "right": dest[3]},
            "encoded_size": data_size,
            "sample_offset": 36,
            "sample_size": data_size,
            "trailing_size": available - data_size,
        }
    return None


def extract_display_sample(frame: SpiceDataFrame) -> bytes | None:
    """Return a bounded encoded video sample from a standard display message.

    The caller is responsible for selecting a destination.  This helper never
    opens a file and does not interpret the sample as a particular codec.
    """
    layout = DISPLAY_SAMPLE_LAYOUTS.get(frame.message_type)
    if layout is None:
        return None
    offset, size_offset = layout
    if frame.message_type == 123:
        size = struct.unpack_from("<I", frame.payload, size_offset)[0]
    else:
        size = struct.unpack_from("<I", frame.payload, size_offset)[0]
    end = offset + size
    if end > len(frame.payload):
        raise SpiceProtocolError("display sample exceeds message bounds")
    return frame.payload[offset:end]


def decode_inputs_message(frame: SpiceDataFrame) -> dict[str, object] | None:
    """Decode the fixed standard SPICE inputs-channel payloads.

    The caller must have selected the inputs channel (channel type 3).  The
    same numeric message IDs are used by other SPICE channels, so this helper
    deliberately does not infer a channel from ``SpiceDataFrame`` alone.
    """
    payload = frame.payload
    message_type = frame.message_type
    kind = INPUTS_MESSAGE_NAMES.get(message_type)
    if kind is None:
        return None
    if message_type in {101, 102}:
        if len(payload) < 4:
            raise SpiceProtocolError("inputs key payload is shorter than 4 bytes")
        return {"kind": kind, "code": struct.unpack_from("<I", payload)[0]}
    if message_type == 104:
        if not payload:
            raise SpiceProtocolError("inputs scancode payload is empty")
        if len(payload) > INPUTS_SCANCODE_MAX:
            raise SpiceProtocolError("inputs scancode payload exceeds 2 KiB")
        return {"kind": kind, "scancodes": bytes(payload)}
    if message_type == 103:
        if len(payload) < 2:
            raise SpiceProtocolError("inputs modifiers payload is shorter than 2 bytes")
        return {"kind": kind, "modifiers": struct.unpack_from("<H", payload)[0]}
    if message_type == 111:
        if len(payload) < 10:
            raise SpiceProtocolError("inputs mouse-motion payload is shorter than 10 bytes")
        dx, dy, buttons_state = struct.unpack_from("<iiH", payload)
        return {
            "kind": kind,
            "dx": dx,
            "dy": dy,
            "buttons_state": buttons_state,
        }
    if message_type == 112:
        if len(payload) < 11:
            raise SpiceProtocolError("inputs mouse-position payload is shorter than 11 bytes")
        x, y, buttons_state, display_id = struct.unpack_from("<IIHB", payload)
        return {
            "kind": kind,
            "x": x,
            "y": y,
            "buttons_state": buttons_state,
            "display_id": display_id,
        }
    if message_type in {113, 114}:
        if len(payload) < 3:
            raise SpiceProtocolError("inputs mouse-button payload is shorter than 3 bytes")
        button, buttons_state = struct.unpack_from("<BH", payload)
        return {
            "kind": kind,
            "button": button,
            "buttons_state": buttons_state,
        }
    return None


def _checked_input_int(name: str, value: object, lower: int, upper: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not lower <= value <= upper:
        raise SpiceProtocolError(f"inputs {name} must be an integer in [{lower}, {upper}]")
    return value


def encode_inputs_message(
    message_type: int,
    *,
    serial: int = 0,
    sub_list: int = 0,
    mini_header: bool = False,
    code: int | None = None,
    modifiers: int | None = None,
    scancodes: bytes | bytearray | memoryview | None = None,
    dx: int | None = None,
    dy: int | None = None,
    buttons_state: int | None = None,
    x: int | None = None,
    y: int | None = None,
    display_id: int | None = None,
    button: int | None = None,
) -> bytes:
    """Encode one standard SPICE inputs-channel client message.

    This builds only the channel data frame; the caller still has to complete
    SPICE channel negotiation and send it on the inputs channel. The
    ``key_scancode`` message is raw variable-length data, matching the
    reference protocol rather than treating a scancode sequence as a uint32.
    """
    if message_type in {101, 102}:
        if code is None:
            raise SpiceProtocolError("inputs key message requires code")
        payload = struct.pack("<I", _checked_input_int("code", code, 0, 0xFFFFFFFF))
    elif message_type == 103:
        if modifiers is None:
            raise SpiceProtocolError("inputs modifiers message requires modifiers")
        payload = struct.pack(
            "<H", _checked_input_int("modifiers", modifiers, 0, 0xFFFF)
        )
    elif message_type == 104:
        if scancodes is None:
            raise SpiceProtocolError("inputs scancode message requires scancodes")
        payload = bytes(scancodes)
        if not payload or len(payload) > INPUTS_SCANCODE_MAX:
            raise SpiceProtocolError("inputs scancodes must contain 1..2048 bytes")
    elif message_type == 111:
        if dx is None or dy is None or buttons_state is None:
            raise SpiceProtocolError("inputs mouse-motion message requires dx, dy and buttons_state")
        payload = struct.pack(
            "<iiH",
            _checked_input_int("dx", dx, -0x80000000, 0x7FFFFFFF),
            _checked_input_int("dy", dy, -0x80000000, 0x7FFFFFFF),
            _checked_input_int("buttons_state", buttons_state, 0, 0xFFFF),
        )
    elif message_type == 112:
        if x is None or y is None or buttons_state is None or display_id is None:
            raise SpiceProtocolError(
                "inputs mouse-position message requires x, y, buttons_state and display_id"
            )
        payload = struct.pack(
            "<IIHB",
            _checked_input_int("x", x, 0, 0xFFFFFFFF),
            _checked_input_int("y", y, 0, 0xFFFFFFFF),
            _checked_input_int("buttons_state", buttons_state, 0, 0xFFFF),
            _checked_input_int("display_id", display_id, 0, 0xFF),
        )
    elif message_type in {113, 114}:
        if button is None or buttons_state is None:
            raise SpiceProtocolError(
                "inputs mouse-button message requires button and buttons_state"
            )
        payload = struct.pack(
            "<BH",
            _checked_input_int("button", button, 0, 0xFF),
            _checked_input_int("buttons_state", buttons_state, 0, 0xFFFF),
        )
    else:
        raise SpiceProtocolError(f"unsupported inputs client message type: {message_type}")

    if mini_header:
        return MINI_DATA_HEADER.pack(message_type, len(payload)) + payload
    if not 0 <= serial <= 0xFFFFFFFFFFFFFFFF:
        raise SpiceProtocolError("inputs serial is out of range")
    if not 0 <= sub_list <= 0xFFFFFFFF:
        raise SpiceProtocolError("inputs sub_list is out of range")
    return DATA_HEADER.pack(serial, message_type, len(payload), sub_list) + payload
