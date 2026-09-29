#!/usr/bin/env python3
"""Compare initial PDS, job emission, record/page finalization with original code."""
import ctypes as C
from datetime import datetime, timezone
import json
from pathlib import Path
import random
import subprocess
from tqx_stream_reference import StreamOracle

ROOT=Path(__file__).resolve().parents[1]
class JobInput(C.Structure):
    _fields_=[(n,C.c_uint64) for n in ('source_va','destination_va','constants_va')]+[
        (n,C.c_uint32) for n in ('element_bytes','width','height','shader_heap_base',
        'source_descriptor_index','pds_code_heap_base','pds_execution_state','pds_constant_state')]
class Input(C.Structure):
    _fields_=[('job',JobInput),('command_va',C.c_uint64),('pds_initial_state',C.c_uint32)]
libpath=ROOT/'build/firmware/tqx-stream.so'
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-shared','-fPIC',
                ROOT/'tests/tqx_stream_oracle_wrapper.c','-o',libpath],check=True)
lib=C.CDLL(str(libpath));lib.build_stream.argtypes=[C.c_void_p,C.c_uint32,C.POINTER(Input)]
x=StreamOracle();rng=random.Random(0x11d194);cases=[]
for element in (1,2,4,8,16):
    for width,height in ((1,1),(1,32768),(32768,1),(32768,32768),(4095,3),(17,32)):
        cases.append((element,width,height))
    for _ in range(32):cases.append((element,rng.randrange(1,32769),rng.randrange(1,32769)))
for i,(element,width,height) in enumerate(cases):
    src=rng.randrange(1<<38);dst=rng.randrange(1<<38);constants=rng.randrange(1<<36)*4
    command=rng.randrange(1,1<<28)*4096
    shader=rng.randrange(1<<20)*128;pds=rng.randrange(1<<20)*16
    state=rng.randrange(1<<20)*16;descriptor=rng.randrange(1<<27)*2
    req=Input(JobInput(src,dst,constants,element,width,height,shader,descriptor,pds,
                       state+4096,state+4*4096),command,state)
    out=(C.c_ubyte*584)();C.memset(out,0xa5,584)
    assert lib.build_stream(out,584,C.byref(req))==0
    ref=x.run(src,dst,element,width,height,constants,command,shader,pds,state,descriptor)
    alloc=ref['allocations']
    assert [(n,k) for n,k,_,_ in alloc]==[(4,4),(9,4),(16,5),(5,4),(4,4)]
    data=bytes(out)
    assert data[:48]==alloc[2][3][:48] and data[48:64]==bytes(16)
    assert data[64:84]==alloc[3][3] and data[84:100]==alloc[4][3] and data[100:136]==alloc[1][3]
    assert data[160:240]==ref['command'] and data[240:256]==alloc[0][3]
    assert data[256:552]==ref['record'] and data[552:568]==ref['page_record'],i
    assert data[568:]==bytes([0xa5])*16
    assert x.before_finish[72:76]==b'\x25\0\0\0' and data[232:236]==b'\x2d\0\0\0'
# Reject malformed command placement and late job failures without output changes.
saved=bytes(out);bad=[]
for va in (0,1,4095,4097,1<<40,(1<<64)-4096):
    b=Input.from_buffer_copy(bytes(req));b.command_va=va;bad.append(b)
for field,value in (('pds_execution_state',1),('pds_constant_state',1),
                    ('pds_code_heap_base',0xfffffff0),('constants_va',(1<<40)-4),
                    ('destination_va',(1<<40)-1)):
    b=Input.from_buffer_copy(bytes(req));setattr(b.job,field,value);bad.append(b)
b=Input.from_buffer_copy(bytes(req));b.pds_initial_state=1;bad.append(b)
for b in bad:assert lib.build_stream(out,584,C.byref(b))<0 and bytes(out)==saved
assert lib.build_stream(out,567,C.byref(req))<0 and bytes(out)==saved
assert lib.build_stream(out,584,None)<0 and bytes(out)==saved
assert lib.build_stream(None,584,C.byref(req))<0
report=dict(utc=datetime.now(timezone.utc).isoformat(),passed=True,reference_sha256=x.x.pe.sha256,
    cases=len(cases),rejected_cases=len(bad)+3,command_bytes=80,initial_pds_bytes=16,
    software_record_bytes=296,page_record_bytes=16,cpu_image_bytes=568,
    executed=['command object constructors 1400d9b08/140119e88/14011964c/1400a24f8',
              'initial state 14011e6dc/1401168e4/14011a480/14011a62c',
              'job source/program/destination 140113c08 and callbacks',
              'job state cache/completion 14011b3cc/1400dad24',
              'finalization 14011d194/14011e768/14011d378/14011d488',
              'record/page insertion and page-size patch 14011df28/14011de48/1400db2c8',
              'recovered leaf 1400da520',
              'device properties/getters 140091470/14004d0e4/14004cc38; module getter 140089a00'],
    modeled=['OS platform configuration getter for device property initialization','command allocator constructor and first-page RAM allocation/GPU end lookup',
             'CPU/GPU/heap-relative state allocation and code address lookup',
             'allocator ownership finalization 140119914','memcpy/memset/stack cookie'],
    assumptions=['single linear-copy rectangle, no previous state or external synchronization',
                 'command page starts at a nonzero 4096-aligned GPU VA',
                 'software record arrays preallocated; no page rollover'],
    hardware_access=False,gpu_execution=False,
    limits='Finalized command plus software records, not DMA/firmware submission. Heap mapping, allocation lifetime, multi-job streams and execution remain incomplete.')
(ROOT/'reports/tqx-stream-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
