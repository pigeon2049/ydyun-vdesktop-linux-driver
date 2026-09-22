import struct
import tempfile
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))

from ydyun_spice import (
    DATA_HEADER,
    MINI_DATA_HEADER,
    SPICE_MAGIC,
    SpiceDataParser,
    SpiceProtocolError,
    NeedMoreData,
    decode_display_message,
    decode_inputs_message,
    encode_inputs_message,
    extract_display_sample,
    parse_viewer_connection_url,
    parse_data_frame,
    parse_link_header,
)
from ydyun_ice_probe import main as probe_main


class SpiceParserTests(unittest.TestCase):
    def test_link_header_uses_redq_magic(self):
        raw = struct.pack("<IIII", SPICE_MAGIC, 2, 2, 12)
        header = parse_link_header(raw)
        self.assertEqual(header.magic_text, "REDQ")
        self.assertEqual(header.major_version, 2)
        self.assertEqual(header.size, 12)

    def test_parses_official_viewer_url_without_exposing_opaque_fields(self):
        parsed = parse_viewer_connection_url(
            "spice://127.0.0.1:10800+session-id+opaque-token"
        )
        self.assertEqual(parsed.scheme, "spice")
        self.assertEqual(parsed.host, "127.0.0.1")
        self.assertEqual(parsed.port, 10800)
        self.assertEqual(parsed.field_count, 3)
        self.assertEqual(parsed.redacted()["extra_field_count"], 2)
        self.assertNotIn("opaque-token", repr(parsed))
        self.assertNotIn("opaque-token", repr(parsed.redacted()))

    def test_viewer_url_accepts_ipv6_and_rejects_shellish_endpoints(self):
        parsed = parse_viewer_connection_url("spice-conn://[2001:db8::10]:5900+")
        self.assertEqual(parsed.host, "2001:db8::10")
        self.assertEqual(parsed.port, 5900)
        for value in (
            "spice://user:pass@host:5900",
            "spice://host:5900/path",
            "spice://host:5900?x=1",
            "spice://host:5900;touch+file",
            "http://host:5900",
        ):
            with self.subTest(value=value):
                with self.assertRaises(SpiceProtocolError):
                    parse_viewer_connection_url(value)

    def test_data_frame_and_display_name(self):
        raw = DATA_HEADER.pack(9, 123, 3, 0) + b"abc"
        frame = parse_data_frame(raw)
        self.assertEqual(frame.serial, 9)
        self.assertEqual(frame.message_type, 123)
        self.assertEqual(frame.message_name, "display_stream_data")
        self.assertEqual(frame.payload, b"abc")

    def test_incremental_parser_handles_split_header_and_payload(self):
        raw = DATA_HEADER.pack(1, 314, 5, 0) + b"hello"
        stream = SpiceDataParser()
        self.assertEqual(stream.feed(raw[:4]), [])
        self.assertEqual(stream.feed(raw[4:20]), [])
        frames = stream.feed(raw[20:])
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0].payload, b"hello")
        self.assertEqual(stream.buffered_bytes, 0)

    def test_mini_header(self):
        raw = MINI_DATA_HEADER.pack(101, 2) + b"xy"
        frame = parse_data_frame(raw, mini_header=True)
        self.assertIsNone(frame.serial)
        self.assertTrue(frame.mini_header)

    def test_rejects_bad_magic_and_oversized_payload(self):
        with self.assertRaises(SpiceProtocolError):
            parse_link_header(struct.pack("<IIII", 0, 2, 2, 0))
        with self.assertRaises(SpiceProtocolError):
            parse_link_header(struct.pack("<IIII", SPICE_MAGIC, 2, 2, 0x2801))
        with self.assertRaises(SpiceProtocolError):
            parse_data_frame(DATA_HEADER.pack(1, 1, 9, 0), max_size=8)

    def test_incomplete_input_is_not_accepted(self):
        with self.assertRaises(NeedMoreData):
            parse_data_frame(DATA_HEADER.pack(1, 1, 2, 0) + b"x")

    def test_decodes_standard_stream_create_metadata(self):
        payload = struct.pack(
            "<IIBBQIIIIiiiiB",
            2, 7, 0, 3, 99,
            1920, 1080, 1920, 1080,
            0, 0, 1920, 1080, 0,
        )
        frame = parse_data_frame(DATA_HEADER.pack(1, 122, len(payload), 0) + payload)
        decoded = decode_display_message(frame)
        self.assertEqual(decoded["kind"], "stream_create")
        self.assertEqual(decoded["codec"], "h264")
        self.assertEqual(decoded["stream_id"], 7)

    def test_decodes_standard_stream_data_metadata(self):
        sample = b"\x00\x00\x01\x65abc"
        payload = struct.pack("<III", 7, 1234, len(sample)) + sample
        frame = parse_data_frame(DATA_HEADER.pack(2, 123, len(payload), 0) + payload)
        decoded = decode_display_message(frame)
        self.assertEqual(decoded["kind"], "stream_data")
        self.assertEqual(decoded["encoded_size"], len(sample))
        self.assertEqual(decoded["sample_size"], len(sample))
        self.assertEqual(extract_display_sample(frame), sample)
        self.assertEqual(decoded["trailing_size"], 0)

    def test_probe_extracts_samples_to_explicit_directory(self):
        sample = b"\x00\x00\x01\x65abc"
        payload = struct.pack("<III", 7, 1234, len(sample)) + sample
        raw = DATA_HEADER.pack(2, 123, len(payload), 0) + payload
        with tempfile.TemporaryDirectory() as tmp:
            input_path = Path(tmp) / "input.bin"
            extract_dir = Path(tmp) / "out"
            input_path.write_bytes(raw)
            output = StringIO()
            with redirect_stdout(output):
                self.assertEqual(
                    probe_main(["--extract-dir", str(extract_dir), str(input_path)]),
                    0,
                )
            files = sorted(extract_dir.glob("*.es"))
            self.assertEqual(len(files), 1)
            self.assertEqual(files[0].read_bytes(), sample)
            self.assertIn('"sample_file"', output.getvalue())

    def test_decodes_keyboard_and_mouse_inputs_messages(self):
        cases = [
            (101, struct.pack("<I", 0x1E), {"kind": "inputs_key_down", "code": 0x1E}),
            (104, b"\x1d\xe0\x9d", {"kind": "inputs_key_scancode", "scancodes": b"\x1d\xe0\x9d"}),
            (103, struct.pack("<H", 0x05), {"kind": "inputs_key_modifiers", "modifiers": 0x05}),
            (111, struct.pack("<iiH", -3, 7, 1), {
                "kind": "inputs_mouse_motion", "dx": -3, "dy": 7, "buttons_state": 1,
            }),
            (112, struct.pack("<IIHB", 120, 240, 2, 0), {
                "kind": "inputs_mouse_position", "x": 120, "y": 240,
                "buttons_state": 2, "display_id": 0,
            }),
            (113, struct.pack("<BH", 1, 1), {
                "kind": "inputs_mouse_press", "button": 1, "buttons_state": 1,
            }),
            (114, struct.pack("<BH", 1, 0), {
                "kind": "inputs_mouse_release", "button": 1, "buttons_state": 0,
            }),
        ]
        for message_type, payload, expected in cases:
            with self.subTest(message_type=message_type):
                frame = parse_data_frame(
                    DATA_HEADER.pack(3, message_type, len(payload), 0) + payload
                )
                self.assertEqual(decode_inputs_message(frame), expected)

    def test_rejects_truncated_inputs_messages(self):
        frame = parse_data_frame(DATA_HEADER.pack(3, 112, 2, 0) + b"xx")
        with self.assertRaises(SpiceProtocolError):
            decode_inputs_message(frame)

    def test_encodes_inputs_frames_for_keyboard_and_mouse(self):
        cases = [
            (101, {"code": 0x1E}, struct.pack("<I", 0x1E)),
            (104, {"scancodes": b"\x1d\xe0\x9d"}, b"\x1d\xe0\x9d"),
            (103, {"modifiers": 5}, struct.pack("<H", 5)),
            (111, {"dx": -3, "dy": 7, "buttons_state": 1}, struct.pack("<iiH", -3, 7, 1)),
            (112, {"x": 120, "y": 240, "buttons_state": 2, "display_id": 0}, struct.pack("<IIHB", 120, 240, 2, 0)),
            (113, {"button": 1, "buttons_state": 1}, struct.pack("<BH", 1, 1)),
        ]
        for message_type, fields, payload in cases:
            with self.subTest(message_type=message_type):
                raw = encode_inputs_message(message_type, serial=9, **fields)
                frame = parse_data_frame(raw)
                self.assertEqual(frame.serial, 9)
                self.assertEqual(frame.message_type, message_type)
                self.assertEqual(frame.payload, payload)

    def test_scancode_is_bounded_variable_length_data(self):
        with self.assertRaises(SpiceProtocolError):
            encode_inputs_message(104, scancodes=b"x" * 2049)
        with self.assertRaises(SpiceProtocolError):
            decode_inputs_message(
                parse_data_frame(DATA_HEADER.pack(3, 104, 0, 0))
            )


if __name__ == "__main__":
    unittest.main()
