#!/usr/bin/env python3
"""Original 0219d0/041cc4 pool routing, ownership insertion and rollback."""
import json
import struct
from datetime import datetime, timezone
from pathlib import Path
from process_resources_reference import ProcessResourcesOracle

ROOT=Path(__file__).resolve().parents[1]

def case(size, failure=None):
    p=ProcessResourcesOracle();p.run(5,dynamic_pools=True);x=p.x
    entries=sorted(int(e['address'],16) for e in map(json.loads,
        (ROOT/'decompiled/mtkm64.sys/functions.jsonl').read_text().splitlines()) if not e.get('external'))
    names='0219d0 041cc4 01dbdc 01dc9c 019af4 019b80 019a7c 03a408'
    ranges=p.ranges+[(a,next(e for e in entries if e>a)) for a in
        (int('140'+n,16) for n in names.split())]+[(0x140130c10,0x140130c12)]
    freed=[]
    x.hooks[0x140008084]=lambda:(freed.append(x.arg(0)),x.ret())
    ctx,out=0x600000,0x622000
    x.put64(ctx+8,p.PLATFORM);x.put64(ctx+0x10,x.get64(p.A+0x1060))
    x.put32(p.PLATFORM+0x28,size);x.put32(p.PLATFORM+0x2c,size)
    assert x.run(0x140019af4,[ctx+0xd0,0 if failure=='list_full' else 512,0],ranges)==0
    if isinstance(failure,int):
        original=x.hooks[0x140007d9c]
        def alloc():
            if x.arg(0)==failure:x.ret(0)
            else:original()
        x.hooks[0x140007d9c]=alloc
    results=[]
    for kind,index,va in ((3,10,0xf0ffe00000),(4,1,0x81ffd03000),(6,2,0x84fff00000)):
        manager=x.get64(p.A+0x1060);slot=manager+8+index*0x38
        available=x.get64(slot+16)
        x.uc.mem_write(out,struct.pack('<4Q',0,0,size,4096))
        x.run(0x1400219d0,[kind,ctx,out],ranges)
        gpu,cpu,n,align=struct.unpack('<4Q',x.uc.mem_read(out,32))
        if failure:
            assert (gpu,cpu,n,align)==(0,0,size,4096)
            assert x.get64(slot+16)==available
            results.append(dict(kind=kind,failed_without_pool_leak=True))
        else:
            descriptor,data,_,_=p.pool_allocations[index]
            rounded=(size+4095)&~4095
            assert (gpu,cpu,n,align)==(va,data,rounded,4096)
            node=x.get64(ctx+0xd0);lease=x.get64(node+0x10)
            assert x.get64(lease)==rounded and x.get64(lease+8)==slot
            assert x.get64(slot+16)==available-rounded
            x.run(0x14001dc9c,[lease],ranges)
            assert x.get64(slot+16)==available
            results.append(dict(kind=kind,heap=index,va=hex(gpu),requested=size,bytes=n))
    if not failure:
        x.uc.mem_write(out,struct.pack('<4Q',0,0,size,4096))
        x.run(0x1400219d0,[5,ctx,out],ranges)
        gpu,cpu,n,align=struct.unpack('<4Q',x.uc.mem_read(out,32))
        assert (gpu,cpu,n)==(0x81ffc00000,p.allocations[4][1],0x100000)
    return results

cases=[case(n) for n in (1,4096,5001,20096,65537)]
failures=[dict(failure=f,cases=case(5001,f)) for f in ('list_full',16,24)]
report=dict(utc=datetime.now(timezone.utc).isoformat(),passed=True,cases=cases,failures=failures,
    pool_callback_cases=15,static_pds_cases=5,rollback_cases=9,
    reference_sha256="0512ad5a75dcf16680d608e0a20b5154564798451d6b42224be9ae8e67d6ef33",
    executed=['0219d0/041cc4 routing','01dbdc and original range allocator',
              '019af4/019b80/019a7c context list ownership','01dc9c failure rollback/release'],
    modeled=['ordinary OS backing/physical page list','metadata allocation/free and locks',
             'preselected ordinary context, configured size, final page mapping boundary'],
    hardware_access=False,gpu_execution=False,
    limits='No complete SDK context construction or program copy through this callback. '
           'Linux first-fit may choose different addresses after fragmentation than Windows size bins.')
(ROOT/'reports/context-pools-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: 15 original pool callbacks, five static-PDS getters, nine allocation/list rollback cases; CPU only')
