"""0x89 TDM shared-memory gate (2D path, r87/r88/r150).

0x89:0x5 RGXTDMGetSharedMemory takes no input and returns two u64s plus a
trailing eError (20 bytes); 0x89:0x6 RGXTDMReleaseSharedMemory takes one
handle and returns eError (4 bytes). The bridge returns separate CLI and USC
PMRs, with one local import reference and release for each.
"""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.helpers import get_repo_root

ROOT = get_repo_root()
BRIDGE = ROOT / 'kernel' / 'recovery' / 'mt_pvr_bridge.c'

EXPECTED_OFFSETS = {
    'ptr1': 0,
    'ptr2': 8,
    'error': 16,
}


def compiled_offsets():
    """offsetof() each field, so the C struct cannot drift from the map."""
    with tempfile.TemporaryDirectory(prefix='mt-tdm-offsets-') as work:
        work = Path(work)
        source = work / 'offsets.c'
        lines = ['#include "mt_pvr_wire.h"', '#include <stddef.h>',
                 '#include <stdio.h>', 'int main(void) {']
        for name in EXPECTED_OFFSETS:
            lines.append(
                '\tprintf("%s %%zu\\n", offsetof(struct mt_pvr_tdm_shmem_out, %s));'
                % (name, name))
        lines.append('\tprintf("size %zu\\n", sizeof(struct mt_pvr_tdm_shmem_out));')
        lines.append(
            '\tprintf("rel_in %zu\\n", sizeof(struct mt_pvr_tdm_release_in));')
        lines.append(
            '\tprintf("rel_out %zu\\n", sizeof(struct mt_pvr_tdm_release_out));')
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


class TdmShmemLayout(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.off = compiled_offsets()

    def test_field_offsets(self):
        for name, want in EXPECTED_OFFSETS.items():
            self.assertEqual(self.off[name], want,
                             'mt_pvr_tdm_shmem_out.%s moved to %d, map says %d'
                             % (name, self.off[name], want))

    def test_wire_sizes(self):
        self.assertEqual(self.off['size'], 20)
        self.assertEqual(self.off['rel_in'], 8)
        self.assertEqual(self.off['rel_out'], 4)


class TdmShmemBridge(unittest.TestCase):
    """The bridge must back CLI and USC slots with separate PMRs."""

    @classmethod
    def setUpClass(cls):
        cls.text = BRIDGE.read_text()

    def test_dispatch_has_tdm_group(self):
        self.assertIn('MT_PVR_BRIDGE_RGXTDM', self.text)
        self.assertIn('case MT_PVR_FN_RGXTDMGETSHAREDMEMORY:', self.text)
        self.assertIn('pvr_cmd_tdm_shmem', self.text)
        self.assertIn('pvr_cmd_tdm_release', self.text)

    def test_backed_by_real_pmr_not_stub(self):
        create = self.text[self.text.index(
            'static int pvr_cmd_tdm_shmem'):]
        create = create[:create.index('\n}\n')]
        self.assertIn('pvr_pmr_new(file, MT_PVR_TDM_SHMEM_BYTES, 12)', create)
        self.assertNotIn('pvr_stub_ok', create)

    def test_aliases_have_distinct_pmr_lifetimes(self):
        create = self.text[self.text.index(
            'static int pvr_cmd_tdm_shmem'):]
        create = create[:create.index('\n}\n')]
        self.assertIn('out.ptr1 = cli_pmr->handle;', create)
        self.assertIn('out.ptr2 = usc_pmr->handle;', create)
        self.assertGreaterEqual(create.count('pvr_pmr_new(file, MT_PVR_TDM_SHMEM_BYTES, 12)'), 2)
        release = self.text[self.text.index(
            'static int pvr_cmd_tdm_release'):]
        release = release[:release.index('\n}\n')]
        self.assertIn('pvr_pmr_put(file, in.handle)', release)


if __name__ == '__main__':
    unittest.main()
