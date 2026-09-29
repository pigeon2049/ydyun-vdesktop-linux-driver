#!/usr/bin/env python3
"""Compare native clear bytes, full state/records/DMA against Windows instructions."""
import ctypes as C
from datetime import datetime, timezone
import json
import random
import subprocess
from tqx_fill_reference import ROOT, FillOracle
from tqx_dma_reference import DmaOracle

class Oracle(FillOracle,DmaOracle):
    pass
class Input(C.Structure):
    _fields_=[(n,C.c_uint64) for n in ('destination_va','command_va')]+[
        (n,C.c_uint32) for n in ('element_bytes','width','height','x','y','rect_width','rect_height')]+[
        ('color',C.c_uint32*4)]+[(n,C.c_uint32) for n in ('shader_heap_base','pds_code_heap_base','pds_initial_state')]
class Source(C.Structure):
    _fields_=[('record',C.c_ubyte*296),('page_record',C.c_ubyte*16),('root_export',C.c_ubyte*16)]
class DmaInput(C.Structure):
    _fields_=[('dma_va',C.c_uint64),('state_va',C.c_uint64),('cores',C.c_uint32)]
class DmaImage(C.Structure):
    _fields_=[('bytes',C.c_uint32),('reserved',C.c_uint32),('descriptor',C.c_ubyte*8192),('submission_view',C.c_ubyte*24)]
libpath=ROOT/'build/firmware/tqx-fill.so'
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-shared','-fPIC',
    ROOT/'tests/tqx_fill_oracle_wrapper.c',ROOT/'tests/tqx_dma_oracle_wrapper.c','-o',libpath],check=True)
lib=C.CDLL(str(libpath))
lib.build_fill.argtypes=[C.c_void_p,C.c_uint32,C.POINTER(Input)]
lib.encode_dma.argtypes=[C.c_void_p,C.c_uint32,C.POINTER(Source),C.POINTER(DmaInput)]
o=Oracle();rng=random.Random(0x10ae38);cases=[]
for element in (1,2,4,8,16):
    for width,height in ((1,1),(32768,1),(1,32768),(32768,32768),(128,128),(67,109)):
        cases.append((element,width,height,0,0,width,height))
        cases.append((element,width,height,width-1,height-1,1,1))
    for _ in range(32):
        w,h=[rng.randrange(1,32769) for _ in range(2)]
        x,y=rng.randrange(w),rng.randrange(h)
        cases.append((element,w,h,x,y,rng.randrange(1,w-x+1),rng.randrange(1,h-y+1)))
for i,(e,w,h,x,y,rw,rh) in enumerate(cases):
    req=Input(rng.randrange(1<<38),0x40000000+rng.randrange(4096)*4096,e,w,h,x,y,rw,rh,
        (C.c_uint32*4)(*[rng.getrandbits(32) for _ in range(4)]),
        rng.randrange(1<<20)*128,rng.randrange(1<<20)*16,rng.randrange(1<<20)*16)
    out=(C.c_ubyte*420)();C.memset(out,0xa5,420)
    assert lib.build_fill(out,420,C.byref(req))==0
    r=o.fill(req.destination_va,e,w,h,list(req.color),(x,y,rw,rh),command_va=req.command_va,
        shader_base=req.shader_heap_base,pds_base=req.pds_code_heap_base,state_base=req.pds_initial_state)
    assert [(n,k) for n,k,_,_ in r['allocations']]==[(4,4)]
    expected=r['command']+r['allocations'][0][3]+r['record']+r['page_record']
    assert len(expected)==404 and bytes(out)==expected+b'\xa5'*16,(i,bytes(out)[:76].hex(),r['command'].hex())
    src=Source.from_buffer_copy(r['record']+r['page_record']+r['root_export'])
    din=DmaInput(0x50000000+rng.randrange(4096)*4096,0x60000000+rng.randrange(4096)*4096,1+i%8)
    dout=DmaImage()
    assert lib.encode_dma(C.byref(dout),C.sizeof(dout),C.byref(src),C.byref(din))==0
    d=o.serialize(din.dma_va,din.state_va,din.cores)
    assert dout.bytes==d['shape'][0] and bytes(dout.descriptor)==d['data']+bytes(8192-dout.bytes)
    assert bytes(dout.submission_view)==d['view']
saved=bytes(out);bad=0
for field,value in [('width',0),('height',32769),('element_bytes',3),('x',req.width),('y',req.height),
    ('rect_width',0),('rect_height',0),('rect_width',0xffffffff),('rect_height',0xffffffff),
    ('destination_va',(1<<40)-1),('command_va',1),('pds_initial_state',1),('pds_code_heap_base',0xfffffff0),
    ('shader_heap_base',0xfffffff0)]:
    b=Input.from_buffer_copy(bytes(req));setattr(b,field,value)
    assert lib.build_fill(out,420,C.byref(b))<0 and bytes(out)==saved,(field,value)
    bad+=1
assert lib.build_fill(out,403,C.byref(req))<0 and bytes(out)==saved
report=dict(utc=datetime.now(timezone.utc).isoformat(),passed=True,reference_sha256=o.x.pe.sha256,
    native_fill_cases=len(cases),full_dma_cases=len(cases),invalid_cases=bad+1,
    element_bytes=[1,2,4,8,16],command_bytes=76,state_allocations=1,
    executed=['14010ae38 clear wrapper','14011e4c4 dispatch','1400dc430 inline-color clear',
              '1400d9eec rectangle/format','14011d194 finalization','14004e924 full DMA serializer'],
    modeled=['original StreamOracle RAM allocators/OS models','already packed destination color',
             'tightly packed linear surface, one clipped rectangle'],hardware_access=False,gpu_execution=False)
(ROOT/'reports/r32-fill-encoding-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
