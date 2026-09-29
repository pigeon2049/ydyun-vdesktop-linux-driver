#!/usr/bin/env python3
"""Execute original TQX bank construction and copy-job parameter preparation."""
import ctypes as C
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
from reference_oracle import ReferenceOracle
from tqx_destination_reference import DestinationOracle
from unicorn.x86_const import UC_X86_REG_RAX, UC_X86_REG_RIP

ROOT = Path(__file__).resolve().parents[1]
x = ReferenceOracle()
x.hooks[0x140130c50] = lambda: x.ret(x.uc.reg_read(UC_X86_REG_RAX))
x.hooks[0x140044efc] = lambda: (_ for _ in ()).throw(AssertionError('reference assertion'))
x.put64(0x140138420,0x2f8810)
x.hooks[0x2f8810] = lambda: x.uc.reg_write(UC_X86_REG_RIP,x.uc.reg_read(UC_X86_REG_RAX))
ranges = [(a,a+n) for a,n in ((0x1400d9340,104),(0x14004b2ac,15),
    (0x14004c2fc,52),(0x14004a59c,17),(0x140119250,977),
    (0x140056a00,92),(0x140045138,83),(0x14004a2d4,27),
    (0x14011e4c4,1),(0x140113c08,1031),(0x140116348,79),(0x14011e5bc,89),
    (0x1400421dc,46),(0x1400fbab8,337),(0x14004a2f0,84),
    (0x14011b7f0,167),(0x1401169fc,9868),(0x140114290,389),
    (0x140051bf0,31),(0x140051dbc,22),(0x14011ccdc,123),
    (0x14011d0f8,154),(0x1400fbc0c,152))]
# Ghidra body_bytes sums basic blocks and can exclude alignment gaps. Bound
# each explicitly allowed function by the next indexed entry instead.
entries=sorted(int(json.loads(line)['address'],16) for line in
    (ROOT/'decompiled/mtkm64.sys/functions.jsonl').read_text().splitlines()
    if not json.loads(line).get('external',False))
ranges=[(start,next(a for a in entries if a>start)) for start,_ in ranges]
ranges += DestinationOracle().ranges
HANDLER,MODULE,DEVICE,LAYOUT,CMD,VIEWS = 0x200000,0x204000,0x205000,0x206000,0x210000,0x214000
x.put64(HANDLER+8,MODULE);x.put64(MODULE+8,DEVICE);x.put32(DEVICE+0x380,2)
selected=[]
x.hooks[0x1400d9518] = x.ret # Reference static-table self-check, not initialization.
def selected_table():
    selected.append((x.arg(1),x.arg(2)));x.ret()
x.hooks[0x140113b80] = selected_table
x.run(0x1400d9340,[HANDLER],ranges)
assert selected == [(0x140b9e850,0x140b9ea30)]
bank = (ROOT/'build/firmware/tqx-family2-programs.bin').read_bytes()
alloc_sizes=[]
def allocation_request():
    alloc_sizes.append(x.arg(1));x.ret(0x207000)
x.hooks[0x140046cd8]=allocation_request
x.hooks[0x1400541a4]=lambda:x.ret(0x208000)
x.hooks[0x140046df0]=lambda:x.ret(0x400000)
x.uc.mem_write(0x400000,bytes(len(bank)))
assert x.run(0x140119250,[HANDLER,*selected[0],LAYOUT],ranges) == 0
assert alloc_sizes == [len(bank)]
assert bytes(x.uc.mem_read(0x400000,len(bank))) == bank
assert struct.unpack('<5Q',x.uc.mem_read(LAYOUT,40)) == (0x400000,0x4e80,0,0xb0,0x4e80)
extracted=json.loads((ROOT/'reports/tqx-program-extraction.json').read_text())
for entry in extracted['records']:
    base = HANDLER + (0x358 if entry['kind']=='shader' else 0x178)
    assert x.get64(base+entry['index']*48+16) == entry['offset']

class Input(C.Structure):
    _fields_=[('constants_va',C.c_uint64),('operation',C.c_uint32),
              ('shader_heap_base',C.c_uint32),('source_descriptor_index',C.c_uint32)]
class State(C.Structure):
    _fields_=[('constants',C.c_ubyte*20),('pds_constants',C.c_ubyte*16),
              ('pds_execution',C.c_ubyte*36)] + [(n,C.c_uint32) for n in
              ('shader_offset','shader_bytes','pds_constants_offset','pds_execution_offset',
               'constants_dwords','pds_constants_dwords')]
libpath=ROOT/'build/firmware/tqx-programs.so'
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-shared','-fPIC',
                ROOT/'tests/tqx_program_oracle_wrapper.c','-o',libpath],check=True)
