"""The DDK2 TDM context token is file-owned; TDM submission stays closed."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BRIDGE = ROOT / 'kernel' / 'recovery' / 'mt_pvr_bridge.c'
WIRE = ROOT / 'kernel' / 'mt_pvr_wire.h'


def compiled_sizes():
    source_text = r'''#include "mt_pvr_wire.h"
#include <stddef.h>
#include <stdio.h>
int main(void) {
    printf("create_in %zu\n", sizeof(struct mt_pvr_tdm_context2_create_in));
    printf("create_out %zu\n", sizeof(struct mt_pvr_tdm_context2_create_out));
    printf("destroy_in %zu\n", sizeof(struct mt_pvr_tdm_context2_destroy_in));
    printf("destroy_out %zu\n", sizeof(struct mt_pvr_tdm_context2_destroy_out));
    printf("in_type %zu\n", offsetof(struct mt_pvr_tdm_context2_create_in, context_type));
    printf("out_error %zu\n", offsetof(struct mt_pvr_tdm_context2_create_out, error));
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='mt-tdm-context2-') as tmp:
        tmp = Path(tmp)
        source = tmp / 'layout.c'
        binary = tmp / 'layout'
        source.write_text(source_text)
        subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-I', str(ROOT / 'kernel'), str(source), '-o',
                        str(binary)], check=True, capture_output=True)
        output = subprocess.run([str(binary)], check=True, capture_output=True,
                                text=True).stdout
    return {key: int(value) for key, value in
            (line.split() for line in output.splitlines())}


class TdmContext2Wire(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sizes = compiled_sizes()

    def test_bridge_wire_sizes_match_umd_wrapper(self):
        self.assertEqual(self.sizes, {
            'create_in': 12, 'create_out': 12,
            'destroy_in': 8, 'destroy_out': 4,
            'in_type': 8, 'out_error': 8,
        })


class TdmContext2Lifecycle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = BRIDGE.read_text()

    def test_create_mints_typed_file_owned_token(self):
        create = self.source[self.source.index(
            'static int pvr_cmd_tdm_context2_create'):]
        create = create[:create.index('\n}\n')]
        self.assertIn('pvr_in(cmd, &in, sizeof(in))', create)
        self.assertIn('pvr_object_new(file, MT_PVR_KIND_TDM_CONTEXT)', create)
        self.assertIn('out.transfer_context = obj->handle;', create)

    def test_destroy_retires_only_tdm_context_token(self):
        destroy = self.source[self.source.index(
            'static int pvr_cmd_tdm_context2_destroy'):]
        destroy = destroy[:destroy.index('\n}\n')]
        self.assertIn('pvr_object_find(file, in.transfer_context, '
                      'MT_PVR_KIND_TDM_CONTEXT)', destroy)
        self.assertIn('list_del(&obj->link);', destroy)
        self.assertIn('kfree(obj);', destroy)
        self.assertIn('return -ENOENT;', destroy)

    def test_dispatch_routes_submit_transfer_to_observer(self):
        # r150 asserted no case 0xa (submission unsupported). r174 connected
        # 0x89:0xa to the accept-and-log observer, so the contract is now
        # "routed to observe", not "absent". See test_pvr_tdm_submit3.py
        # for the observer's bounds (bounded scan, ctx validated, nested
        # arrays untouched, no execution path).
        tdm = self.source[self.source.index('case MT_PVR_BRIDGE_RGXTDM:'):]
        tdm = tdm[:tdm.index('\n\t\tdefault:')]
        self.assertIn('case MT_PVR_FN_RGXTDMCREATETRANSFERCONTEXT2:', tdm)
        self.assertIn('case MT_PVR_FN_RGXTDMDESTROYTRANSFERCONTEXT2:', tdm)
        self.assertRegex(tdm, r'case MT_PVR_FN_RGXTDMSUBMITTRANSFER3:[\s\S]*?pvr_cmd_tdm_submit3_observe')


if __name__ == '__main__':
    unittest.main()
