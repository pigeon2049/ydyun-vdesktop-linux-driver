#!/usr/bin/env python3
"""CE3 original initialization/copy/barrier/finalization with modeled RAM allocator.

Preparation capability requiring external synchronization is disabled in this
fixture. No hardware is touched, and record-to-DMA conversion is not executed.
"""
from reference_oracle import ReferenceOracle
import ctypes as C
import json,struct,random,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
from unicorn.x86_const import UC_X86_REG_RIP,UC_X86_REG_RAX
x=ReferenceOracle();OBJ=0x200000;SRC=0x204000;DST=0x205000;REG=0x206000;DEVICE=0x210000;VT=0x211000;OUT=0x400000;GPU=0x800000
state={'pos':0,'start':None,'chunk':0};desc=[]
def reserve():
 pos=state['pos'];n=0x98
 if (pos%4096)+n>4096:pos=(pos+4095)&~4095
 state['chunk']=pos//4096;state['start']=pos;state['pos']=pos+n
 x.ret(OUT+pos)
def commit():
 pos=x.arg(1)-OUT
 assert state['start'] is not None and state['start']<=pos<=state['pos'],(state,pos)
 state['pos']=pos;state['start']=None;x.ret(0)
x.hooks[0x1400fd3a4]=reserve;x.hooks[0x1400fc600]=commit
x.hooks[0x1400a4140]=lambda:x.ret(state['chunk']+1)
x.hooks[0x1400bc878]=lambda:x.ret(0x26)
x.hooks[0x1400fcac0]=lambda:x.ret(GPU+state['pos'])
x.hooks[0x14004b2ac]=lambda:x.ret(DEVICE)
x.put64(DEVICE,VT);x.put64(VT+0x158,0x2f8800);x.hooks[0x2f8800]=lambda:x.ret(1)
x.put64(0x140138420,0x2f8810);x.hooks[0x2f8810]=lambda:x.uc.reg_write(UC_X86_REG_RIP,x.uc.reg_read(UC_X86_REG_RAX))
for a in (0x1400e8ef4,0x1400e9ca8,0x140130c50,0x1400e9008):x.hooks[a]=x.ret
x.hooks[0x1400dd328]=lambda:x.ret(0x220000)
x.hooks[0x1400e3474]=lambda:(desc.append(bytes(x.uc.mem_read(x.arg(1),256))),x.ret(0))
x.hooks[0x140122590]=lambda:x.ret(bool(flags&4))
x.hooks[0x140044efc]=lambda:(_ for _ in ()).throw(AssertionError('reference assert'))
x.put64(OBJ+0xb58,0x240000)


ranges=[(0x140045138,0x14004518b),(0x1400e5df4,0x1400e6350),(0x1400e4d04,0x1400e51fa),(0x1400e4b7c,0x1400e4d03),
 (0x1400e5360,0x1400e54a7),(0x1400e9420,0x1400e9aa1),(0x1400e9b88,0x1400e9ca7),
 (0x1400e4058,0x1400e406a),(0x1400e33d0,0x1400e33e2),(0x140056a00,0x140056a5c),
 (0x14004b2bc,0x14004b2cb),(0x1400a6168,0x1400a61be),(0x1400a8ce0,0x1400a8d5e),
 (0x1400c57c8,0x1400c58eb),(0x1400450c8,0x140045138),(0x14004a2d4,0x14004a2f0)]

class Copy(C.Structure):
 _fields_=[('src',C.c_uint64),('dst',C.c_uint64),('bytes',C.c_uint64),('version',C.c_uint32),('flags',C.c_uint32)]
class Input(C.Structure):
 _fields_=[('copy',Copy),('command_va',C.c_uint64)]
libpath=ROOT/'build/firmware/ce-stream.so'
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-shared','-fPIC',ROOT/'tests/ce_stream_oracle_wrapper.c','-o',libpath],check=True)
lib=C.CDLL(str(libpath));lib.prepare.argtypes=[C.c_void_p,C.c_uint32,C.POINTER(Input)]
packet=C.create_string_buffer(448);rng=random.Random(3000003);cases=0
# Reused by the paging oracle to compare the entire construction chain.
sequence_fixtures=[]
for flags in range(8):
 for n in (1,16,255,4096):
  for placement in (0,32,128,3840):
   command=((rng.randrange(1,1<<28)<<12)+placement)
   GPU=command-4096
   source=(rng.randrange(1<<20,1<<21)<<12);dest=(rng.randrange(1<<21,1<<22)<<12)
   x.uc.mem_write(OBJ,bytes(0x1400));x.uc.mem_write(OUT,bytes(16384))
   x.put64(OBJ+0xb58,0x240000);x.put32(OBJ+0x1018,6)
   x.put64(SRC+8,source);x.put64(DST+8,dest)
   x.put32(SRC+0x54,2 if flags&1 else 0);x.put32(DST+0x54,2 if flags&2 else 0)
   x.uc.mem_write(REG,struct.pack('<QQQ',0,0,n))
   state.update(pos=0,start=None,chunk=0);desc.clear()
   x.run(0x1400e5df4,[OBJ,SRC,DST,1,REG],ranges)
   x.run(0x1400e4b7c,[OBJ],ranges)
   x.run(0x1400e9420,[OBJ],ranges)
   root=x.get64(OBJ+0xfa0)-GPU
   assert root==4096 and state['pos']-root==176 and len(desc)==1
   expected=bytes(x.uc.mem_read(OUT+root,176))+desc[0]
   inp=Input(Copy(source,dest,n,3,flags),command)
   C.memset(packet,0xa5,448)
   assert lib.prepare(packet,448,C.byref(inp))==0
   assert packet.raw[:432]==expected,(flags,n,placement)
   assert packet.raw[432:]==b'\xa5'*16
   sequence_fixtures.append((source,dest,n,flags,command,expected))
   cases+=1
report=dict(passed=True,reference_sha256=x.pe.sha256,complete_sequences=cases,
 command_bytes=176,software_record_bytes=256,hardware_accessed=False,
 functions=['1400e4d04','1400e5df4','1400e4b7c','1400e5360','1400e9420','1400e9b88'],
 assumptions=['CE3; state reservation 6 DWORDs','single copy plus explicit pending-copy barrier',
 'no optional prefix or saved external state','no platform pre-copy external synchronization',
 'modeled 0x26-DWORD reservation windows over 4096-byte RAM chunks'],
 limits='The command and software record bytes match under these assumptions. Allocator, capability predicates, record storage and object reset are modeled. Live profile and record-to-DMA conversion/GPU execution are unverified.')
(ROOT/'reports/ce-stream-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
