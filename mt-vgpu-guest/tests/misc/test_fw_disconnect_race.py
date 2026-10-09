"""Replay the observed offline/completion race without accessing hardware."""
import ctypes
import errno
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
spec = importlib.util.spec_from_file_location('connection_model', ROOT / 'scripts/verify-fw-connection.py')
model = importlib.util.module_from_spec(spec)
spec.loader.exec_module(model)


class LateCompletion(model.Model):
    def __init__(self, final_idle=1, final_state=1, started=0, **kwargs):
        super().__init__('disconnect', **kwargs)
        self.final_idle, self.final_state, self.still_started = final_idle, final_state, started
        self.controls = 0

    def started(self, _=None):
        return self.still_started

    def firmware_state(self, _=None):
        state = 1 if self.controls == 1 else self.final_state
        self.events.append(('read-fw', state))
        return state

    def control(self, _=None):
        self.controls += 1
        value = 0 if self.controls == 1 else self.final_idle
        self.events.append(('control-idle', value))
        return value


class DisconnectRaceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        so = Path(cls.temp.name) / 'connection.so'
        subprocess.run(['cc', '-shared', '-fPIC', '-Wall', '-Wextra', '-Werror',
                        str(ROOT / 'tests/c/fw_connection_wrapper.c'), '-o', str(so)], check=True)
        cls.lib = ctypes.CDLL(str(so))
        cls.lib.disconnect_fw.argtypes = [ctypes.POINTER(model.Ops), ctypes.c_void_p]

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def invoke(self, timeline):
        ops = timeline.ops()
        return self.lib.disconnect_fw(ctypes.byref(ops), None)

    def test_reference_exits_before_late_event_drain(self):
        original = LateCompletion()
        self.assertFalse(original.reference())
        self.assertNotIn(('guest', 0), original.events)
        candidate = LateCompletion()
        self.assertEqual(self.invoke(candidate), 0)
        self.assertEqual(candidate.controls, 2)
        self.assertEqual([e for e in candidate.events if e[0] == 'send'], [('send', 0x47, 0)])
        self.assertEqual(candidate.events[-1], ('guest', 0))

    def test_incomplete_or_unstable_completion_keeps_guest_state(self):
        for args in ({'final_idle': 0}, {'final_idle': -errno.EIO},
                     {'final_state': 2}, {'final_state': 4}, {'started': 1},
                     {'send_error': -errno.EAGAIN}, {'normal': 0}, {'work': 10}):
            with self.subTest(args=args):
                timeline = LateCompletion(**args)
                self.assertLess(self.invoke(timeline), 0)
                self.assertNotIn(('guest', 0), timeline.events)

    def test_retained_hardware_snapshot_has_consumed_disconnect_and_pending_event(self):
        path = ROOT / 'build/fresh-trials/20260929T144220Z-eb28ef82/trial_firmware'
        raw = path.read_bytes()
        word = lambda off: struct.unpack_from('<I', raw, off)[0]
        base, cursor = 0x6ddd0, 0x6ddd0 + 0x2e00
        self.assertEqual(word(4), 0)
        self.assertEqual((word(base + 0xc), word(base + 0x50 + 0xc)), (0x46, 0x47))
        self.assertEqual((word(cursor), word(cursor + 8)), (2, 2))
        self.assertEqual((word(cursor + 32), word(cursor + 40)), (2, 1))
        # The event is a null-context completion, matching the bounded trial.
        event = struct.unpack_from('<6I', raw, base + 0x2800 + 24)
        self.assertEqual(event, (0, 0, 0, 0, 0, 0))


if __name__ == '__main__':
    unittest.main()
