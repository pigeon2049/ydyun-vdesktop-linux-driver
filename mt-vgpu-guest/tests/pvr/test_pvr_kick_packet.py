#!/usr/bin/env python3
"""Pin the 0x88:0x4 kick-packet layout against live UMD captures.

The 84-byte IN carries sync bookkeeping only (counts, UMD pointers, fence
FDs) -- no GPU command bytes. The real work hides behind the kicksync
context (CCB + mapped PMRs), so S4-3 kick translation must resolve the
context, not re-encode this packet. Both packets were captured from the
real 5.2.0 UMD (rung8 RGXKickSync, reports/r53) with the shim's
UMD_DUMP_BRIDGE="0x88:0x4" recorder.
"""
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'kernel' / 'mt_pvr_wire.h'

# Two independent runs of the same synthetic kick. Stable fields must agree;
# the fence-name pointer moves with ASLR and hCheckFenceFD is UMD-side
# uninitialized garbage on this synthetic path (a valid fd or -1 in real
# use), so neither value is pinned -- only their shape.
PACKET_A = (
    '2f100000000000000000000000000000000000000000000000000000000000'
    '00000000000000000000000000000000000000000000000000000000000000'
    '00007091aeabfd7f00008088b4d8ffffffff00000000'
)
PACKET_B = (
    '2f100000000000000000000000000000000000000000000000000000000000'
    '00000000000000000000000000000000000000000000000000000000000000'
    '0000b07952dffc7f000080f84fb5ffffffff00000000'
)

EXPECTED_OFFSETS = {
    'kicksync_context': 0,
    'check_devvar_offset': 8,
    'check_value': 16,
    'check_ufo_block': 24,
    'client_check_count': 32,
    'update_devvar_offset': 36,
    'update_value': 44,
    'update_ufo_block': 52,
    'client_update_count': 60,
    'update_fence_name': 64,
    'check_fence_fd': 72,
    'timeline_fence_fd': 76,
    'ext_job_ref': 80,
}


def compiled_offsets():
    """offsetof() each field, so the C struct cannot drift from the map."""
    with tempfile.TemporaryDirectory(prefix='mt-kick-offsets-') as work:
        work = Path(work)
        source = work / 'offsets.c'
        lines = ['#include "mt_pvr_wire.h"', '#include <stddef.h>',
                 '#include <stdio.h>', 'int main(void) {']
        for name in EXPECTED_OFFSETS:
            lines.append(
                '\tprintf("%s %%zu\\n", offsetof(struct mt_pvr_kicksync3_in, %s));'
                % (name, name))
        lines.append('\tprintf("size %zu\\n", sizeof(struct mt_pvr_kicksync3_in));')
        lines.append('\treturn 0;}')
        source.write_text('\n'.join(lines) + '\n')
        binary = work / 'offsets'
        subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-I', str(ROOT / 'kernel'), str(source), '-o',
                        str(binary)], check=True, capture_output=True)
        out = subprocess.run([str(binary)], check=True, capture_output=True,
                             text=True).stdout
    values = dict(line.split() for line in out.splitlines() if line)
    return {k: int(v) for k, v in values.items()}


def decode(packet):
    raw = bytes.fromhex(packet)
    assert len(raw) == 84
    u64 = lambda o: struct.unpack_from('<Q', raw, o)[0]
    u32 = lambda o: struct.unpack_from('<I', raw, o)[0]
    return {
        'kicksync_context': u64(0),
        'check_devvar_offset': u64(8),
        'check_value': u64(16),
        'check_ufo_block': u64(24),
        'client_check_count': u32(32),
        'update_devvar_offset': u64(36),
        'update_value': u64(44),
        'update_ufo_block': u64(52),
        'client_update_count': u32(60),
        'update_fence_name': u64(64),
        'check_fence_fd': u32(72),
        'timeline_fence_fd': u32(76),
        'ext_job_ref': u32(80),
    }


# 0x88:0x0 create on the same synthetic path: all-zero IN
# {hPrivData u64, ui32ContextFlags u32, ui32PackedCCBSizeU88 u32},
# OUT {hKickSyncContext u64, eError u32} = 0x102f, 0.
CREATE_IN = '00000000000000000000000000000000'
CREATE_OUT = '2f1000000000000000000000'


