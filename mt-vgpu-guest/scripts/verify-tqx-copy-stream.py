#!/usr/bin/env python3
"""Original byte-copy splitting, all jobs, state reuse, finalization and roots."""
import ctypes as C
from datetime import datetime, timezone
import json
from pathlib import Path
import random
import struct
import subprocess
from tqx_stream_reference import StreamOracle
ROOT=Path(__file__).resolve().parents[1]
class Copy(C.Structure):
    _fields_=[(n,C.c_uint64) for n in ('src','dst','bytes')]
class Addresses(C.Structure):
    _fields_=[('constants_va',C.c_uint64)]+[(n,C.c_uint32) for n in
        ('source_descriptor_index','pds_execution_state','pds_constant_state')]
class Input(C.Structure):
    _fields_=[('copy',Copy),('command_va',C.c_uint64)]+[(n,C.c_uint32) for n in
        ('shader_heap_base','pds_code_heap_base','pds_initial_state')]+[('addresses',Addresses*4)]
class Program(C.Structure):
    _fields_=[('constants',C.c_ubyte*20),('pds_constants',C.c_ubyte*16),('pds_execution',C.c_ubyte*36),
              ('meta',C.c_uint32*6)]
class Image(C.Structure):
    _fields_=[('count',C.c_uint32),('command_bytes',C.c_uint32),('commands',C.c_ubyte*320),
              ('sources',(C.c_ubyte*64)*4),('programs',Program*4),('pds_initial',C.c_ubyte*16),
              ('record',C.c_ubyte*296),('page_record',C.c_ubyte*16),('root_export',C.c_ubyte*16)]
assert C.sizeof(Image)==1312
path=ROOT/'build/firmware/tqx-copy-stream.so'
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-shared','-fPIC',
                ROOT/'tests/tqx_copy_stream_oracle_wrapper.c','-o',path],check=True)
lib=C.CDLL(str(path));lib.build_copy_stream.argtypes=[C.c_void_p,C.c_uint32,C.POINTER(Input)]
x=StreamOracle();rng=random.Random(0x10f034)
lengths=[1,2,3,15,16,17,4095,4096,4097,4111,4112,32767,32768,
         0x7ffffff,0x8000000,0x8000001,0xfffffff,0xfffffffe,0xffffffff]
cases=[(0x100000+low,0x100000000+(low*7%16),n) for low in range(16) for n in lengths]
cases += [(rng.randrange(1<<20,1<<35),rng.randrange(1<<36,1<<38),rng.randrange(1,1<<32)) for _ in range(128)]
counts={};allocation_counts={}
for ci,(src,dst,length) in enumerate(cases):
    command=rng.randrange(1,1<<28)*4096;shader=rng.randrange(1<<20)*128
    pds=rng.randrange(1<<20)*16;state=rng.randrange(1<<20)*16
    constants=rng.randrange(1<<35)*4;descriptor=rng.randrange(1<<26)*2
    req=Input(Copy(src,dst,length),command,shader,pds,state)
    for i in range(4):req.addresses[i]=Addresses(constants+i*4096,descriptor+i*256,state+(1+i*4)*4096,state+(4+i*4)*4096)
    out=(C.c_ubyte*(C.sizeof(Image)+16))();C.memset(out,0xa5,C.sizeof(out))
    assert lib.build_copy_stream(out,C.sizeof(out),C.byref(req))==0
    image=Image.from_buffer_copy(out)
    assert 1<=image.count<=4 and image.command_bytes==image.count*80
    ref=x.run(src,dst,1,1,1,constants,command,shader,pds,state,descriptor,copy_bytes=length)
    alloc=ref['allocations'];assert len(alloc)==1+image.count*4
    assert alloc[0][:2]==(4,4) and bytes(image.pds_initial)==alloc[0][3]
    for i in range(image.count):
        group=alloc[1+i*4:5+i*4]
        assert [(n,k) for n,k,_,_ in group]==[(9,4),(16,5),(5,4),(4,4)]
        assert bytes(image.sources[i])[:48]==group[1][3][:48] and bytes(image.sources[i])[48:]==bytes(16)
        program=image.programs[i]
        assert bytes(program.pds_execution)==group[0][3]
        assert bytes(program.constants)==group[2][3]
        assert bytes(program.pds_constants)==group[3][3]
        control=struct.unpack_from('<I',bytes(image.commands),i*80+72)[0]
        assert control==(0x2d if i+1==image.count else 0x25)
    assert bytes(image.commands)[:image.command_bytes]==ref['command'],ci
    assert bytes(image.commands)[image.command_bytes:]==bytes(320-image.command_bytes)
    for i in range(image.count,4):
        assert bytes(image.sources[i])==bytes(64) and bytes(image.programs[i])==bytes(96)
    assert bytes(image.record)==ref['record'] and bytes(image.page_record)==ref['page_record'],ci
    assert bytes(image.root_export)==ref['root_export']
    assert bytes(out)[C.sizeof(Image):]==bytes([0xa5])*16
    counts[image.count]=counts.get(image.count,0)+1
    allocation_counts[len(alloc)]=allocation_counts.get(len(alloc),0)+1
