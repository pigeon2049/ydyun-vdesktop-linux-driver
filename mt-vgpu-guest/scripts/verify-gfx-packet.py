#!/usr/bin/env python3
"""Compare the complete bounded GFX packet with original Windows instructions."""
import ctypes as C
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
from gfx_reference import GfxOracle

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/r35-gfx';OUT.mkdir(exist_ok=True)
class Registers(C.Structure):
    _fields_=[('metadata',C.c_ubyte*0xe8),('vertex',C.c_ubyte*0x78),
              ('render',C.c_ubyte*0x400),('context',C.c_ubyte*0x40),('render_base',C.c_uint64)]
class Source(C.Structure):
    _fields_=[('registers',Registers),('batch',C.c_ubyte*0x798),('render_word',C.c_uint32)]
class Input(C.Structure):
    _fields_=[('dma_va',C.c_uint64),('state_va',C.c_uint64),('job_ref',C.c_uint64),
              ('frame',C.c_uint32),('pid',C.c_uint32)]
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-shared','-fPIC','-O2',
    ROOT/'tests/gfx_packet_oracle_wrapper.c','-o',OUT/'gfx-packet.so'],check=True)
lib=C.CDLL(str(OUT/'gfx-packet.so'))
lib.encode_packet.argtypes=[C.c_void_p,C.c_uint32,C.POINTER(Source),C.POINTER(Input),C.c_uint32]
oracle=GfxOracle();rng=random.Random(0x15afa0)
cases=[];optional=(0x618,0x670,0x750,0x760,0x770,0x780)
for i in range(288):
    s=Source.from_buffer_copy(rng.randbytes(C.sizeof(Source)))
    struct.pack_into('<I',s.registers.render,0x148,i%9)
    struct.pack_into('<I',s.registers.metadata,0x80,0)
    if i%3==0:struct.pack_into('<Q',s.registers.metadata,0,0)
    for off in optional:struct.pack_into('<I',s.batch,off,0)
    s.batch[0x61c]=0;s.batch[0x620]=i&255
    struct.pack_into('<I',s.batch,0x5c8,i&255)
    inp=Input(0x50000000+4096*rng.randrange(8192),rng.getrandbits(64),
              rng.getrandbits(64),rng.getrandbits(32),rng.getrandbits(32))
    out=(C.c_ubyte*(0x46c0+16))();C.memset(out,0xa5,C.sizeof(out))
    assert lib.encode_packet(out,C.sizeof(out),C.byref(s),C.byref(inp),2)==0
    r=oracle.packet(bytes(s.registers.metadata),bytes(s.registers.vertex),bytes(s.registers.render),
        bytes(s.batch),s.registers.render_base,struct.unpack_from('<Q',s.registers.context,0x28)[0],
        s.render_word,inp.dma_va,inp.frame,inp.job_ref,inp.pid,inp.state_va)
    actual=bytes(out)
    assert actual==r['packet']+b'\xa5'*16,(i,[(hex(j),actual[j],r['packet'][j])
        for j in range(0x46c0) if actual[j]!=r['packet'][j]][:20])
    if i<9:
        for name,data in {'packet':r['packet'],'shape':r['shape'],'source':bytes(s),'input':bytes(inp)}.items():
            (OUT/f'windows-full-{i}-{name}.bin').write_bytes(data)
    cases.append(hashlib.sha256(r['packet']).hexdigest())

bad=0
def rejected(src=s,request=inp,size=0x46c0,family=2):
    global bad
    out=(C.c_ubyte*(0x46c0+16))();C.memset(out,0x5a,C.sizeof(out));saved=bytes(out)
    sp=C.byref(src) if src is not None else None
    ip=C.byref(request) if request is not None else None
    assert lib.encode_packet(out,size,sp,ip,family)<0 and bytes(out)==saved
    bad+=1
for off in optional:
    z=Source.from_buffer_copy(bytes(s));struct.pack_into('<I',z.batch,off,1);rejected(src=z)
z=Source.from_buffer_copy(bytes(s));z.batch[0x61c]=1;rejected(src=z)
z=Source.from_buffer_copy(bytes(s));struct.pack_into('<I',z.registers.metadata,0x80,1);rejected(src=z)
z=Source.from_buffer_copy(bytes(s));struct.pack_into('<I',z.registers.render,0x148,9);rejected(src=z)
for va in (0,0x3ffff000,0x50000001,0x803fffc000,0xfffffffffffff000):
    req=Input.from_buffer_copy(bytes(inp));req.dma_va=va;rejected(request=req)
rejected(src=None);rejected(request=None);rejected(size=0x46bf)
rejected(family=1);rejected(family=3)
for delta in (-64,0,64):
    # Both directions of source overlap, while all memory remains allocated.
    backing=(C.c_ubyte*0x6000)();dest=C.addressof(backing)+0x100
    src=C.cast(dest+delta,C.POINTER(Source));C.memmove(src,C.byref(s),C.sizeof(s))
    saved=bytes(backing)
    assert lib.encode_packet(dest,0x46c0,src,C.byref(inp),2)<0 and bytes(backing)==saved
    bad+=1
backing=(C.c_ubyte*0x6000)();ip=C.cast(backing,C.POINTER(Input));ip[0]=inp;saved=bytes(backing)
assert lib.encode_packet(backing,0x46c0,C.byref(s),ip,2)<0 and bytes(backing)==saved;bad+=1
report=dict(utc=datetime.now(timezone.utc).isoformat(),passed=True,
    reference_sha256=oracle.pe.sha256,full_windows_packet_cases=len(cases),invalid_cases=bad,
    packet_bytes=0x46c0,record_bytes=0x3580,record_offset=0x90,register_offset=0x3400,
    csw_offset=0x3620,csw_bytes=0x10a0,region_count=2,
    original_functions=['180157d80 layout constants','180181ec0 complete shape',
        '18015afa0 outer packet','18015b880 combined TA/3D record',
        '180158cc0 render metadata','180158f90 TA registers','180159520 3D registers'],
    modeled=['CPU state getter callbacks','layout object populated from original shape',
        'libc memory operations','fixed PID callback','bounded stack probe',
        'checked security cookie preserving RAX'],
    hardware_access=False,gpu_execution=False,
    limits='One family-2 batch/core; optional lists disabled. Raw resource/register fixtures and initial zero CSW do not establish valid shader, VDM stream, render-context allocation, ownership or GPU execution.',
    sha256_cases=cases)
(ROOT/'reports/r35-gfx-packet-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='sha256_cases'},indent=2))