lib=C.CDLL(str(libpath))
lib.build_state.argtypes=[C.c_void_p,C.c_uint32,C.POINTER(Input)]
out=(C.c_ubyte*(len(bank)+16))();C.memset(out,0xa5,C.sizeof(out))
assert lib.build_bank(out,C.sizeof(out),2)==0 and bytes(out) == bank+bytes([0xa5])*16
saved=bytes(out)
assert lib.build_bank(out,len(bank)-1,2)<0 and bytes(out)==saved
assert lib.build_bank(out,len(bank),6)<0 and bytes(out)==saved

# Original job builder runs through real parameter reordering and template
# patches and source texture encoding. Abstract heap allocation supplies known
# CPU/GPU/relative addresses; initial command state and finalization are modeled.
x.put32(DEVICE+0x71c,32);x.put32(DEVICE+0x720,16)
x.put64(CMD+0xb68,MODULE)
assert x.run(0x1400948c8,[1,DEVICE+8,DEVICE+16,DEVICE+24,DEVICE+32,
                        DEVICE+0x29a0,DEVICE+0x29a8,DEVICE+0x29b0],ranges)==0
x.hooks[0x14011e6dc]=x.ret
x.hooks[0x1401168e4]=x.ret
shader_base=0; descriptor_index=0; constants_va=0
x.hooks[0x1401140a4]=lambda:x.ret((shader_base if x.arg(1)==0 else 0x7000)+x.arg(2))
allocations=[]
def allocate_state():
    words,alignment,va_out,kind,relative_out=[x.arg(i) for i in (1,2,3,4,5)]
    words &= 0xffffffff; alignment &= 0xffffffff; kind &= 0xffffffff
    assert (kind,words,alignment) in ((4,9,4),(1,5,1),(4,4,4),(5,16,8)), (kind,words,alignment)
    index=len(allocations);cpu=0x500000+index*4096
    gpu=constants_va if kind==1 else 0x800000+index*4096
    relative=descriptor_index<<4 if kind==5 else 0x9000+index*4096
    x.uc.mem_write(cpu,bytes(words*4))
    x.put64(va_out,gpu)
    if relative_out:x.put64(relative_out,relative)
    allocations.append((cpu,words,kind,relative));x.ret(cpu)
x.hooks[0x1400a354c]=allocate_state
# Execute real 11e4c4 dispatch and 0dc638 emission. Only command-buffer
# reservation/commit and the subsequent finish callback use RAM stubs.
x.put64(CMD,0x219000);x.put64(0x219000+0x510,0x2f8820)
x.put64(0x219000+0x560,0x1400dc638)
x.put64(CMD+0xb58,0x21b000)
x.hooks[0x2f8820]=x.ret
commits=[]
def reserve_command():
    assert x.arg(0)==0x21b000
    x.uc.mem_write(0x600000,bytes(80));x.ret(0x600000)
def commit_command():
    args=tuple(x.arg(i) for i in range(3))
    assert args==(0x21b000,0x600050,1)
    commits.append(args);x.ret()
x.hooks[0x1400fd3a4]=reserve_command
x.hooks[0x1400fc600]=commit_command
class JobInput(C.Structure):
    _fields_=[(n,C.c_uint64) for n in ('source_va','destination_va','constants_va')]+[
        (n,C.c_uint32) for n in ('element_bytes','width','height','shader_heap_base',
        'source_descriptor_index','pds_code_heap_base','pds_execution_state','pds_constant_state')]
class Job(C.Structure):
    _fields_=[('source',C.c_ubyte*64),('program',State),('destination',C.c_ubyte*80)]
assert C.sizeof(Job)==240
lib.build_job.argtypes=[C.c_void_p,C.c_uint32,C.POINTER(JobInput)]
# Surface/state/rectangle for equal single-layer copies; random dimensions below.
for offset,surface,state,rect in ((0,0x215000,0x216000,0x217000),(64,0x215100,0x216100,0x217100)):
    x.put64(VIEWS+offset,surface);x.put64(VIEWS+offset+8,state);x.put64(VIEWS+offset+40,rect)
    x.put32(surface+0x28,1)
rng=random.Random(0x113c08)
class TextureInput(C.Structure):
    _fields_=[('source_va',C.c_uint64),('element_bytes',C.c_uint32),('width',C.c_uint32),('height',C.c_uint32)]
