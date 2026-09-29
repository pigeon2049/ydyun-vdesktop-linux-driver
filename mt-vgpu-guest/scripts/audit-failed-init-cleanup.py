#!/usr/bin/env python3
"""Bounded original-instruction audit of Guest platform teardown, no hardware."""
import json
from pathlib import Path
from unicorn.x86_const import UC_X86_REG_RAX, UC_X86_REG_RIP
from reference_oracle import ReferenceOracle

ROOT = Path(__file__).resolve().parents[1]
DEVICE, CONFIG, GPU = 0x200000, 0x202000, 0x204000
PLATFORM = GPU + 8


def main():
    o = ReferenceOracle()
    events = []

    def record(name):
        def callback():
            events.append(dict(event=name, arg0=hex(o.arg(0)), arg1=hex(o.arg(1))))
            o.ret()
        return callback

    def bar1():
        assert o.arg(0) == PLATFORM and o.arg(1) == 0x158
        value = o.arg(2)
        # Upper bits are unspecified stack data in this reference function.
        events.append(dict(event='unregister', slot=(value >> 1) & 255,
                           valid=value & 1, descriptor=hex(value)))
        o.ret()

    def dispatch():
        o.uc.reg_write(UC_X86_REG_RIP, o.uc.reg_read(UC_X86_REG_RAX))

    o.put64(0x140138420, 0x2f8000)
    o.hooks[0x2f8000] = dispatch
    o.put64(0x140138190, 0x2f8010)
    o.hooks[0x2f8010] = record('unmap')
    o.hooks[0x140008084] = record('free-pool')
    o.hooks[0x140008090] = record('free-page')
    o.hooks[0x140027780] = bar1
    # Other subsystems are not modeled as firmware quiescence evidence.
    modeled = (0x14000c1a8, 0x14001b0d8, 0x1400241ec,
               0x14001daa8, 0x14001cc98)
    for entry in modeled:
        o.hooks[entry] = record(hex(entry))
    ranges = [(0x14000d990, 0x14000da7b), (0x1400220e8, 0x140022169),
              (0x140025a9c, 0x140025b87), (0x140025a48, 0x140025a99),
              (0x1400271d8, 0x140027272), (0x140025834, 0x140025a46),
              (0x140024f24, 0x140024f30), (0x14000b6a8, 0x14000b6f2),
              (0x14000edf8, 0x14000ee27)]
    cases = []
    for mask in range(16):
        o.uc.mem_write(DEVICE, bytes(0x2000))
        o.uc.mem_write(CONFIG, b'\1' + bytes(4095))
        o.uc.mem_write(GPU, bytes(0x4000))
        o.put64(DEVICE, CONFIG)
        o.put64(DEVICE + 0x418, GPU)
        o.uc.mem_write(PLATFORM + 0x44, b'\1\1\1\1')
        o.run(0x14002d950, [PLATFORM], [(0x14002d950, 0x14002db8d)])
        offsets = (0x9f8, 0xa00, 0xa28, 0x1ad8)
        assert all(o.get64(PLATFORM + x) == 0 for x in offsets)
        o.put64(PLATFORM + 0x1bf0, 0x220000)
        o.put64(PLATFORM + 0x1be0, 0x200000)
        o.put64(PLATFORM + 0x1cb0, 0x230000)
        o.put64(PLATFORM + 0x1cb8, 0x231000)
        for slot in range(4):
            if mask & (1 << slot):
                o.put64(PLATFORM + 0x1bf8 + slot * 0x28, 0x240000 + slot * 4096)
                o.uc.mem_write(PLATFORM + 0x1c04 + slot * 0x28, b'\1')
        if mask & 1:
            o.put64(PLATFORM + 0x1c18, 0x140024f24)
            o.put64(PLATFORM + 0x1cc8, 0x240000)
        events.clear()
        o.run(0x14000d990, [DEVICE], ranges)
        expected = [i for i in range(3, -1, -1) if mask & (1 << i)]
        writes = [x for x in events if x['event'] == 'unregister']
        assert [x['slot'] for x in writes] == expected
        assert all(x['valid'] == 0 for x in writes)
        assert o.get64(DEVICE + 0x418) == 0
        assert o.get64(PLATFORM + 0x1cc8) == 0
        assert all(o.get64(PLATFORM + 0x1bf8 + i * 0x28) == 0 for i in range(4))
        assert all(o.get64(PLATFORM + x) == 0 for x in (0x1bf0, 0x1cb0, 0x1cb8))
        cases.append(dict(registered_mask=mask, events=list(events), platform_pointer_cleared=True))
    report = dict(reference_sha256=o.pe.sha256, passed=True, cases=cases,
                  hardware_written=False, host_emulated=False,
                  native_cleanup_callbacks_absent_in_guest=[hex(x) for x in offsets],
                  modeled_helpers=[hex(x) for x in modeled] + ['allocator frees', 'unmap', 'MMIO'],
                  limits='Selected failed-init platform destruction only; other subsystem cleanup is hooked. No firmware quiescence acknowledgment is established by this test.')
    (ROOT / 'reports/failed-init-cleanup-oracle.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: 16 registration masks through failed-init platform destruction; no hardware access')


if __name__ == '__main__':
    main()
