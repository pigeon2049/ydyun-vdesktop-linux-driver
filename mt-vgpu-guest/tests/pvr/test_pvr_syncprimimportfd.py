#!/usr/bin/env python3
"""Gate the SyncPrimImportFD handler (r386).

0x2:0xc must reach pvr_cmd_syncprim_importfd (not the -ENOTTY default):
IN 24B {u32 fd, u64 hSyncBlock, u32 offset, u64 hDevmemCtx} per the
hash-verified KMD 5.2.0 generated header MTGPU_BRIDGE_IN_SYNCPRIMIMPORTFD
(reference/kmd-5.2.0-server-generated/common_sync_bridge.h:243),
confirmed against the UMD ZeusSyncPrimImportFD wrapper FUN_00139990
(decompiled.c:12418). OUT 12B {u64 value, u32 error}.

The handler resolves hSyncBlock like the translator wait path, validates
the FD with fdget (TO-VALIDATE: payload not interpreted), and returns the
current u32 value at the offset. No fence, no submission, no wakeup.
"""
import re
import unittest
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.helpers import get_repo_root

SOURCE = get_repo_root() / 'kernel/recovery/mt_pvr_bridge.c'
WIRE = get_repo_root() / 'kernel/mt_pvr_wire.h'


def code():
    text = SOURCE.read_text()
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def fn_body(src, name):
    m = re.search(r'static (?:int|void) %s\([^;]*\)\s*\{(.*?)^}' % re.escape(name),
                  src, re.S | re.M)
    assert m, '%s definition not found' % name
    return m.group(0)


class SyncPrimImportFd(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.src = code()
        cls.dispatch = fn_body(cls.src, 'pvr_dispatch_sync')
        cls.body = fn_body(cls.src, 'pvr_cmd_syncprim_importfd')
        cls.wire = WIRE.read_text()

    def test_importfd_routed(self):
        self.assertRegex(self.dispatch,
                         r'case MT_PVR_FN_SYNCPRIMIMPORTFD:[\s\S]*?pvr_cmd_syncprim_importfd',
                         '0x2:0xc must reach the import handler, not -ENOTTY')

    def test_fn_constant_defined(self):
        self.assertIn('#define MT_PVR_FN_SYNCPRIMIMPORTFD 0xcU', self.wire)

    def test_wire_layout_matches_kmd_header(self):
        # IN 24B: u32 fd, u64 hSyncBlock, u32 offset, u64 hDevmemCtx
        self.assertIn('mt_pvr_syncprimimportfd_in', self.body)
        self.assertIn('mt_pvr_syncprimimportfd_out', self.body)
        for field in ('in.fd', 'in.sync_block', 'in.offset', 'in.devmem_ctx'):
            self.assertIn(field, self.body, 'IN field %s missing' % field)
        self.assertIn('out.value', self.body)
        self.assertIn('out.error', self.body)

    def test_resolves_like_translator_wait(self):
        self.assertIn('pvr_translator_resolve(', self.body)

    def test_fd_validated(self):
        self.assertIn('fdget(', self.body)
        self.assertIn('fd_empty(', self.body)
        self.assertIn('-EBADF', self.body)
        self.assertIn('fdput(', self.body)

    def test_returns_current_value(self):
        self.assertIn('memcpy(', self.body)
        self.assertIn('cond.host', self.body)
        self.assertIn('pvr_out(cmd, &out, sizeof(out))', self.body)

    def test_no_execution_or_wakeup(self):
        for token in ('dma_submit', 'mt_fw_event', 'mt_system_submit',
                      'doorbell', 'submit_tqx_work', 'submit_context',
                      'dma_fence', 'wake_up', 'complete('):
            self.assertNotIn(token, self.body,
                             'import path must not reach %s' % token)

    def test_to_validate_marked(self):
        self.assertIn('TO-VALIDATE', self.body)


if __name__ == '__main__':
    unittest.main()
