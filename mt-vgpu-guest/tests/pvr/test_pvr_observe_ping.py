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
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.helpers import get_repo_root

TOOL = get_repo_root() / 'probe' / 'pvr_observe_ping.c'


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

    def test_full_path_builds_legal_envelope(self):
        for frag in ('bridge_call(fd, 0x82, 0x8,',
                     'bridge_call(fd, 0x6, 0x9,',
                     'bridge_call(fd, 0x6, 0x15,',
                     'bridge_call(fd, 0x6, 0x13,'):
            self.assertIn(frag, self.src,
                          'full-path fire needs %s' % frag.strip())

    def test_full_path_expects_accept(self):
        m = re.search(r'0x82:0x14 full-path fire accepted[\s\S]{0,300}'
                      r'!gfx_out\.error', self.src)
        self.assertIsNotNone(m, 'full-path fire must require ret 0')

    def test_full_path_tears_everything_down(self):
        for frag in ('bridge_call(fd, 0x82, 0x9,',
                     'bridge_call(fd, 0x6, 0x14,',
                     'bridge_call(fd, 0x6, 0x16,',
                     'bridge_call(fd, 0x6, 0x7,'):
            self.assertIn(frag, self.src,
                          'teardown needs %s' % frag.strip())

    def test_nonzero_phase_plants_then_refires(self):
        self.assertRegex(self.src, r'bridge_call\(fd, 0x2, 0xa,')
        self.assertIn('0x82:0x14 nonzero-window fire accepted', self.src)
        m = re.search(r'nonzero-window fire accepted[\s\S]{0,300}'
                      r'!gfx_out\.error', self.src)
        self.assertIsNotNone(m, 'nonzero fire must require ret 0')

    def test_ccb_phase_loads_file_then_fires(self):
        self.assertIn('CCB_BYTES_PATH', self.src)
        self.assertRegex(self.src, r'fread\(img, 1, sizeof\(img\), f\)')
        self.assertIn('0x82:0x14 ccb-window fire accepted', self.src)
        m = re.search(r'ccb-window fire accepted[\s\S]{0,300}'
                      r'!gfx_out\.error', self.src)
        self.assertIsNotNone(m, 'ccb fire must require ret 0')
        self.assertIn('submission_size = CCB_WINDOW', self.src)

    def test_negative_bounds_refused(self):
        self.assertIn('oversize window refused (-EINVAL)', self.src)
        self.assertIn('wild index refused (-ERANGE)', self.src)
        m = re.search(r'oversize window refused \(-EINVAL\)[\s\S]{0,400}'
                      r'saved_errno == EINVAL', self.src)
        self.assertIsNotNone(m, 'oversize must require -EINVAL')
        m = re.search(r'wild index refused \(-ERANGE\)[\s\S]{0,400}'
                      r'saved_errno == ERANGE', self.src)
        self.assertIsNotNone(m, 'wild index must require -ERANGE')


if __name__ == '__main__':
    unittest.main()
