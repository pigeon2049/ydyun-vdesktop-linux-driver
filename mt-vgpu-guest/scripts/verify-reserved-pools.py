#!/usr/bin/env python3
"""Execute reserved-pool creation/suballocation/free; no GPU or Host access.

The heap plan and range allocator execute original instructions. Only OS
backing allocation/free, metadata allocation/free and locks are modeled.
This does not treat the separate static USC descriptor as the heap-2 pool.
"""
import json
from datetime import datetime, timezone
from pathlib import Path
from process_resources_reference import ProcessResourcesOracle

ROOT = Path(__file__).resolve().parents[1]


def main():
    # Reuse the original 02a34c/01ccd8 heap-plan setup, not its modeled
    # optional process-resource list or static-resource allocation hooks.
    setup = ProcessResourcesOracle()
    x, adapter = setup.x, setup.A
    x.uc.mem_write(adapter, bytes(0x2000))
    x.uc.mem_write(adapter + 0x578, setup.heap_image)
    entries = sorted(int(e['address'], 16) for e in map(json.loads,
        (ROOT / 'decompiled/mtkm64.sys/functions.jsonl').read_text().splitlines())
        if not e.get('external'))
    names = '01dafc 01d98c 01d3c8 0083b0 01dbdc 01dc9c 01daa8 01d938 03a408'
    ranges = [(a, next(e for e in entries if e > a))
              for a in (int('140' + n, 16) for n in names.split())]
    ranges += [(0x140040760, 0x140041cc4), (0x140130c10, 0x140130c12)]
    backing = {}
    freed = []

    def allocate_backing():
        size = x.arg(1)
        assert size in (0x100000, 0x200000)
        assert x.arg(2) == 0x4c4f4f50
        descriptor, cpu = x.allocate(0x40), x.allocate(size)
        x.put64(descriptor, cpu)
        x.put32(descriptor + 8, size)
        backing[descriptor] = size
        x.ret(descriptor)

    def free_backing():
        descriptor = x.arg(1)
        assert descriptor in backing and descriptor not in freed
        freed.append(descriptor)
        x.ret()

    x.hooks[0x140022218] = allocate_backing
    x.hooks[0x140022264] = free_backing
    x.hooks[0x140008084] = lambda: x.ret()  # Metadata free only.
    assert x.run(0x14001dafc, [adapter + 0x1060, adapter + 0xe98,
                             adapter + 0xc88, 0, 0x200000], ranges) == 0
    manager = x.get64(adapter + 0x1060)
    assert int.from_bytes(x.uc.mem_read(manager, 4), 'little') == 22
    expected = {1: (0x81ffd03000, 0x200000),
                2: (0x84fff00000, 0x100000),
                10: (0xf0ffe00000, 0x200000)}
    pools = []
    for index in range(22):
        slot = manager + 8 + index * 0x38
        descriptor = x.get64(slot)
        if index not in expected:
            assert descriptor == 0
            continue
        va, size = expected[index]
        assert descriptor in backing and backing[descriptor] == size
        assert (x.get64(slot + 8), x.get64(slot + 16)) == (va, size)
        # Actual rounding, capacity exhaustion, free and full-range reuse.
        first = x.run(0x14001dbdc, [slot, 5001], ranges)
        assert first and x.get64(first) == 8192 and x.get64(first + 8) == slot
        second = x.run(0x14001dbdc, [slot, size - 8192], ranges)
        assert second and x.get64(second) == ((8192 << 32) | (size - 8192))
        assert x.get64(second + 8) == slot and x.get64(slot + 16) == 0
        assert x.run(0x14001dbdc, [slot, 4096], ranges) == 0
        assert x.get64(slot + 16) == 0
        x.run(0x14001dc9c, [first], ranges)
        x.run(0x14001dc9c, [second], ranges)
        assert x.get64(slot + 16) == size
        whole = x.run(0x14001dbdc, [slot, size], ranges)
        assert whole and x.get64(whole) == size and x.get64(whole + 8) == slot
        x.run(0x14001dc9c, [whole], ranges)
        assert x.get64(slot + 16) == size
        pools.append(dict(heap=index, va=hex(va), bytes=size,
                          separate_backing=True, suballocation_checks_passed=True))
    x.run(0x14001daa8, [0, manager], ranges)
    assert set(freed) == set(backing) and len(freed) == 3
    report = dict(utc=datetime.now(timezone.utc).isoformat(), passed=True,
        reference_sha256='0512ad5a75dcf16680d608e0a20b5154564798451d6b42224be9ae8e67d6ef33',
        pools=pools, backing_allocations=len(backing), backing_frees=len(freed),
        executed=['02a34c/01ccd8 heap plan', '01dafc/01d98c reserved pools',
                  '01d3c8 and original range allocator', '01dbdc/01dc9c suballocation/free',
                  '01daa8/01d938 pool teardown'],
        modeled=['OS backing allocation/free (no physical page list)',
                 'metadata allocation/free and single-threaded lock boundaries'],
        hardware_access=False, gpu_execution=False,
        limits='Original CPU allocation logic only; no Linux dynamic-pool integration, '
               'page mapping, static-USC-to-pool copy or SDK context creation proved here.')
    (ROOT / 'reports/reserved-pools-validation.json').write_text(
        json.dumps(report, indent=2) + '\n')
    print('PASS: three original reserved pools, rounded suballocation, exhaustion, '
          'coalesced reuse and three backing frees; CPU models only')


if __name__ == '__main__':
    main()
