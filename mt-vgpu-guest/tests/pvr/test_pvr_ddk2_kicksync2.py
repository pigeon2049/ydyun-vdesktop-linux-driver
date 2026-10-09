#!/usr/bin/env python3
"""Gate the DDK2 kick-sync-context commands (r141/r143).

Under drm_major=2 the vendor UMD issues BridgeRGXCreateKickSyncContext2
(0x88:0x5, 8-byte IN, 12-byte OUT) and BridgeRGXDestroyKickSyncContext2
(0x88:0x6, 8-byte IN, 4-byte OUT); the missing create made
RGXCreateKickSyncContextCCB die and the UMD segfault on the absent state.
The create mints a KIND_KICKSYNC object like legacy 0x88:0x0; the destroy
shares the legacy release path.
"""
import re
import unittest
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.helpers import get_repo_root

SOURCE = get_repo_root() / 'kernel/recovery/mt_pvr_bridge.c'



def fn_body(src, name):
    import re
    m = re.search(r'static (?:int|void) %s\([^;]*\)\s*\{(.*?)^}' % re.escape(name),
                  src, re.S | re.M)
    assert m, '%s definition not found' % name
    return m.group(0)

def code():
    text = SOURCE.read_text()
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def kicksync_block(src):
    return fn_body(src, 'pvr_dispatch_rgxkicksync')


class Ddk2KickSyncContext(unittest.TestCase):
    def test_create_routed(self):
        block = kicksync_block(code())
        self.assertRegex(block, r'case MT_PVR_FN_RGXCREATEKICKSYNCCONTEXT2:[\s\S]*?pvr_cmd_kicksyncctx2_create',
                         '0x88:0x5 must reach the DDK2 create handler')

    def test_destroy_routed(self):
        block = kicksync_block(code())
        self.assertRegex(block,
                         r'case MT_PVR_FN_RGXDESTROYKICKSYNCCONTEXT2:[\s\S]*?pvr_cmd_kicksync_destroy\(file, cmd\)',
                         '0x88:0x6 must share the kicksync release path')

    def test_create_mints_kicksync_and_answers_12byte_out(self):
        src = code()
        fn = re.search(r'static int pvr_cmd_kicksyncctx2_create\(.*?^}',
                       src, re.S | re.M)
        self.assertIsNotNone(fn, 'pvr_cmd_kicksyncctx2_create not found')
        body = fn.group(0)
        self.assertIn('mt_pvr_kicksyncctx2_create_in', body)
        self.assertIn('mt_pvr_kicksyncctx2_create_out', body)
        self.assertIn('MT_PVR_KIND_KICKSYNC', body)


if __name__ == '__main__':
    unittest.main()
