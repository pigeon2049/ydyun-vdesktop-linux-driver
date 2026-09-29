#!/usr/bin/env python3
"""Execute StopAdapter control flow; record MMIO and resource cleanup only."""
import json
from pathlib import Path
from reference_oracle import ReferenceOracle

ROOT = Path(__file__).resolve().parents[1]
DEVICE, CONFIG, GPU, INFO, LAYOUT = 0x200000, 0x202000, 0x204000, 0x207000, 0x208000


def main():
    o = ReferenceOracle()
    events = []
    disconnect_result = 0

    def record(name, result=0):
        def callback():
            events.append({'event': name, 'arg0': hex(o.arg(0))})
            o.ret(result)
        return callback

    def disconnect():
        assert o.arg(0) == DEVICE
        events.append({'event': 'disconnect', 'modeled_result': disconnect_result})
        o.ret(disconnect_result)

    def bar0():
        assert o.arg(0) == GPU + 8 and o.arg(1) == 0
        events.append({'event': 'bar0', 'offset': hex(o.arg(2)), 'value': o.arg(3)})
        o.ret()

    def bar1():
        assert o.arg(0) == GPU + 8
        events.append({'event': 'bar1', 'offset': hex(o.arg(1)), 'value': o.arg(2)})
        o.ret()

    for n, iat in enumerate((0x1401380c0, 0x1401380c8)):
        o.import_noop(iat, 0x2f8000 + n * 16)
    o.put64(0x1401380d8, 0x2f8020)
    o.hooks[0x2f8020] = record('free-layout')
    for entry in (0x140040728, 0x14001cb44, 0x14000edf8, 0x14001ca4c,
                  0x14001a35c, 0x14000c1a8, 0x14001b0d8, 0x1400241ec,
                  0x14001daa8, 0x1400220e8, 0x14001cc98, 0x14001d444,
                  0x140015ca4, 0x140022264):
        o.hooks[entry] = record(hex(entry))
    o.hooks[0x140016b4c] = disconnect
    o.hooks[0x140027740] = bar0
    o.hooks[0x140027780] = bar1
    ranges = [(0x14000e18c, 0x14000e3f1), (0x14001759c, 0x140017633),
              (0x140017680, 0x140017732), (0x1400155a4, 0x140015647),
              (0x140023afc, 0x140023b83), (0x140023998, 0x1400239a1),
              (0x1400262d4, 0x1400262f9)]
    cases = []
    for layout_present, allocation_present, disconnect_result in (
            (0, 0, 0), (1, 0, 0), (1, 1, 0), (1, 1, 1)):
        for flag in (0, 1):
            o.uc.mem_write(DEVICE, bytes(0x2000))
            o.uc.mem_write(GPU, bytes(0x3000))
            o.uc.mem_write(LAYOUT, bytes(0x2000))
            o.uc.mem_write(CONFIG, b'\1' + bytes(255))
            o.put64(DEVICE, CONFIG)
            o.put64(DEVICE + 0x418, GPU)
            o.put64(DEVICE + 0x420, LAYOUT if layout_present else 0)
            o.put64(DEVICE + 0xa8, DEVICE + 0xa8)
            o.put64(DEVICE + 0xb0, DEVICE + 0xa8)
            o.put64(GPU + 8 + 0x1cb0, INFO)
            o.put64(INFO + 0x10, flag)
            o.put32(GPU + 0x1e18, 0x890)
            if allocation_present:
                o.put64(LAYOUT + 0xc0, 0x20b000)
                o.put64(LAYOUT + 0xc8, 0x20c000)
                o.uc.mem_write(LAYOUT + 0x4c8, b'\1')
            events.clear()
            assert o.run(0x14000e18c, [DEVICE], ranges) == 0
            writes = [x for x in events if x['event'] in ('bar0', 'bar1')]
            assert writes == [{'event': 'bar0', 'offset': '0x890', 'value': 0}] + (
                [{'event': 'bar1', 'offset': '0x78', 'value': 1}] if flag else [])
            disconnects = [x for x in events if x['event'] == 'disconnect']
            assert len(disconnects) == allocation_present
            if allocation_present:
                names = [x['event'] for x in events]
                assert names.index('disconnect') < names.index('0x140022264') < names.index('bar0')
                assert o.get64(LAYOUT + 0xc0) == 0 and o.get64(LAYOUT + 0xc8) == 0
            cases.append(dict(layout_present=bool(layout_present),
                              allocation_present=bool(allocation_present),
                              modeled_disconnect_result=disconnect_result, info_flag0=flag,
                              stop_return=0, events=list(events)))
    report = dict(reference_sha256=o.pe.sha256, entry='14000e18c', passed=True, cases=cases,
                  hardware_written=False, host_execution_emulated=False,
                  modeled_helpers=['single-threaded locks', 'disconnect result',
                                   'cleanup callbacks without freeing memory', 'MMIO recording'],
                  conclusion='StopAdapter writes Guest OFF even if the modeled disconnect fails. Its return is not a firmware-drain acknowledgment; this is not proof of a safe retained-session reset.')
    (ROOT / 'reports/stop-lifecycle-oracle.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: {len(cases)} original StopAdapter cases; no hardware accessed')


if __name__ == '__main__':
    main()
