#!/usr/bin/env python3
"""Pin every dispatched bridge function ID to its wire value (r188).

The dispatch in pvr_bridge_dispatch compares case labels only; a typo in
a pasted hex literal would silently reroute a command. Values come from
reports/stage-b-bridge-requirements.json (5.2 KMD headers); the single
derived name is RGXTDMSUBMITTRANSFER3 (table says CMD_LAST, following
the SUBMITTRANSFER2 pattern).
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WIRE = ROOT / 'kernel' / 'mt_pvr_wire.h'
BRIDGE = ROOT / 'kernel' / 'recovery' / 'mt_pvr_bridge.c'

PINNED = {
    'MT_PVR_FN_CONNECT': '0x0U',
    'MT_PVR_FN_DISCONNECT': '0x1U',
    'MT_PVR_FN_ACQUIREGLOBALEVENTOBJECT': '0x2U',
    'MT_PVR_FN_RELEASEGLOBALEVENTOBJECT': '0x3U',
    'MT_PVR_FN_EVENTOBJECTOPEN': '0x4U',
    'MT_PVR_FN_EVENTOBJECTWAIT': '0x5U',
    'MT_PVR_FN_EVENTOBJECTCLOSE': '0x6U',
    'MT_PVR_FN_ALIGNMENTCHECK': '0xaU',
    'MT_PVR_FN_EVENTOBJECTWAITTIMEOUT': '0xdU',
    'MT_PVR_FN_GETMULTICOREINFO': '0xcU',
    'MT_PVR_FN_ACQUIREINFOPAGE': '0xfU',
    'MT_PVR_FN_RELEASEINFOPAGE': '0x10U',
    'MT_PVR_FN_ALLOCSYNCPRIMITIVEBLOCK': '0x0U',
    'MT_PVR_FN_FREESYNCPRIMITIVEBLOCK': '0x1U',
    'MT_PVR_FN_SYNCPRIMSET': '0x2U',
    'MT_PVR_FN_SYNCALLOCEVENT': '0x7U',
    'MT_PVR_FN_SYNCFREEEVENT': '0x8U',
    'MT_PVR_FN_PMRMAKELOCALIMPORTHANDLE': '0x3U',
    'MT_PVR_FN_PMRUNMAKELOCALIMPORTHANDLE': '0x4U',
    'MT_PVR_FN_PMRLOCALIMPORTPMR': '0x6U',
    'MT_PVR_FN_PMRUNREFPMR': '0x7U',
    'MT_PVR_FN_DEVMEMINTCTXDESTROY': '0x10U',
    'MT_PVR_FN_DEVMEMINTHEAPDESTROY': '0x12U',
    'MT_PVR_FN_PHYSMEMNEWRAMBACKEDPMR': '0x9U',
    'MT_PVR_FN_DEVMEMINTCTXCREATE': '0xfU',
    'MT_PVR_FN_DEVMEMINTHEAPCREATE': '0x11U',
    'MT_PVR_FN_DEVMEMINTMAPPMR': '0x13U',
    'MT_PVR_FN_DEVMEMINTUNMAPPMR': '0x14U',
    'MT_PVR_FN_DEVMEMINTRESERVERANGE': '0x15U',
    'MT_PVR_FN_DEVMEMINTUNRESERVERANGE': '0x16U',
    'MT_PVR_FN_HEAPCFGHEAPCOUNT': '0x1eU',
    'MT_PVR_FN_HEAPCFGHEAPDETAILS': '0x20U',
    'MT_PVR_FN_MTGPUUPDATEOOMSTATS': '0x27U',
    'MT_PVR_FN_RGXCREATECOMPUTECONTEXT': '0x0U',
    'MT_PVR_FN_RGXDESTROYCOMPUTECONTEXT': '0x1U',
    'MT_PVR_FN_RGXCREATEZSBUFFER': '0x2U',
    'MT_PVR_FN_RGXDESTROYZSBUFFER': '0x3U',
    'MT_PVR_FN_RGXCREATERENDERCONTEXT': '0x8U',
    'MT_PVR_FN_RGXDESTROYRENDERCONTEXT': '0x9U',
    'MT_PVR_FN_RGXCREATERENDERCONTEXT2': '0x12U',
    'MT_PVR_FN_RGXDESTROYRENDERCONTEXT2': '0x13U',
    'MT_PVR_FN_RGXCREATEKICKSYNCCONTEXT': '0x0U',
    'MT_PVR_FN_RGXDESTROYKICKSYNCCONTEXT': '0x1U',
    'MT_PVR_FN_RGXKICKSYNC2': '0x2U',
    'MT_PVR_FN_RGXSETKICKSYNCCONTEXTPROPERTY': '0x3U',
    'MT_PVR_FN_RGXKICKSYNC3': '0x4U',
    'MT_PVR_FN_RGXCREATEKICKSYNCCONTEXT2': '0x5U',
    'MT_PVR_FN_RGXDESTROYKICKSYNCCONTEXT2': '0x6U',
    'MT_PVR_FN_RGXACQUIREHWPERFFSETTINGS': '0x4U',
    'MT_PVR_FN_RGXRELEASEHWPERFFSETTINGS': '0x5U',
    'MT_PVR_FN_RGXTDMGETSHAREDMEMORY': '0x5U',
    'MT_PVR_FN_RGXTDMRELEASESHAREDMEMORY': '0x6U',
    'MT_PVR_FN_RGXTDMCREATETRANSFERCONTEXT2': '0x8U',
    'MT_PVR_FN_RGXTDMDESTROYTRANSFERCONTEXT2': '0x9U',
    'MT_PVR_FN_RGXTDMSUBMITTRANSFER3': '0xaU',
}


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


class FnIds(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.wire = WIRE.read_text()
        text = strip_comments(BRIDGE.read_text())
        start = text.index('static int pvr_bridge_dispatch')
        nxt = text.index('\nstatic int pvr_ioctl_bridge', start)
        cls.dispatch = text[start:nxt]

    def test_wire_pins_every_dispatched_id(self):
        self.assertEqual(len(PINNED), 55)
        for name, literal in PINNED.items():
            self.assertRegex(
                self.wire,
                r'#define\s+%s\s+%s\b' % (name, re.escape(literal)),
                '%s must keep its wire value %s' % (name, literal))

    def test_dispatch_labels_use_macros(self):
        self.assertIsNone(
            re.search(r'case\s+0x[0-9a-fA-F]+:', self.dispatch),
            'no bare case label may remain in pvr_bridge_dispatch')
        for name in PINNED:
            self.assertIn('case %s:' % name, self.dispatch,
                          'dispatch must route %s' % name)


if __name__ == '__main__':
    unittest.main()
