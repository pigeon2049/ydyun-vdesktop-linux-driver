#!/usr/bin/env python3
import configparser
import socket
import struct
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))
import ydyun_usbctl as ctl


class UsbCtlTests(unittest.TestCase):
    def test_builds_observed_vendor_proxy_preamble_without_session_material(self):
        header = ctl.build_vendor_proxy_header("127.0.0.1", 40955)
        self.assertEqual(len(header), 0x74)
        self.assertEqual(struct.unpack_from("<I", header, 0x00)[0], 1)
        self.assertEqual(struct.unpack_from("<I", header, 0x08)[0], 3246)
        self.assertEqual(struct.unpack_from("<I", header, 0x0C)[0], 2)
        self.assertEqual(header[0x10:0x12], b"u\x02")
        self.assertEqual(header[0x12:0x12 + 9].rstrip(b"\x00"), b"127.0.0.1")
        self.assertEqual(header[0x33:0x33 + 5].rstrip(b"\x00"), b"40955")

    def test_vendor_proxy_preamble_rejects_oversized_destination(self):
        with self.assertRaises(ctl.ControllerError):
            ctl.build_vendor_proxy_header("a" * 34, 40955)

    def test_load_settings_and_devices(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "config.ini"
            path.write_text(
                "[connection]\n"
                "remote_host = 192.0.2.10\n"
                "remote_port = 3240\n"
                "reconnect_seconds = 2\n"
                "detach_on_exit = no\n\n"
                "[devices]\n"
                "1-2 = storage\n"
                "1-3 = keyboard\n",
                encoding="utf-8",
            )
            settings = ctl.load_settings(str(path))
        self.assertEqual(settings.remote_host, "192.0.2.10")
        self.assertEqual(settings.remote_port, 3240)
        self.assertEqual([d.busid for d in settings.devices], ["1-2", "1-3"])
        self.assertFalse(settings.detach_on_exit)

    def test_loads_local_export_devices_separately(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "config.ini"
            path.write_text(
                "[connection]\n"
                "remote_host = 192.0.2.10\n"
                "remote_port = 3240\n\n"
                "[devices]\n"
                "2-1 = cloud storage\n\n"
                "[local_devices]\n"
                "3-9 = local mouse\n",
                encoding="utf-8",
            )
            settings = ctl.load_settings(str(path))
        self.assertEqual([d.busid for d in settings.devices], ["2-1"])
        self.assertEqual([d.busid for d in settings.local_devices], ["3-9"])
        self.assertEqual(settings.local_devices[0].name, "local mouse")

    def test_doctor_is_read_only_and_does_not_contact_remote(self):
        settings = ctl.Settings("192.0.2.10", 3240, (), 5, True, 30)
        with mock.patch.object(ctl.platform, "machine", return_value="x86_64"), \
                mock.patch.object(ctl, "find_usbip", return_value="/usr/sbin/usbip"), \
                mock.patch.object(ctl, "_module_available", return_value=True), \
                mock.patch.object(ctl, "run_checked", return_value="Imported USB devices\n") as run:
            self.assertEqual(ctl.doctor(settings), 0)
        commands = [call.args[0] for call in run.call_args_list]
        self.assertIn(["/usr/sbin/usbip", "port"], commands)
        self.assertIn(["/usr/sbin/usbip", "list", "--local"], commands)
        self.assertFalse(any("3240" in " ".join(command) for command in commands))

    def test_rejects_shell_like_host_and_busid(self):
        with self.assertRaises(ctl.ControllerError):
            ctl._validate_host("127.0.0.1; touch /tmp/pwned")
        with self.assertRaises(ctl.ControllerError):
            ctl._validate_busid("1-2;touch")
        with self.assertRaises(ctl.ControllerError):
            ctl._validate_busid("../../sys")

    def test_port_parser_is_conservative(self):
        output = (
            "Port 00: <Port in Use>\n"
            "\tPort 00: at high-speed\n"
            "\t1-2 -> storage\n"
            "Port 01: <Port in Use>\n"
            "\t1-3 -> keyboard\n"
        )
        self.assertEqual(ctl.port_indexes_for_busids(output, ["1-2"]), ["00"])
        self.assertEqual(ctl.port_indexes_for_busids(output, ["not-present"]), [])
        self.assertEqual(ctl.busids_in_ports(output, ["1-2", "1-3", "2-9"]), {"1-2", "1-3"})

    def test_attach_uses_standard_usbip_arguments(self):
        settings = ctl.Settings("192.0.2.10", 3240, (), 5, True, 30)
        spec = ctl.DeviceSpec("1-2", "storage")
        with mock.patch.object(ctl, "ensure_modules"), \
                mock.patch.object(ctl, "check_remote"), \
                mock.patch.object(ctl, "find_usbip", return_value="/usr/sbin/usbip"), \
                mock.patch.object(ctl, "run_checked", return_value="ok") as run:
            self.assertEqual(ctl.attach(settings, spec), "ok")
        run.assert_called_once_with(
            [
                "/usr/sbin/usbip", "--tcp-port", "3240", "attach",
                "--remote", "192.0.2.10", "--busid", "1-2",
            ],
            30,
        )

    def test_export_uses_usbip_host_bind_and_never_remote_attach(self):
        settings = ctl.Settings("192.0.2.10", 3240, (), 5, True, 30)
        spec = ctl.LocalDeviceSpec("3-9", "local mouse")
        with mock.patch.object(ctl, "ensure_export_modules"), \
                mock.patch.object(ctl, "_release_local_usb_drivers", return_value=[]), \
                mock.patch.object(ctl, "find_usbip", return_value="/usr/sbin/usbip"), \
                mock.patch.object(ctl, "run_checked", return_value="ok") as run:
            self.assertEqual(ctl.export_device(settings, spec), "ok")
        run.assert_called_once_with(
            ["/usr/sbin/usbip", "bind", "--busid", "3-9"], 30
        )

    def test_export_rejects_usb_hub_before_loading_or_unbinding(self):
        with tempfile.TemporaryDirectory() as tmp:
            devices = Path(tmp) / "devices"
            device = devices / "3-0"
            device.mkdir(parents=True)
            (device / "bDeviceClass").write_text("09\n", encoding="ascii")
            with mock.patch.object(ctl, "USB_SYSFS_DEVICES", devices), \
                    mock.patch.object(ctl, "ensure_export_modules") as modules:
                with self.assertRaises(ctl.ControllerError):
                    ctl.export_device(
                        ctl.Settings("192.0.2.10", 3240, (), 5, True, 30),
                        ctl.LocalDeviceSpec("3-0", "root hub"),
                    )
            modules.assert_not_called()

    def test_export_restores_interfaces_when_composite_unbind_is_partial(self):
        bindings = [
            (Path("/sys/bus/usb/devices/3-8:1.0"), Path("/sys/bus/usb/drivers/uvcvideo")),
            (Path("/sys/bus/usb/devices/3-8:1.1"), Path("/sys/bus/usb/drivers/uvcvideo")),
        ]
        failure = ctl.ControllerError("simulated ENODEV")
        with mock.patch.object(ctl, "_local_interface_bindings", return_value=bindings), \
                mock.patch.object(ctl, "_write_sysfs", side_effect=[None, failure]) as write, \
                mock.patch.object(ctl, "_restore_local_usb_drivers") as restore:
            with self.assertRaises(ctl.ControllerError):
                ctl._release_local_usb_drivers("3-8")
        self.assertEqual(write.call_count, 2)
        restore.assert_called_once_with("3-8", [bindings[0]])

    def test_unexport_uses_configured_busid(self):
        settings = ctl.Settings("192.0.2.10", 3240, (), 5, True, 30)
        spec = ctl.LocalDeviceSpec("3-9", "local mouse")
        with mock.patch.object(ctl, "find_usbip", return_value="/usr/sbin/usbip"), \
                mock.patch.object(ctl, "run_checked", return_value="ok") as run, \
                mock.patch.object(ctl, "_probe_local_usb_device") as probe:
            self.assertEqual(ctl.unexport_device(settings, spec), "ok")
        run.assert_called_once_with(
            ["/usr/sbin/usbip", "unbind", "--busid", "3-9"], 30
        )
        probe.assert_called_once_with("3-9")

    def test_usbip_constants_match_wire_protocol(self):
        self.assertEqual(ctl.USBIP_VERSION, 0x0111)
        self.assertEqual(ctl.OP_REQ_LINK_INIT, 0x8008)
        self.assertEqual(ctl.OP_REP_LINK_INIT, 0x0008)
        self.assertEqual(ctl.OP_REQ_DEVLIST, 0x8005)
        self.assertEqual(ctl.OP_REP_DEVLIST, 0x0005)
        self.assertEqual(ctl.OP_REQ_IMPORT, 0x8003)
        self.assertEqual(ctl.USBIP_IMPORT_BUSID_SIZE, 32)

    def test_probe_protocol_performs_real_devlist_handshake(self):
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(3)
        request = {}

        def serve_probe():
            connection, _address = listener.accept()
            with connection:
                raw = connection.recv(16)
                request["header"] = raw[:8]
                request["body"] = raw[8:]
                connection.sendall(
                    struct.pack(
                        "!HHII", ctl.USBIP_VERSION, ctl.OP_REP_DEVLIST, 0, 2
                    )
                )

        worker = threading.Thread(target=serve_probe)
        worker.start()
        try:
            settings = ctl.Settings(
                "127.0.0.1", listener.getsockname()[1], (), 5, True, 3
            )
            self.assertEqual(
                ctl.probe_protocol(settings),
                "standard-usbip version=0x0111 devices=2",
            )
        finally:
            worker.join(timeout=3)
            listener.close()
        self.assertEqual(
            request["header"],
            struct.pack("!HHI", ctl.USBIP_VERSION, ctl.OP_REQ_DEVLIST, 0),
        )
        self.assertEqual(request["body"], struct.pack("!I", 0))

    def test_backward_frame_only_accepts_proven_usb_operations(self):
        frame = bytearray(ctl.BACKWARD_FRAME_SIZE)
        frame[:2] = b"\x02\x02"
        struct.pack_into("<I", frame, 8, 1)
        self.assertEqual(ctl.parse_backward_frame(bytes(frame)), 1)
        frame[ctl.BACKWARD_BUSID_OFFSET:ctl.BACKWARD_BUSID_OFFSET + 4] = b"1-2\x00"
        self.assertEqual(ctl.backward_frame_busid(bytes(frame)), "1-2")
        struct.pack_into("<I", frame, 8, 17)
        with self.assertRaises(ctl.ControllerError):
            ctl.parse_backward_frame(bytes(frame))
        frame[0] = 3
        with self.assertRaises(ctl.ControllerError):
            ctl.parse_backward_frame(bytes(frame))

    def test_backward_settings_require_explicit_enable(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "config.ini"
            path.write_text(
                "[backward]\n"
                "enabled = yes\n"
                "listen_host = 127.0.0.1\n"
                "listen_port = 3246\n"
                "proxy_host = 127.0.0.1\n"
                "proxy_port = 3247\n"
                "allowed_ops = 1,2\n",
                encoding="utf-8",
            )
            settings = ctl.load_backward_settings(str(path))
        self.assertEqual(settings.listen_port, 3246)
        self.assertEqual(settings.proxy_port, 3247)
        self.assertEqual(settings.allowed_ops, frozenset({1, 2}))
        self.assertFalse(settings.auto_attach)
        self.assertEqual(settings.attach_timeout, 30)
        self.assertEqual(settings.compression, "off")

    def test_backward_compression_setting_is_explicit(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "config.ini"
            path.write_text(
                "[backward]\n"
                "enabled = yes\n"
                "listen_host = 127.0.0.1\n"
                "listen_port = 3246\n"
                "proxy_host = 127.0.0.1\n"
                "proxy_port = 3247\n"
                "compression = lz4\n",
                encoding="utf-8",
            )
            settings = ctl.load_backward_settings(str(path))
        self.assertEqual(settings.compression, "lz4")

        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "config.ini"
            path.write_text(
                "[backward]\n"
                "enabled = yes\n"
                "compression = auto\n",
                encoding="utf-8",
            )
            with self.assertRaises(ctl.ControllerError):
                ctl.load_backward_settings(str(path))

    def test_backward_codec_type_zero_roundtrip(self):
        payload = b"usbip-header-and-payload"
        frame = ctl.encode_backward_compressed_frame(payload)
        self.assertEqual(
            struct.unpack("!BII", frame[:ctl.BACKWARD_CODEC_HEADER_SIZE]),
            (0, len(payload), len(payload)),
        )
        self.assertEqual(ctl.decode_backward_compressed_frame(frame), payload)

    def test_backward_codec_decodes_lz4_block(self):
        # Three literals (abc), then offset 3 and a six-byte match.
        block = b"\x32abc\x03\x00"
        frame = struct.pack("!BII", 1, 9, len(block)) + block
        self.assertEqual(ctl.decode_backward_compressed_frame(frame), b"abcabcabc")

    def test_backward_codec_rejects_bad_lengths_and_types(self):
        with self.assertRaises(ctl.ControllerError):
            ctl.decode_backward_compressed_frame(struct.pack("!BII", 0, 4, 3) + b"abc")
        with self.assertRaises(ctl.ControllerError):
            ctl.decode_backward_compressed_frame(struct.pack("!BII", 7, 3, 3) + b"abc")

    def test_backward_auto_attach_setting_is_explicit(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "config.ini"
            path.write_text(
                "[backward]\n"
                "enabled = yes\n"
                "listen_host = 127.0.0.1\n"
                "listen_port = 3246\n"
                "proxy_host = 127.0.0.1\n"
                "proxy_port = 3247\n"
                "auto_attach = yes\n",
                encoding="utf-8",
            )
            settings = ctl.load_backward_settings(str(path))
        self.assertTrue(settings.auto_attach)

    def test_attach_to_proxy_does_not_probe_first(self):
        with mock.patch.object(ctl, "ensure_modules"), \
                mock.patch.object(ctl, "find_usbip", return_value="/usr/sbin/usbip"), \
                mock.patch.object(ctl, "run_checked", return_value="ok") as run:
            self.assertEqual(ctl.attach_to_proxy("127.0.0.1", 3247, "1-2", 10), "ok")
        run.assert_called_once_with(
            [
                "/usr/sbin/usbip", "--tcp-port", "3247", "attach",
                "--remote", "127.0.0.1", "--busid", "1-2",
            ],
            10,
        )

    def test_backward_auto_attach_uses_validated_busid(self):
        settings = ctl.BackwardSettings(
            "127.0.0.1", 3246, "127.0.0.1", 3247, 10, 2, frozenset({1, 2}), True, 30
        )
        bridge = ctl.BackwardBridge(settings)
        peer, connection = socket.socketpair()
        frame = bytearray(ctl.BACKWARD_FRAME_SIZE)
        frame[:2] = b"\x02\x02"
        struct.pack_into("<I", frame, 8, 1)
        frame[ctl.BACKWARD_BUSID_OFFSET:ctl.BACKWARD_BUSID_OFFSET + 4] = b"1-2\x00"
        called = threading.Event()

        def fake_attach(*args):
            called.set()
            return "attached"

        with mock.patch.object(ctl, "attach_to_proxy", side_effect=fake_attach) as attach:
            worker = threading.Thread(
                target=bridge._prepare_remote,
                args=(connection, ("test", 0)),
            )
            worker.start()
            peer.sendall(frame)
            worker.join(timeout=3)
            self.assertTrue(called.wait(3))
            attach.assert_called_once_with("127.0.0.1", 3247, "1-2", 30)
        self.assertEqual(bridge.pending.qsize(), 1)
        _pending = bridge.pending.get_nowait()
        ctl._close_socket(_pending.connection)
        peer.close()

    def test_backward_auto_detach_uses_busid_port_lookup(self):
        settings = ctl.BackwardSettings(
            "127.0.0.1", 3246, "127.0.0.1", 3247, 10, 2, frozenset({1, 2}), True, 30
        )
        bridge = ctl.BackwardBridge(settings)
        peer, connection = socket.socketpair()
        frame = bytearray(ctl.BACKWARD_FRAME_SIZE)
        frame[:2] = b"\x02\x02"
        struct.pack_into("<I", frame, 8, 2)
        frame[ctl.BACKWARD_BUSID_OFFSET:ctl.BACKWARD_BUSID_OFFSET + 4] = b"1-2\x00"
        with mock.patch.object(
            ctl, "list_ports", return_value="Port 03: <Port in Use>\n\t1-2 -> storage\n"
        ) as ports, mock.patch.object(ctl, "detach", return_value="detached") as detach:
            worker = threading.Thread(
                target=bridge._prepare_remote,
                args=(connection, ("test", 0)),
            )
            worker.start()
            peer.sendall(frame)
            worker.join(timeout=3)
        ports.assert_called_once()
        detach.assert_called_once_with(mock.ANY, "03")
        peer.close()

    def test_backward_detach_is_not_misrouted_when_auto_attach_disabled(self):
        settings = ctl.BackwardSettings(
            "127.0.0.1", 3246, "127.0.0.1", 3247, 10, 2, frozenset({1, 2}), False, 30
        )
        bridge = ctl.BackwardBridge(settings)
        peer, connection = socket.socketpair()
        frame = bytearray(ctl.BACKWARD_FRAME_SIZE)
        frame[:2] = b"\x02\x02"
        struct.pack_into("<I", frame, 8, 2)
        frame[ctl.BACKWARD_BUSID_OFFSET:ctl.BACKWARD_BUSID_OFFSET + 4] = b"1-2\x00"
        worker = threading.Thread(
            target=bridge._prepare_remote,
            args=(connection, ("test", 0)),
        )
        worker.start()
        peer.sendall(frame)
        worker.join(timeout=3)
        self.assertFalse(worker.is_alive())
        self.assertEqual(bridge.pending.qsize(), 0)
        peer.close()

    def test_local_import_is_matched_by_busid_and_prefix_is_forwarded(self):
        settings = ctl.BackwardSettings(
            "127.0.0.1", 3246, "127.0.0.1", 3247, 10, 2, frozenset({1, 2}), False, 30
        )
        bridge = ctl.BackwardBridge(settings)
        cloud_peer, cloud_connection = socket.socketpair()
        bridge.pending.put(ctl.PendingLink(cloud_connection, 1, "1-2"))
        local_peer, local_connection = socket.socketpair()
        prefix = struct.pack("!HHI", ctl.USBIP_VERSION, ctl.OP_REQ_IMPORT, 0)
        prefix += b"1-2\x00" + (b"\x00" * 28)
        worker = threading.Thread(
            target=bridge._prepare_local,
            args=(local_connection, ("local", 0)),
        )
        worker.start()
        local_peer.sendall(prefix)
        self.assertEqual(cloud_peer.recv(len(prefix)), prefix)
        local_peer.sendall(b"usbip-data")
        self.assertEqual(cloud_peer.recv(64), b"usbip-data")
        cloud_peer.sendall(b"usbip-reply")
        self.assertEqual(local_peer.recv(64), b"usbip-reply")
        local_peer.close()
        cloud_peer.close()
        worker.join(timeout=3)
        self.assertFalse(worker.is_alive())

    def test_backward_bridge_network_integration(self):
        def free_port():
            probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            try:
                probe.bind(("127.0.0.1", 0))
                return probe.getsockname()[1]
            finally:
                probe.close()

        remote_port = free_port()
        proxy_port = free_port()
        settings = ctl.BackwardSettings(
            "127.0.0.1", remote_port, "127.0.0.1", proxy_port,
            3, 4, frozenset({1, 2}), False, 3,
        )
        bridge = ctl.BackwardBridge(settings)
        stop = threading.Event()
        server = threading.Thread(target=bridge.serve, args=(stop,), daemon=True)
        cloud = local = None
        server.start()
        try:
            deadline = time.monotonic() + 3
            while True:
                try:
                    cloud = socket.create_connection(("127.0.0.1", remote_port), 0.2)
                    break
                except OSError:
                    if time.monotonic() >= deadline:
                        raise
                    time.sleep(0.02)

            frame = bytearray(ctl.BACKWARD_FRAME_SIZE)
            frame[:2] = b"\x02\x02"
            struct.pack_into("<I", frame, 8, 1)
            frame[ctl.BACKWARD_BUSID_OFFSET:ctl.BACKWARD_BUSID_OFFSET + 4] = b"1-2\x00"
            cloud.sendall(frame)
            local = socket.create_connection(("127.0.0.1", proxy_port), 3)
            prefix = struct.pack("!HHI", ctl.USBIP_VERSION, ctl.OP_REQ_IMPORT, 0)
            prefix += b"1-2\x00" + (b"\x00" * 28)
            local.sendall(prefix)
            self.assertEqual(ctl._recv_exact(cloud, len(prefix)), prefix)
            cloud.sendall(b"opaque-usbip-data")
            self.assertEqual(ctl._recv_exact(local, 17), b"opaque-usbip-data")
            local.sendall(b"opaque-reply")
            self.assertEqual(ctl._recv_exact(cloud, 12), b"opaque-reply")
        finally:
            if local is not None:
                local.close()
            if cloud is not None:
                cloud.close()
            stop.set()
            server.join(timeout=3)
        self.assertFalse(server.is_alive())

    def test_relay_forwards_opaque_bytes(self):
        left, right = socket.socketpair()
        thread = threading.Thread(target=ctl._relay_sockets, args=(left, right))
        thread.start()
        try:
            left.sendall(b"usbip-wire")
            self.assertEqual(right.recv(64), b"usbip-wire")
            right.sendall(b"reverse")
            self.assertEqual(left.recv(64), b"reverse")
        finally:
            left.close()
            right.close()
            thread.join(timeout=3)
        self.assertFalse(thread.is_alive())

    def test_relay_translates_explicit_compression_mode(self):
        left_peer, left = socket.socketpair()
        right, right_peer = socket.socketpair()
        thread = threading.Thread(
            target=ctl._relay_sockets,
            args=(left, right),
            kwargs={"compression": "lz4"},
        )
        thread.start()
        try:
            left_peer.sendall(b"usbip-wire")
            header = ctl._recv_exact(right_peer, ctl.BACKWARD_CODEC_HEADER_SIZE)
            _, _, compressed_size = struct.unpack("!BII", header)
            frame = header + ctl._recv_exact(right_peer, compressed_size)
            self.assertEqual(ctl.decode_backward_compressed_frame(frame), b"usbip-wire")
            right_peer.sendall(ctl.encode_backward_compressed_frame(b"reverse"))
            self.assertEqual(left_peer.recv(64), b"reverse")
        finally:
            left_peer.close()
            right_peer.close()
            left.close()
            right.close()
            thread.join(timeout=3)
        self.assertFalse(thread.is_alive())

    def test_probe_validates_standard_devlist_handshake(self):
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.bind(("127.0.0.1", 0))
        server.listen(1)
        port = server.getsockname()[1]
        received = []

        def serve():
            conn, _ = server.accept()
            with conn:
                received.append(conn.recv(12))
                conn.sendall(struct.pack("!HHII", 0x0111, 0x0005, 0, 2))

        thread = threading.Thread(target=serve)
        thread.start()
        try:
            settings = ctl.Settings("127.0.0.1", port, (), 5, True, 3)
            self.assertEqual(ctl.probe_protocol(settings), "standard-usbip version=0x0111 devices=2")
        finally:
            thread.join(timeout=3)
            server.close()
        self.assertEqual(received, [struct.pack("!HHII", 0x0111, 0x8005, 0, 0)])

    def test_probe_link_validates_vendor_init_before_devlist(self):
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.bind(("127.0.0.1", 0))
        server.listen(1)
        port = server.getsockname()[1]
        received = []

        def serve():
            conn, _ = server.accept()
            with conn:
                received.append(ctl._recv_exact(conn, 8))
                conn.sendall(struct.pack("!HHI", 0x0111, 0x0008, 0))
                conn.sendall(struct.pack("!HHI", 0x0111, 0x8005, 0))
                received.append(ctl._recv_exact(conn, 8))
                conn.sendall(struct.pack("!I", 3))

        thread = threading.Thread(target=serve)
        thread.start()
        try:
            settings = ctl.Settings("127.0.0.1", port, (), 5, True, 3)
            self.assertEqual(
                ctl.probe_link_protocol(settings),
                "official-usbip-link version=0x0111 devices=3",
            )
        finally:
            thread.join(timeout=3)
            server.close()
        self.assertEqual(
            received,
            [
                struct.pack("!HHI", 0x0111, 0x8008, 0),
                struct.pack("!HHI", 0x0111, 0x0005, 0),
            ],
        )


if __name__ == "__main__":
    unittest.main()
