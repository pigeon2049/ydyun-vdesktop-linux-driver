import unittest

from capture_local_usbipc import decode_message_header


class LocalUsbIpcHeaderTests(unittest.TestCase):
    def test_observed_registration_header_is_little_endian(self):
        prefix = bytes.fromhex("01000000000000004ccf030000000000")
        self.assertEqual(decode_message_header(prefix), (1, 0, 0x3CF4C, 0))

    def test_short_prefix_is_rejected(self):
        self.assertIsNone(decode_message_header(b"short"))
