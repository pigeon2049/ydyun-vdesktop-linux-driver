#!/usr/bin/env python3
"""Verify original allocation/initialization transitions; no GPU execution."""
from datetime import datetime, timezone
import json
from pathlib import Path
import struct
from tqx_state_init_reference import StateInitOracle

ROOT = Path(__file__).resolve().parents[1]
x = StateInitOracle()
cases = failures = invalid = 0
sample = None
for pattern in (0, 0x5a, 0xa5, 0xff):
    for suppressed in (False, True):
        r = x.run(pattern=pattern, suppressed=suppressed)
        # Compare the complete OS allocation request, including padding.
        create = bytearray(0x58)
        struct.pack_into('<5Q', create, 8, 0x5678, 0x9876, 0xabc, x.ALLOCATION, 0x5000)
        struct.pack_into('<4I', create, 0x30, 0x80, 2, 2, 2)
        struct.pack_into('<I', create, 0x44, 9)
        assert r['create'] == create
        mapping = struct.pack('<8Q', 0, 0x40000000, 0x8040000000,
                              x.HANDLE, 0, 5, 1, 4)
        assert r['mapping'] == mapping
        # Private allocation fields establish the descriptor association.
        assert struct.unpack_from('<4I', r['private']) == (0, 8, 0x20, 0x5000)
        assert struct.unpack_from('<Q', r['private'], 0x40)[0] == x.descriptor
        assert not x.state_writes
        sample = r
        cases += 1
for status in (0xc0000017, 0xc0000001, 0xc000000d):
    x.run(pattern=0x5a, failure=status)
    assert not x.state_writes
    failures += 1

# Null CPU mappings and missing internal allocations are rejected by the real
# paging helper without changing the descriptor. Then re-notify another mapping.
x.run()
uc = x.x
saved = bytes(uc.uc.mem_read(x.descriptor, 0x40))
for internal, cpu in ((0, x.CPU), (x.INTERNAL, 0), (0, 0)):
    uc.put64(x.ALLOCATION+8, internal)
    uc.put64(x.PAGING+0x40, cpu)
    assert uc.run(0x1411c93d4, [x.ADAPTER, x.PAGING], x.ranges) == 0xc000000d
    assert bytes(uc.uc.mem_read(x.descriptor, 0x40)) == saved
    invalid += 1
uc.put64(x.ALLOCATION+8, x.INTERNAL)
uc.put64(x.PAGING+0x40, x.CPU+4096)
assert uc.run(0x1411c93d4, [x.ADAPTER, x.PAGING], x.ranges) == 0
expected = bytearray(saved)
struct.pack_into('<Q', expected, 0x10, x.CPU+4096)
assert bytes(uc.uc.mem_read(x.descriptor, 0x40)) == expected
assert not x.state_writes

report = dict(utc=datetime.now(timezone.utc).isoformat(), passed=True,
    reference_sha256=uc.pe.sha256, allocation_init_getter_cases=cases,
    os_allocation_failure_cases=failures, invalid_notification_cases=invalid,
    remapping_cases=1, state_memory_instruction_writes=0,
    allocation_bytes=0x5000, allocation_alignment=0x80,
    allocation_flags=9, mapping_protection=4,
    descriptor_flags_before=13, descriptor_flags_after=15,
    executed=['00405c callback table', '01e000/01df28 allocation request',
              '006470 OS callback adapter', '00700c GPU mapping callback adapter',
              '1411c93d4 operation10 dispatch', '0061c4/00a27c CPU mapping notification',
              '0219d0/041cc4 TQX ctxState getter'],
    modeled=['ExAllocatePoolWithTag/ExFreePoolWithTag',
             '1411c8214/1411c85dc allocation metadata construction/destruction',
             'OS context allocation and GPU VA assignment',
             'OS-issued paging notification and CPU mapping',
             'callback-table copy performed by 00db50; optional diagnostic backend absent'],
    sample={k: v.hex() if isinstance(v, bytes) else v for k, v in sample.items()},
    hardware_access=False, gpu_execution=False,
    conclusion='Reference init notification records CPU mapping only; no state memory writes in this executed path.',
    limits='Modeled OS backing contents and metadata. Does not prove OS zeroing, hardware initial-state acceptance, external-context path, or successful GPU execution.',
    documentation=[
      'https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkcb_createcontextallocation',
      'https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ne-d3dkmddi-_dxgk_buildpagingbuffer_operation'])
(ROOT/'reports/tqx-state-init-validation.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k != 'sample'}, indent=2))
