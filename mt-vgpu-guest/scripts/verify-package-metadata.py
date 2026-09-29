#!/usr/bin/env python3
"""Execute the reference updater metadata sequence with RPC/download modeled."""
import json
from pathlib import Path
import struct
from reference_oracle import ReferenceOracle

ROOT = Path(__file__).resolve().parents[1]
o = ReferenceOracle()
CTX, RPC, VERSION = 0x210000, 0x220000, 0x224000
o.put64(CTX + 0x1d18, RPC)
trace = []
enabled, available = 0, 0


def query():
    assert (o.arg(0), o.arg(1) & 255, o.arg(3) & 255) == (RPC, 0, 1)
    raw = bytes(o.uc.mem_read(o.arg(2), 32))
    value, kind, subtype = struct.unpack_from('<QBB', raw)
    assert kind == 0 and subtype in (5, 6)
    if subtype == 6:
        assert value == 0x48809490d
    trace.append({'query': subtype, 'value': None if subtype == 5 else hex(value)})
    o.uc.mem_write(o.arg(4), struct.pack('<Q', enabled if subtype == 5 else available) + bytes(24))
    o.ret()


def download():
    trace.append({'would_download': True})
    o.ret()


o.hooks[0x14002b874] = query
o.hooks[0x140026670] = download
cases = []
for enabled, available in ((0, 0), (1, 0), (1, 0x48809490e)):
    o.put64(VERSION, 0x48809490d)
    trace.clear()
    o.run(0x1400266e0, [CTX, VERSION, 0], [(0x1400266e0, 0x14002680b), (0x140130c50, 0x140130c70)])
    assert [event['query'] for event in trace if 'query' in event] == ([5, 6] if enabled else [5])
    assert any('would_download' in event for event in trace) == bool(enabled and available)
    cases.append({'enabled': enabled, 'available': hex(available), 'trace': list(trace)})
report = {'reference': '1400266e0', 'cases': cases, 'passed': True,
          'device_accessed': False, 'download_executed': False,
          'difference': 'Linux probe stops after metadata, including when a package is available'}
(ROOT / 'reports/package-metadata-oracle.json').write_text(json.dumps(report, indent=2) + '\n')
print('PASS: 3 reference metadata sequences; no device access or download')
