#!/usr/bin/env python3
"""Compare full DMA allocation and submission view with original instructions."""
import ctypes as C
from datetime import datetime,timezone
import json
from pathlib import Path
import random
import struct
import subprocess
from tqx_dma_reference import DmaOracle
from unicorn.x86_const import UC_X86_REG_RSP
ROOT=Path(__file__).resolve().parents[1]
class Source(C.Structure):
    _fields_=[('record',C.c_ubyte*296),('page_record',C.c_ubyte*16),('root_export',C.c_ubyte*16)]
class Input(C.Structure):
    _fields_=[('dma_va',C.c_uint64),('state_va',C.c_uint64),('cores',C.c_uint32)]
class Image(C.Structure):
    _fields_=[('bytes',C.c_uint32),('reserved',C.c_uint32),('descriptor',C.c_ubyte*8192),('submission_view',C.c_ubyte*24)]
libpath=ROOT/'build/firmware/tqx-dma.so'
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-shared','-fPIC',ROOT/'tests/tqx_dma_oracle_wrapper.c','-o',libpath],check=True)
lib=C.CDLL(str(libpath));lib.encode_dma.argtypes=[C.c_void_p,C.c_uint32,C.POINTER(Source),C.POINTER(Input)]
x=DmaOracle();rng=random.Random(0x04e924);counts={};sizes={}
lengths=[1,15,16,4095,4096,4097,0x8000000,0x8001001,0xffffffff]
cases=[(low,length) for low in range(16) for length in lengths]
for ci,(low,length) in enumerate(cases):
    command=0x40000000+rng.randrange(4096)*4096
    ref=x.run(0x100000001+low,0x300000003,1,1,1,command_va=command,
        shader_base=rng.randrange(1<<20)*128,pds_base=rng.randrange(1<<24)*16,
        state_base=rng.randrange(1<<24)*16,copy_bytes=length)
    source=Source.from_buffer_copy(ref['record']+ref['page_record']+ref['root_export'])
    req=Input(0x50000000+rng.randrange(4096)*4096,0x60000000+rng.randrange(4096)*4096,1+ci%8)
    d=x.serialize(req.dma_va,req.state_va,req.cores)
    out=(C.c_ubyte*(C.sizeof(Image)+16))();C.memset(out,0xa5,C.sizeof(out))
    assert lib.encode_dma(out,C.sizeof(out),C.byref(source),C.byref(req))==0
    image=Image.from_buffer_copy(out)
    assert image.bytes==d['shape'][0] and image.reserved==0
    assert bytes(image.descriptor)==d['data']+bytes(8192-image.bytes)
    assert bytes(image.submission_view)==d['view'] and d['after']==bytes(16)
    assert bytes(out)[C.sizeof(Image):]==b'\xa5'*16
    counts[req.cores]=counts.get(req.cores,0)+1;sizes[req.cores]=image.bytes
    # Poison original callee stack, not output padding: serialization must not
    # inherit stale stack bits. Allocation padding remains Linux zero policy.
    if ci<8:
        x.x.hooks[0x14004e924]=lambda:x.x.uc.mem_write(x.x.uc.reg_read(UC_X86_REG_RSP)-0x600,b'\xa5'*0x600)
        poisoned=x.serialize(req.dma_va,req.state_va,req.cores)
        del x.x.hooks[0x14004e924]
        assert poisoned==d
saved=bytes(out);bad=0
for field,value in [('cores',0),('cores',9),('dma_va',1),('state_va',1),('dma_va',1<<40),
    ('state_va',1<<40),('dma_va',0x8040000000-4096),('state_va',req.dma_va),('dma_va',command)]:
    b=Input.from_buffer_copy(bytes(req));setattr(b,field,value)
    assert lib.encode_dma(out,C.sizeof(out),C.byref(source),C.byref(b))<0 and bytes(out)==saved;bad+=1
for offset in [0,0x20,0x28,0x30,0x34,0x38,0x3c,0x80,0xa0,0x120,0x124,296,304,312,320]:
    s=Source.from_buffer_copy(bytes(source));C.cast(C.byref(s),C.POINTER(C.c_ubyte))[offset]^=1
    assert lib.encode_dma(out,C.sizeof(out),C.byref(s),C.byref(req))<0 and bytes(out)==saved;bad+=1
assert lib.encode_dma(out,C.sizeof(Image)-1,C.byref(source),C.byref(req))<0 and bytes(out)==saved;bad+=1
assert lib.encode_dma(out,C.sizeof(out),None,C.byref(req))<0 and bytes(out)==saved;bad+=1
assert lib.encode_dma(out,C.sizeof(out),C.byref(source),None)<0 and bytes(out)==saved;bad+=1
assert lib.encode_dma(None,C.sizeof(out),C.byref(source),C.byref(req))<0;bad+=1
report=dict(utc=datetime.now(timezone.utc).isoformat(),passed=True,reference_sha256=x.x.pe.sha256,
    full_copy_dma_cases=len(cases),core_counts=counts,descriptor_bytes=sizes,stack_poison_cases=8,rejected_cases=bad,
    executed=['full copy generation/finalization/root export','04cc80 device descriptor constants',
        '09c3e8 actual allocation shape','052020 layout object initialization',
        '04e924 DMA serializer including actual page/record iterators','11d7b8/100c7c submission view'],
    modeled=['RAM allocations and original StreamOracle models','cores supplied explicitly, not a hardware topology discovery',
        'zero allocation padding; optional instrumentation disabled; one fresh record/page'],
    hardware_access=False,gpu_execution=False,
    limits='Family2 CPU DMA encoding only. Engine-state initialization, DMA BO mapping/upload, final work ownership and live submission remain unverified.')
(ROOT/'reports/tqx-dma-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