lib.build_texture.argtypes=[C.c_void_p,C.c_uint32,C.POINTER(TextureInput)]
for case in range(192):
    operation=(8,16,21)[case%3]
    shader_base=rng.randrange(0,0x100000)*128
    descriptor_index=rng.randrange(1<<31)*2 # Descriptor allocation is 32-byte aligned.
    constants_va=rng.randrange(1<<36)*4
    width,height=rng.randrange(1,32769),rng.randrange(1,32769)
    element=(1,2,4)[(case//3)%3] if operation==8 else (8 if operation==16 else 16)
    source_va=rng.randrange(1<<37)
    destination_va=rng.randrange(1<<37)
    x.uc.mem_write(0x218000,struct.pack('<Q4I',0,element,1,width,height))
    for surface,state in ((0x215000,0x216000),(0x215100,0x216100)):
        x.run(0x1400a5740,[0x218000,surface,state],ranges)
    x.put64(VIEWS+80,source_va);x.put64(VIEWS+16,destination_va)
    for rect in (0x217000,0x217100):x.uc.mem_write(rect,struct.pack('<4I',0,0,width,height))
    allocations.clear();commits.clear();x.put32(CMD+0x2190,0)
    x.run(0x140113c08,[HANDLER,CMD,VIEWS,operation,0],ranges)
    assert [(n,k) for _,n,k,_ in allocations] == [(9,4),(16,5),(5,1),(4,4)]
    state=State();req=Input(constants_va,operation,shader_base,descriptor_index)
    assert lib.build_state(C.byref(state),C.sizeof(state),C.byref(req))==0
    for index,field in ((0,'pds_execution'),(2,'constants'),(3,'pds_constants')):
        cpu,n,_,_=allocations[index]
        assert bytes(getattr(state,field)) == bytes(x.uc.mem_read(cpu,n*4)), (case,field)
    texture=(C.c_ubyte*64)();texture_req=TextureInput(source_va,element,width,height)
    assert lib.build_texture(texture,64,C.byref(texture_req))==0
    assert bytes(texture)[:48]==bytes(x.uc.mem_read(allocations[1][0],48))
    job=Job();job_req=JobInput(source_va,destination_va,constants_va,element,width,height,
        shader_base,descriptor_index,0x7000,allocations[0][3],allocations[3][3])
    assert lib.build_job(C.byref(job),C.sizeof(job),C.byref(job_req))==0
    assert bytes(job.source)==bytes(texture)
    assert bytes(job.program)==bytes(state)
    assert bytes(job.destination)==bytes(x.uc.mem_read(0x600000,80)),case
    assert len(commits)==1 and x.get64(CMD+0x2298)==0x600048
    assert struct.unpack('<I',x.uc.mem_read(CMD+0x2190,4))[0]==80
    meta=extracted['records'][operation]
    assert (state.shader_offset,state.shader_bytes)==(meta['offset'],meta['code_dwords']*4)
    assert state.pds_execution_offset==0xa0 and state.constants_dwords==5 and state.pds_constants_dwords==4

out=(C.c_ubyte*(C.sizeof(State)+16))();C.memset(out,0xa5,C.sizeof(out))
good=Input(0x100000,8,0x20000,0xffffffff)
assert lib.build_state(out,C.sizeof(out),C.byref(good))==0
assert bytes(out)[C.sizeof(State):]==bytes([0xa5])*16
saved=bytes(out)
bad=[Input(0,9,0,0),Input(1,8,0,0),Input(0,8,1,0),Input(1<<40,8,0,0),
     Input((1<<40)-4,8,0,0),Input(0,8,0xffffff80,0)]
for req in bad:assert lib.build_state(out,C.sizeof(out),C.byref(req))<0 and bytes(out)==saved
assert lib.build_state(out,C.sizeof(State)-1,C.byref(good))<0 and bytes(out)==saved
assert lib.build_state(out,C.sizeof(out),None)<0 and bytes(out)==saved
assert lib.build_state(None,C.sizeof(out),C.byref(good))<0
report=dict(utc=datetime.now(timezone.utc).isoformat(),passed=True,reference_sha256=x.pe.sha256,
            programs=131,bank_bytes=len(bank),bank_sha256=hashlib.sha256(bank).hexdigest(),
            job_cases=192,job_image_bytes=240,rejected_state_cases=9,hardware_access=False,gpu_execution=False,
            modeled=['bank allocation and platform lookup','stack cookie and memcpy',
                     'heap-relative shader/PDS address lookup','CPU/GPU state allocations',
                     'initial command state','command buffer allocation/commit and finish callback'],
            executed=['family selector 1400d9340','bank constructor 140119250',
                      'job builder 140113c08','parameter builder/reorder 1401169fc/140114290',
                      'PDS patching 14011b7f0/14011ccdc/14011d0f8','execution word 14011e5bc/1400fbab8',
                      'source texture/sampler 14011bbe4 and original callbacks',
                      'destination dispatch/emission 14011e4c4/1400dc638/1400d98c8'],
            limits='Program bytes and combined source/program/destination job construction verified in RAM; heap allocation/mapping, initial command state, final stream and GPU execution remain incomplete.')
(ROOT/'reports/tqx-program-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
