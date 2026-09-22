import json
from pathlib import Path
import stat
import tempfile
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))

from ydyun_session import SessionFormatError, parse_session


class SessionProbeTests(unittest.TestCase):
    def test_parses_and_redacts_decoded_firm_auth(self):
        raw = {
            "data": {
                "vmId": "vm-1",
                "spuCode": "zte-soho",
                "vmcIp": "192.0.2.1",
                "vmcPort": 8443,
                "cagIp": "192.0.2.2",
                "cagPort": 8899,
                "vmUserName": "alice",
                "vmPassword": "secret-password",
                "authCode": "secret-auth",
                "token": "secret-token",
            }
        }
        descriptor = parse_session(raw)
        redacted = json.dumps(descriptor.redacted(), sort_keys=True)
        self.assertEqual(descriptor.route_count, 2)
        self.assertTrue(descriptor.has_password)
        self.assertTrue(descriptor.has_token)
        for secret in ("alice", "secret-password", "secret-auth", "secret-token"):
            self.assertNotIn(secret, redacted)

    def test_rejects_encrypted_data_and_invalid_host(self):
        with self.assertRaises(SessionFormatError):
            parse_session({"data": "base64-or-encrypted"})
        with self.assertRaises(SessionFormatError):
            parse_session({"vmId": "vm-1", "vmcIp": "127.0.0.1;touch", "vmcPort": 1})

    def test_missing_route_is_reported_by_descriptor(self):
        descriptor = parse_session({"vmId": "vm-1"})
        self.assertEqual(descriptor.route_count, 0)
        self.assertEqual(descriptor.redacted()["secret_presence"]["token"], False)

    def test_ipv6_cag_route_counts_without_printing_secrets(self):
        descriptor = parse_session(
            {
                "vmId": "vm-1",
                "cagIpv6": "2001:db8::10",
                "cagPort": 8899,
            }
        )
        self.assertEqual(descriptor.route_count, 1)
        self.assertEqual(descriptor.redacted()["cag_ipv6"], "2001:db8::10")

    def test_emits_owner_only_loader_config_without_redacting_the_summary(self):
        descriptor = parse_session(
            {
                "vmId": "vm-1",
                "vmUserName": "alice",
                "authCode": "secret-auth",
                "bizCode": "secret-biz",
            }
        )
        content = descriptor.loader_config(
            library="/opt/vendor/libchuanyun.so",
            server_ip="198.51.100.10",
            server_port=443,
            terminal_sn="terminal-1",
            unit_type="debian",
        )
        self.assertIn("auth_code = secret-auth", content)
        self.assertIn("biz_code = secret-biz", content)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "loader.conf"
            from ydyun_session import write_private_config

            write_private_config(output, content)
            self.assertEqual(stat.S_IMODE(output.stat().st_mode), 0o600)
            with self.assertRaises(FileExistsError):
                write_private_config(output, content)

    def test_loader_config_requires_public_sdk_credentials(self):
        descriptor = parse_session({"vmId": "vm-1"})
        with self.assertRaises(SessionFormatError):
            descriptor.loader_config(
                library="/opt/vendor/libchuanyun.so",
                server_ip="198.51.100.10",
                server_port=443,
            )


if __name__ == "__main__":
    unittest.main()
