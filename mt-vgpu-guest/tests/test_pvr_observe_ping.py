#!/usr/bin/env python3
"""Gate the 0x82:0x14 live routing ping tool (r217).

pvr_observe_ping must send a raw RGXKickTA3D5 packet with a bogus render
context and require -ENOENT (observer reached, auth refused), plus a
control ping to the still-unknown 0x82:0x1f requiring -ENOTTY. Sizes
must come from the wire structs, never literals.
"""
import re
import unittest
from pathlib import Path

TOOL = Path(__file__).resolve().parents[1] / 'probe' / 'pvr_observe_ping.c'


class ObservePingTool(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.src = TOOL.read_text()

    def test_pings_kickta3d5(self):
        self.assertRegex(self.src, r'bridge_call\(fd, 0x82, 0x14,')

    def test_expects_enoent_not_enotty(self):
        m = re.search(r'0x82:0x14 reaches observer \(-ENOENT\)[\s\S]{0,200}'
                      r'errno == ENOENT', self.src)
        self.assertIsNotNone(m, 'ping must require -ENOENT for 0x82:0x14')

    def test_control_still_refused(self):
        self.assertRegex(self.src, r'bridge_call\(fd, 0x82, 0x1f,')
        m = re.search(r'0x82:0x1f still refused \(-ENOTTY\)[\s\S]{0,200}'
                      r'errno == ENOTTY', self.src)
        self.assertIsNotNone(m, 'control must require -ENOTTY for 0x82:0x1f')

    def test_uses_wire_structs(self):
        self.assertIn('struct mt_pvr_rgxkickta3d5_in gfx_in', self.src)
        self.assertIn('sizeof(gfx_in)', self.src)
        self.assertIn('sizeof(gfx_out)', self.src)

    def test_bogus_context_nonzero(self):
        m = re.search(r'#define BOGUS_CONTEXT (0x[0-9a-fA-F]+)', self.src)
        self.assertIsNotNone(m)
        self.assertNotEqual(int(m.group(1), 16), 0,
                            'bogus handle must not be 0 (may alias)')


if __name__ == '__main__':
    unittest.main()