class KickPacket(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.offsets = compiled_offsets()

    def test_struct_offsets_match_the_52_header_map(self):
        for name, offset in EXPECTED_OFFSETS.items():
            self.assertEqual(self.offsets[name], offset,
                             'mt_pvr_kicksync3_in.%s moved to %d, map says %d'
                             % (name, self.offsets[name], offset))
        self.assertEqual(self.offsets['size'], 84)

    def check_synthetic_kick_carries_no_work(self, packet):
        fields = decode(packet)
        # The context handle the bridge minted (0x102f on these runs).
        self.assertEqual(fields['kicksync_context'], 0x102f)
        # No check/update arrays: NULL pointers with zero counts.
        self.assertEqual(fields['client_check_count'], 0)
        self.assertEqual(fields['client_update_count'], 0)
        for name in ('check_devvar_offset', 'check_value',
                     'check_ufo_block', 'update_devvar_offset',
                     'update_value', 'update_ufo_block'):
            self.assertEqual(fields[name], 0,
                             '%s must be NULL when its count is 0' % name)
        # Fence name is a live canonical-low-half userspace pointer.
        self.assertGreater(fields['update_fence_name'], 0)
        self.assertEqual(fields['update_fence_name'] >> 48, 0)
        self.assertEqual(fields['timeline_fence_fd'], 0xffffffff)
        self.assertEqual(fields['ext_job_ref'], 0)
        # hCheckFenceFD is deliberately NOT pinned: on this synthetic
        # path the UMD leaves it uninitialized (0xd8b48880 and 0xb54ff880
        # on the two runs). Real kicks carry a valid fd or -1.

    def test_both_captures_carry_no_work(self):
        self.check_synthetic_kick_carries_no_work(PACKET_A)
        self.check_synthetic_kick_carries_no_work(PACKET_B)

    def test_stable_fields_agree_across_runs(self):
        a, b = decode(PACKET_A), decode(PACKET_B)
        for name in EXPECTED_OFFSETS:
            if name in ('update_fence_name', 'check_fence_fd'):
                continue
            self.assertEqual(a[name], b[name],
                             '%s differs between runs: %#x vs %#x'
                             % (name, a[name], b[name]))

    def test_create_carries_zero_ccb_on_synthetic_path(self):
        raw = bytes.fromhex(CREATE_IN)
        out = bytes.fromhex(CREATE_OUT)
        self.assertEqual(len(raw), 16)
        self.assertEqual(raw, bytes(16))
        self.assertEqual(int.from_bytes(out[:8], 'little'), 0x102f)
        self.assertEqual(int.from_bytes(out[8:], 'little'), 0)

    def test_header_declares_the_handle_first(self):
        text = HEADER.read_text()
        body = text[text.index('struct MT_PVR_PACKED mt_pvr_kicksync3_in {'):
                    text.index('};', text.index('mt_pvr_kicksync3_in'))]
        self.assertLess(body.index('kicksync_context'),
                        body.index('client_check_count'))
        self.assertIn('sizeof(struct mt_pvr_kicksync3_in) == 84', text)


class KickInspect(unittest.TestCase):
    """The bridge's inspect-only path (translator T1+T2).

    Parses via mt_pvr_kicksync3_in (no raw offset reads), caps array counts,
    resolves UFO handles against both PMRs and objects, and never propagates
    a failure: the fence + OUT path below is unconditional, so the wire
    result is identical whether inspection succeeds or degrades.
    """
    BRIDGE = (Path(__file__).resolve().parents[2] / 'kernel' / 'recovery' /
              'mt_pvr_bridge.c')

    @classmethod
    def setUpClass(cls):
        cls.text = cls.BRIDGE.read_text()

    def test_handle_comes_from_the_struct(self):
        self.assertIn('struct mt_pvr_kicksync3_in in3;', self.text)
        self.assertIn('handle = in3.kicksync_context;', self.text)
        self.assertNotIn('*(u64 *)in84', self.text)

    def test_counts_are_capped(self):
        self.assertIn('MT_PVR_KICK_SYNC_MAX', self.text)
        self.assertIn('ncheck > MT_PVR_KICK_SYNC_MAX', self.text)
        self.assertIn('nupdate > MT_PVR_KICK_SYNC_MAX', self.text)

    def test_ufo_resolves_against_pmrs_and_objects(self):
        resolve = self.text[self.text.index(
            'static int pvr_kick_ufo_known'):]
        resolve = resolve[:resolve.index('\n}\n')]
        self.assertIn('file->pmrs', resolve)
        self.assertIn('file->objects', resolve)

    def test_failures_degrade_never_propagate(self):
        inspect = self.text[self.text.index(
            'static void pvr_kick_inspect'):]
        inspect = inspect[:inspect.index('\n}\n')]
        self.assertNotIn('return -', inspect)
        self.assertIn('kfree(check_ufo)', inspect)
        self.assertIn('kfree(update_ufo)', inspect)


if __name__ == '__main__':
    unittest.main()
