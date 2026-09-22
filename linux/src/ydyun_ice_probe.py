#!/usr/bin/env python3
"""Offline SPICE/ICE framing probe.

Reads a user-supplied byte stream or file only.  It does not connect to a
remote host, decrypt TLS, or capture traffic.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

# The packaged probe lives in /usr/bin while the parser is kept in a private
# library directory.  The fallback keeps the source tree directly runnable.
sys.path.insert(0, "/usr/lib/ydyun-usb")
sys.path.insert(0, str(Path(__file__).resolve().parent))

from ydyun_spice import (
    DATA_HEADER,
    LINK_HEADER,
    SpiceDataParser,
    SpiceProtocolError,
    decode_display_message,
    extract_display_sample,
    parse_link_header,
)


def _json_line(value: dict) -> None:
    print(json.dumps(value, ensure_ascii=False, sort_keys=True))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", help="binary file, or '-' for stdin")
    parser.add_argument(
        "--mini-header",
        action="store_true",
        help="parse SpiceMiniDataHeader (6 bytes) instead of SpiceDataHeader",
    )
    parser.add_argument(
        "--link-header",
        action="store_true",
        help="consume and print a 16-byte SpiceLinkHeader before data frames",
    )
    parser.add_argument(
        "--extract-dir",
        metavar="DIR",
        help="write standard display encoded samples to DIR; no network is used",
    )
    args = parser.parse_args(argv)

    raw = sys.stdin.buffer.read() if args.input == "-" else Path(args.input).read_bytes()
    offset = 0
    if args.link_header:
        if len(raw) < LINK_HEADER.size:
            raise SpiceProtocolError("input ends before SpiceLinkHeader")
        link = parse_link_header(raw[:LINK_HEADER.size])
        _json_line(
            {
                "kind": "link",
                "magic": link.magic_text,
                "major": link.major_version,
                "minor": link.minor_version,
                "size": link.size,
            }
        )
        offset = LINK_HEADER.size

    frame_parser = SpiceDataParser(mini_header=args.mini_header)
    extract_dir = Path(args.extract_dir) if args.extract_dir else None
    if extract_dir is not None:
        extract_dir.mkdir(parents=True, exist_ok=True)
    stream_codecs: dict[int, str] = {}
    stream_counts: dict[int, int] = {}
    for frame in frame_parser.feed(raw[offset:]):
        record = {
            "kind": "data",
            "mini_header": frame.mini_header,
            "serial": frame.serial,
            "type": frame.message_type,
            "name": frame.message_name,
            "size": frame.size,
            "sub_list": frame.sub_list,
        }
        decoded = decode_display_message(frame)
        if decoded is not None:
            record["display"] = decoded
            if decoded.get("kind") == "stream_create":
                stream_codecs[int(decoded["stream_id"])] = str(decoded["codec"])
            if extract_dir is not None and decoded.get("kind") in {
                "stream_data",
                "stream_data_sized",
            }:
                stream_id = int(decoded["stream_id"])
                count = stream_counts.get(stream_id, 0)
                stream_counts[stream_id] = count + 1
                codec = stream_codecs.get(stream_id, "unknown")
                sample = extract_display_sample(frame)
                if sample is not None:
                    name = f"stream-{stream_id}-{codec}-{count:06d}.es"
                    destination = extract_dir / name
                    destination.write_bytes(sample)
                    record["sample_file"] = str(destination)
        _json_line(record)
    if frame_parser.buffered_bytes:
        raise SpiceProtocolError(
            f"trailing incomplete frame: {frame_parser.buffered_bytes} bytes"
        )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except SpiceProtocolError as exc:
        print(f"ydyun-ice-probe: {exc}", file=sys.stderr)
        raise SystemExit(2)
