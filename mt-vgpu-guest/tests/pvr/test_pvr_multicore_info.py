"""GetMultiCoreInfo must report the observed one-core Rogue topology."""
import unittest
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.helpers import get_repo_root


def fn_body(src, name):
    import re
    pat = 'static (?:int|void) ' + re.escape(name) + r'\([^;]*\)\s*\{(.*?)^}'
    m = re.search(pat, src, re.S | re.M)
    assert m, '%s definition not found' % name
    return m.group(0)

ROOT = get_repo_root()
BRIDGE = ROOT / 'kernel' / 'recovery' / 'mt_pvr_bridge.c'
SHIM = ROOT / 'probe' / 'umd_bridge_shim.c'


class MulticoreInfoDispatch(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = BRIDGE.read_text()

    def test_response_reports_nonzero_single_core_count(self):
        handler = self.source[self.source.index(
            'static int pvr_cmd_multicore_info'):]
        handler = handler[:handler.index('\n}\n')]
        self.assertIn('pvr_in(cmd, &in, sizeof(in))', handler)
        self.assertIn('out.caps = in.caps;', handler)
        self.assertIn('out.num_cores = 1;', handler)
        self.assertIn('pvr_out(cmd, &out, sizeof(out))', handler)

    def test_srvcore_dispatch_uses_real_handler(self):
        srvcore = fn_body(self.source, 'pvr_dispatch_srvcore')
        self.assertIn('case MT_PVR_FN_GETMULTICOREINFO:', srvcore)
        self.assertIn('return pvr_cmd_multicore_info(cmd);', srvcore)
        self.assertNotIn('case MT_PVR_FN_GETMULTICOREINFO:\t\t\t/* GetMultiCoreInfo */\n\t\t\treturn pvr_stub_ok(cmd);',
                         srvcore)

    def test_fabricated_replay_reports_single_core_and_echoes_caps(self):
        shim = SHIM.read_text()
        handler = shim[shim.index('static void fabricate_multicore_info'):]
        handler = handler[:handler.index('\n}\n')]
        self.assertIn('memcpy(&caps, in, sizeof(caps))', handler)
        self.assertIn('memcpy(out, &caps, sizeof(caps))', handler)
        self.assertIn('memcpy(out + 12, &cores, sizeof(cores))', handler)
        self.assertIn('uint32_t cores = 1;', handler)
        self.assertIn('cmd.bridge_id == 0x1 && cmd.bridge_func_id == 0xc', shim)


if __name__ == '__main__':
    unittest.main()
