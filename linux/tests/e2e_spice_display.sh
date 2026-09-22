#!/bin/bash
set -eu

# Offline display-data integration test. It validates the standard SPICE display
# sample boundary and the Debian FFmpeg decode handoff; it never opens a socket.

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SAMPLE="$ROOT/build/spice-e2e-sample.h264"
WIRE="$ROOT/build/spice-e2e-wire.bin"
JSON="$ROOT/build/spice-e2e-probe.jsonl"
OUT="$ROOT/build/spice-e2e-display"
PNG="$ROOT/build/spice-e2e-frame.png"

cleanup() {
    status=$?
    set +e
    for path in "$SAMPLE" "$WIRE" "$JSON" "$PNG"; do
        if [ -e "$path" ]; then
            unlink "$path"
        fi
    done
    if [ -d "$OUT" ]; then
        find "$OUT" -type f -delete
        rmdir "$OUT"
    fi
    return "$status"
}
trap cleanup EXIT INT TERM

if ! command -v ffmpeg >/dev/null 2>&1 || ! command -v ffprobe >/dev/null 2>&1; then
    echo 'e2e_spice_display.sh: ffmpeg and ffprobe are required' >&2
    exit 77
fi
if [ -e "$SAMPLE" ] || [ -e "$WIRE" ] || [ -e "$JSON" ] || [ -e "$PNG" ] || [ -e "$OUT" ]; then
    echo 'e2e_spice_display.sh: build output already exists' >&2
    exit 1
fi

ffmpeg -hide_banner -loglevel error \
    -f lavfi -i testsrc=size=64x48:rate=1 -frames:v 1 \
    -c:v libx264 -preset ultrafast -tune zerolatency -f h264 "$SAMPLE"

python3 - "$SAMPLE" "$WIRE" <<'PY'
import struct
import sys
from pathlib import Path

sample = Path(sys.argv[1]).read_bytes()
if not sample:
    raise SystemExit("empty H.264 fixture")

# SPICE_MSG_DISPLAY_STREAM_CREATE: H.264 stream 7, 64x48, no clip.
create_payload = struct.pack(
    "<IIBBQIIIIiiiiB",
    0, 7, 0, 3, 0,
    64, 48, 64, 48,
    0, 0, 64, 48, 0,
)
data_payload = struct.pack("<III", 7, 0, len(sample)) + sample
data_header = struct.Struct("<QHII")
link_header = struct.pack("<IIII", 0x51444552, 2, 2, 0)
wire = bytearray(link_header)
wire += data_header.pack(1, 122, len(create_payload), 0) + create_payload
wire += data_header.pack(2, 123, len(data_payload), 0) + data_payload
Path(sys.argv[2]).write_bytes(wire)
PY

/usr/bin/ydyun-ice-probe --link-header --extract-dir "$OUT" "$WIRE" > "$JSON"
sample_out=$(find "$OUT" -type f -name '*.es' -print -quit)
if [ -z "$sample_out" ]; then
    echo 'e2e_spice_display.sh: probe did not extract a display sample' >&2
    exit 1
fi
cmp "$SAMPLE" "$sample_out"
codec=$(ffprobe -v error -select_streams v:0 \
    -show_entries stream=codec_name,width,height -of csv=p=0 "$sample_out")
[ "$codec" = 'h264,64,48' ]
ffmpeg -hide_banner -loglevel error -y -i "$sample_out" -frames:v 1 "$PNG"
test -s "$PNG"
grep -F '"magic": "REDQ"' "$JSON" >/dev/null
grep -F '"sample_file"' "$JSON" >/dev/null
printf '%s\n' 'PASS SPICE display stream -> bounded extraction -> FFmpeg decode'
