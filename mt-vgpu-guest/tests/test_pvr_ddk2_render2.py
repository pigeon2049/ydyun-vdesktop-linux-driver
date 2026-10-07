#!/usr/bin/env python3
"""Gate the DDK2 render-context commands (r141/r142).

Under drm_major=2 the vendor UMD issues BridgeRGXCreateRenderContext2
(0x82:0x12, 12-byte IN, 4-byte OUT) and BridgeRGXDestroyRenderContext2
(0x82:0x13, 8-byte IN, 4-byte OUT); both fell through to -ENOTTY and the
render path died with the stub constant 37. The create mints a KIND_CONTEXT
object like legacy 0x82:0x8 but answers the 4-byte OUT the stub consumes;
the destroy shares the widened-handle release path.
"""
import re
import unittest
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[1] / 'kernel/recovery/mt_pvr_bridge.c'


def code():
    text = SOURCE.read_text()
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def ta3d_block(src):
    m = re.search(r'case MT_PVR_BRIDGE_RGXTA3D:(.*?)case MT_PVR_BRIDGE_\w+:',
                  src, re.S)
    assert m, 'RGXTA3D dispatch block not found'
    return m.group(1)


class Ddk2RenderContext(unittest.TestCase):
    def test_create_routed(self):
        block = ta3d_block(code())
        self.assertRegex(block, r'case MT_PVR_FN_RGXCREATERENDERCONTEXT2:[\s\S]*?pvr_cmd_render2_create',
                         '0x82:0x12 must reach the DDK2 create handler')

    def test_destroy_routed(self):
        block = ta3d_block(code())
        self.assertRegex(block,
                         r'case MT_PVR_FN_RGXDESTROYRENDERCONTEXT2:[\s\S]*?pvr_cmd_handle_release\(file, cmd,\s*'
                         r'MT_PVR_KIND_CONTEXT\)',
                         '0x82:0x13 must share the context release path')

    def test_sync_prim_set_writes(self):
        # r220 re-judges the r142 contract: SyncPrimSet is a real
        # 16-byte write (handle/index/value per the UMD wrapper), not
        # a zeroed-OUT stub. Its 0x2:0x1/0x2:0x7 siblings stay stubbed.
        src = code()
        m = re.search(r'case MT_PVR_BRIDGE_SYNC:(.*?)case MT_PVR_BRIDGE_\w+:',
                      src, re.S)
        self.assertIsNotNone(m, 'SYNC dispatch block not found')
        self.assertRegex(m.group(1),
                         r'case MT_PVR_FN_SYNCPRIMSET:[\s\S]*?pvr_cmd_syncprim_set',
                         '0x2:0x2 (SyncPrimSet) must reach the write handler')

    def test_sync_free_event_stubbed(self):
        src = code()
        m = re.search(r'case MT_PVR_BRIDGE_SYNC:(.*?)case MT_PVR_BRIDGE_\w+:',
                      src, re.S)
        self.assertIsNotNone(m, 'SYNC dispatch block not found')
        self.assertRegex(m.group(1),
                         r'case MT_PVR_FN_SYNCFREEEVENT:[\s\S]*?pvr_stub_ok',
                         '0x2:0x8 (SyncFreeEvent, r144) must answer zeroed OUT; '
                         'the UMD ignores the value on the destroy path')

    def test_create_validates_12byte_in_and_answers_4byte_out(self):
        src = code()
        fn = re.search(r'static int pvr_cmd_render2_create\(.*?^}',
                       src, re.S | re.M)
        self.assertIsNotNone(fn, 'pvr_cmd_render2_create not found')
        body = fn.group(0)
        self.assertIn('mt_pvr_render2_create_in', body)
        self.assertIn('mt_pvr_render2_create_out', body)
        self.assertIn('MT_PVR_KIND_CONTEXT', body)
        self.assertNotIn('mt_pvr_handle_out', body,
                         '12-byte legacy OUT would be refused (out_size=4)')


if __name__ == '__main__':
    unittest.main()
