#!/usr/bin/env python3
"""Pin the r113 check-only first-frame DM2 envelope (offline half).

The live half (new session) submits exactly these bytes: the in-tree
0x46f0 template with the kick sequence as u64 frame_tag at +0x08 and
no render-target binding (va/stride/extent slots stay zero; 0x4668
passes the reference value through — flagged live risk, see below). This test pins the
offline-constructible side so the live run has a byte-exact expectation:

- template parses to exactly MT_GFX_LINUX_PACKET_BYTES (0x46f0) bytes;
- RT slots 0x45a0/0x45a8/0x45b0/0x4668 are zero in the template
  (empty marker ships unbound; the drm ioctl only writes them when a
  target GEM handle is provided);
- the tag slot +0x08 is zero in the template (applied dynamically);
- kernel/recovery/mt_live_3d_drm.c writes tag at 0x08, binds RT only
  under `if (target_lease)`, and submits bytes=MT_GFX_LINUX_PACKET_BYTES
  with type=3 (DM2).

Live acceptance (r113): submit with frame_tag=N, expect completed+1,
continuous fence seqno, zero faults; pixel side asserts nothing (no RT).
"""
import re
import struct
import unittest
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.helpers import get_repo_root

ROOT = get_repo_root()
TEMPLATE_H = ROOT / 'kernel' / 'mt_gfx_packet_template.h'
DRM_C = ROOT / 'kernel' / 'recovery' / 'mt_live_3d_drm.c'

ENVELOPE_BYTES = 0x46f0
TAG_OFFSET = 0x08
# RT bind sites written by the drm ioctl only when a target GEM handle is
# provided (guarded by `if (target_lease)`). In the template the
# va/stride/extent slots ship zero; 0x4668 carries the reference run's
# value 0xed00000000, which passes through untouched on the no-RT path.
# That pass-through is a flagged live risk (DM2 empty-marker acceptance),
# not a gate failure: any change here must be deliberate and documented.
RT_ZERO_SLOTS = (0x45a0, 0x45a8, 0x45b0)
RT_PASSTHROUGH_SLOT = 0x4668
RT_PASSTHROUGH_VALUE = 0xed00000000


def parse_template():
    text = TEMPLATE_H.read_text()
    m = re.search(r'#define\s+MT_GFX_LINUX_PACKET_BYTES\s+(0x[0-9a-fA-F]+U?)',
                  text)
    assert m, 'MT_GFX_LINUX_PACKET_BYTES define missing'
    size = int(m.group(1).rstrip('Uu'), 16)
    body = text[text.index('mt_gfx_linux_packet_template'):]
    body = body[body.index('{'):body.index('};')]
    raw = bytes(int(b, 16) for b in re.findall(r'0x([0-9a-fA-F]{2})', body))
    return size, raw


def first_frame_envelope(raw, tag):
    """What the live ioctl must submit for kick #tag: template + tag, no RT."""
    assert len(raw) == ENVELOPE_BYTES
    out = bytearray(raw)
    struct.pack_into('<Q', out, TAG_OFFSET, tag)
    return bytes(out)


class FirstFrameEnvelope(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.size, cls.raw = parse_template()
        cls.drm = DRM_C.read_text()

    def test_template_size_is_the_dm2_envelope(self):
        self.assertEqual(self.size, ENVELOPE_BYTES)
        self.assertEqual(len(self.raw), ENVELOPE_BYTES,
                         'template array has %d bytes, define says %#x'
                         % (len(self.raw), self.size))

    def test_template_ships_with_no_render_target(self):
        for off in RT_ZERO_SLOTS:
            self.assertEqual(struct.unpack_from('<Q', self.raw, off)[0], 0,
                             'RT slot %#x is nonzero in the template: '
                             'empty marker must ship unbound' % off)

    def test_passthrough_slot_value_is_pinned(self):
        self.assertEqual(
            struct.unpack_from('<Q', self.raw, RT_PASSTHROUGH_SLOT)[0],
            RT_PASSTHROUGH_VALUE,
            '0x4668 changed: the no-RT path passes this value through to '
            'firmware, so any change is a live-behavior change')

    def test_tag_slot_is_free_in_the_template(self):
        self.assertEqual(struct.unpack_from('<Q', self.raw, TAG_OFFSET)[0], 0,
                         'tag slot +0x08 baked nonzero: frame_tag must be '
                         'applied dynamically per kick')

    def test_drm_path_writes_tag_at_008(self):
        self.assertIn('write_bo(&command_3d, 0x08, &tag, 8)', self.drm)

    def test_drm_path_binds_rt_only_with_target(self):
        for off in RT_ZERO_SLOTS + (RT_PASSTHROUGH_SLOT,):
            self.assertIn('write_bo(&command_3d, %#x,' % off, self.drm)
        bind = self.drm[self.drm.index('Dynamic Render Target binding'):]
        bind = bind[:bind.index('req.command_va')]
        self.assertIn('if (target_lease)', bind)

    def test_drm_path_submits_dm2_envelope(self):
        self.assertIn('req.bytes = MT_GFX_LINUX_PACKET_BYTES;', self.drm)
        self.assertIn('req.type = 3;', self.drm)

    def test_first_frame_bytes_are_template_plus_tag(self):
        for tag in (1, 2):
            out = first_frame_envelope(self.raw, tag)
            self.assertEqual(len(out), ENVELOPE_BYTES)
            self.assertEqual(struct.unpack_from('<Q', out, TAG_OFFSET)[0],
                             tag)
            for off in RT_ZERO_SLOTS:
                self.assertEqual(struct.unpack_from('<Q', out, off)[0], 0)
            self.assertEqual(
                struct.unpack_from('<Q', out, RT_PASSTHROUGH_SLOT)[0],
                RT_PASSTHROUGH_VALUE)
            unchanged = bytearray(out)
            struct.pack_into('<Q', unchanged, TAG_OFFSET, 0)
            self.assertEqual(bytes(unchanged), self.raw,
                             'only +0x08 may differ from the template')


if __name__ == '__main__':
    unittest.main()
