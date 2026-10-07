#!/usr/bin/env python3
"""Gate the update-writeback probe tool (r223).

pvr_update_writeback must fire an update-only 0x88:0x4 (one entry,
real sync PMR, count 1) and require ret 0, then probe the same slot
with a check entry and require ret 0 PROMPTLY (<4s): timing is the
readback for the translator's post-fence writeback (r159 order). It
must tear the kick context down afterwards.
"""
import re
import unittest
from pathlib import Path

TOOL = Path(__file__).resolve().parents[1] / 'probe' / 'pvr_update_writeback.c'


class UpdateWritebackTool(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.src = TOOL.read_text()

    def test_update_fire_wired(self):
        self.assertRegex(self.src,
                         r'kick_in\.client_update_count = 1;')
        self.assertIn('kick_in.update_ufo_block', self.src)
        self.assertIn('UPDATE_VAL', self.src)

    def test_mixed_fire_wired(self):
        self.assertIn('client_check_count = 2;', self.src)
        self.assertIn('CHECK_VAL2', self.src)
        self.assertIn('check slot 1 preset', self.src)
        self.assertIn('check slot 2 preset', self.src)
        self.assertRegex(self.src, r'bridge_call\(fd, 0x2, 0xa,')
        m = re.search(r'mixed fire accepted[\s\S]{0,400}'
                      r'!kick_out\.error', self.src)
        self.assertIsNotNone(m, 'mixed fire must require ret 0')

    def test_check_probe_timed(self):
        self.assertIn('client_check_count = 1;', self.src)
        m = re.search(r'check probe prompt \(writeback landed\)[\s\S]{0,300}'
                      r'dt < PROMPT_LIMIT_NS', self.src)
        self.assertIsNotNone(m, 'check probe must require prompt return')

    def test_tears_context_down(self):
        self.assertRegex(self.src, r'bridge_call\(fd, 0x88, 0x1,')

    def test_uses_wire_structs(self):
        self.assertIn('struct mt_pvr_kicksync3_in kick_in', self.src)
        self.assertIn('sizeof(kick_in)', self.src)


if __name__ == '__main__':
    unittest.main()