# Force four chunks to cover late failures; inactive address slots are not read.
req.copy=Copy(0x100001,0x200000000,0xffffffff)
assert lib.build_copy_stream(out,C.sizeof(out),C.byref(req))==0
saved=bytes(out);bad=[]
for field,value in (('src',1<<40),('dst',(1<<40)-1),('bytes',0),('bytes',1<<32)):
    r=Input.from_buffer_copy(bytes(req));setattr(r.copy,field,value);bad.append(r)
for i in range(4):
    for field,value in (('constants_va',1),('pds_execution_state',0xfffffff0),('pds_constant_state',1)):
        r=Input.from_buffer_copy(bytes(req));setattr(r.addresses[i],field,value);bad.append(r)
for field,value in (('command_va',0),('command_va',1),('command_va',1<<40),
                    ('shader_heap_base',0xffffff80),('pds_code_heap_base',0xfffffff0),('pds_initial_state',1)):
    r=Input.from_buffer_copy(bytes(req));setattr(r,field,value);bad.append(r)
for r in bad:assert lib.build_copy_stream(out,C.sizeof(out),C.byref(r))<0 and bytes(out)==saved
assert lib.build_copy_stream(out,C.sizeof(Image)-1,C.byref(req))<0 and bytes(out)==saved
assert lib.build_copy_stream(out,C.sizeof(out),None)<0 and bytes(out)==saved
assert lib.build_copy_stream(None,C.sizeof(out),C.byref(req))<0
report=dict(utc=datetime.now(timezone.utc).isoformat(),passed=True,reference_sha256=x.x.pe.sha256,
    cases=len(cases),chunk_counts=counts,allocation_counts=allocation_counts,rejected_cases=len(bad)+3,
    cpu_image_bytes=C.sizeof(Image),max_command_bytes=320,
    executed=['command object constructor and initialization',
              'outer copy splitter/geometry/operation selector 14010f034/1400a624c/140114418',
              'all source/program/destination jobs 140113c08',
              'initial PDS cache/state comparison 14011e634/14011b3cc',
              'finalization, software/page records and backpatches 14011d194',
              'command root export 14011d6bc/14011a21c'],
    modeled=['RAM/GPU/relative allocations and program heap lookup','memcmp/memcpy/memset/stack cookie',
             'OS configuration getter','allocator ownership finalization 140119914'],
    assumptions=['fresh object, one region, no external synchronization, one command page',
                 'distinct caller-provided state addresses; no live heap/VM allocation'],
    hardware_access=False,gpu_execution=False,
    limits='Complete bounded CPU encoding from byte-copy region through root export, not an executable firmware submission. Heap mapping, allocation lifetime, DMA conversion and GPU execution remain incomplete.')
(ROOT/'reports/tqx-copy-stream-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
