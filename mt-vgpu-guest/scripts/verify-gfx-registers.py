#!/usr/bin/env python3
"""Verify QY1 TA/3D C packing against original Windows and Linux UMD code."""
import ctypes as C
from datetime import datetime,timezone
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
from gfx_reference import GfxOracle
from linux_gfx_reference import LinuxGfxOracle
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/r35-gfx';OUT.mkdir(exist_ok=True)
class Source(C.Structure):
 _fields_=[('metadata',C.c_ubyte*0xe8),('vertex',C.c_ubyte*0x78),('render',C.c_ubyte*0x400),('context',C.c_ubyte*0x40),('render_base',C.c_uint64)]
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-shared','-fPIC','-O2',
 ROOT/'tests/gfx_registers_oracle_wrapper.c','-o',OUT/'gfx-registers.so'],check=True)
lib=C.CDLL(str(OUT/'gfx-registers.so'));lib.encode.argtypes=[C.c_void_p,C.c_uint32,C.POINTER(Source),C.c_uint32]
w=GfxOracle();l=LinuxGfxOracle();rng=random.Random(0x158f90)
def pack(source):
 out=(C.c_ubyte*544)();C.memset(out,0xa5,544)
 assert lib.encode(out,544,C.byref(source),2)==0 and bytes(out)[528:]==b'\xa5'*16
 return bytes(out)[:528]
windows=0
for i in range(180):
 blobs=[rng.randbytes(n) for n in (0xe8,0x78,0x400,0x40)]
 rt=bytearray(blobs[2]);struct.pack_into('<I',rt,0x148,i%9);blobs[2]=bytes(rt)
 base=rng.getrandbits(64);source=Source.from_buffer_copy(b''.join(blobs)+struct.pack('<Q',base))
 encoded=pack(source)
 expected=w.vertex_registers(*blobs[:2])+w.fragment_registers(blobs[0],blobs[2],blobs[3],base)
 assert encoded==expected
 windows+=1

def set32(b,off,value):struct.pack_into('<I',b,off,value)
def set64(b,off,value):struct.pack_into('<Q',b,off,value)
def get32(b,off):return struct.unpack_from('<I',b,off)[0]
def transfer(dst,do,src,so,n):dst[do:do+n]=src[so:so+n]

def corresponding_windows_state(job,surface,render,vertex,context):
 # A common-state fixture: the three metadata words shared by the Windows
 # TA/3D helper must agree in the independently structured Linux inputs.
 assert vertex[0x10:0x18]==render[0x60:0x68]
 assert vertex[0x48:0x50]==render[0x70:0x78]
 assert vertex[0x40:0x48]==render[0x68:0x70]
 assert get32(job,0x760)==0 # Optional DDK debug register extension not supported.
 meta=bytearray(0xe8);ta=bytearray(0x78);rt=bytearray(0x400);ctx=bytearray(0x40)
 transfer(meta,8,job,0x748,8);transfer(meta,0x38,job,0x740,8)
 for do,so in ((0x18,0x10),(0x48,0x28),(0x58,0x30),(0x68,0x48),(0x70,0x40),(0xb0,0x18)):
  transfer(meta,do,vertex,so,8)
 transfer(ta,0,surface,0x10,8);set64(ta,8,0x3089705f3089705f);transfer(ta,0x10,vertex,0x20,8)
 transfer(ta,0x70,vertex,0x38,8);set64(ta,0x58,0x10)
 for do,so in ((0x18,0x60),(0x20,0x68),(0x30,0x64)):transfer(ta,do,vertex,so,4)
 set32(ta,0x28,(get32(job,0)>>9)&8)
 transfer(ta,0x40,surface,0x18,4);transfer(ta,0x48,surface,0x34,4)
 if get32(job,0)&0x20000:transfer(ta,0x60,vertex,0x124,4)
 set32(ta,0x68,(get32(job,0)>>7)&0x40)
 set32(rt,0x148,8)
 for i in range(8):transfer(rt,8+i*40,render,0xc0+i*24,24)
 transfer(rt,0x388,render,0x180,8);transfer(ctx,0x28,context,0x98,8)
 transfer(rt,0x1e8,surface,0x10,8)
 for do,so in ((0x1f8,0x40),(0x1d8,0x3c),(0x180,0x34),(0x208,0x10),(0x378,0x48)):
  transfer(rt,do,render,so,4)
 for i in range(6):transfer(rt,0x150+i*8,surface,0x41c+i*4,4)
 transfer(rt,0x1c8,surface,0x34,4)
 for off,value in ((0x210,0x88),(0x370,0x80),(0x380,0x8000001),(0x2e8,0x80)):set32(rt,off,value)
 return Source.from_buffer_copy(bytes(meta+ta+rt+ctx)+struct.pack('<Q',0xed00000000))
linux=0
for i in range(96):
 job,surface,render,vertex,context=[bytearray(rng.randbytes(n)) for n in (0x7b8,0x600,0x200,0x140,0x330)]
 set32(job,0x760,0)
 for vo,ro in ((0x10,0x60),(0x48,0x70),(0x40,0x68)):transfer(render,ro,vertex,vo,8)
 source=corresponding_windows_state(job,surface,render,vertex,context)
 expected=l.registers(*map(bytes,(job,surface,render,vertex,context)))
 a=w.vertex_registers(bytes(source.metadata),bytes(source.vertex))
 b=w.fragment_registers(bytes(source.metadata),bytes(source.render),bytes(source.context),source.render_base)
 assert expected==a+b==pack(source),[(hex(j),expected[j],(a+b)[j]) for j in range(528) if expected[j]!=(a+b)[j]][:8]
 # Original Linux complete kick/envelope must carry these same registers.
 if i<8:
  p=l.packet(*map(bytes,(job,surface,render,vertex,context)))
  assert p['registers']==expected and len(p['packet'])==0x46f0
  for name,data in {'job':job,'surface':surface,'render':render,'vertex':vertex,'context':context,
   'registers':expected,'native-packet':p['packet'],'native-kick':p['kick']}.items():
   (OUT/f'linux-{i}-{name}.bin').write_bytes(data)
 linux+=1
# Vary all relevant source bytes for pure Windows record assembly, with
# optional indirect/fence lists disabled, so writes remain inside the fixture.
records=[]
for i in range(24):
 meta=bytearray(rng.randbytes(0xe8));ta=rng.randbytes(0x78);rt=bytearray(rng.randbytes(0x400));batch=bytearray(rng.randbytes(0x798))
 set32(rt,0x148,i%9)
 set32(meta,0x80,0) # Optional metadata list is outside this record fixture.
 for off in (0x618,0x670,0x750,0x760,0x770,0x780):set32(batch,off,0)
 batch[0x61c]=0 # No extra indirect payload.
 base=rng.getrandbits(64);contextword=rng.getrandbits(64)
 r=w.record(bytes(meta),ta,bytes(rt),bytes(batch),base,contextword,0x12345678)
 # The full serializer copies meta then changes only packing metadata not
 # consumed by the register functions (RT tile counts etc.).
 ctx=bytearray(0x40);set64(ctx,0x28,contextword)
 reference=w.vertex_registers(bytes(meta),ta)+w.fragment_registers(bytes(meta),bytes(rt),bytes(ctx),base)
 assert r['record'][0x3370:]==reference
 if i<2:
  (OUT/f'windows-{i}-kick.bin').write_bytes(r['record'])
 records.append(dict(bytes=len(r['record']),sha256=hashlib.sha256(r['record']).hexdigest()))
# Rejection must leave output untouched.
out=(C.c_ubyte*544)();C.memset(out,0x5a,544);saved=bytes(out);bad=0
for n,family,s in ((527,2,C.byref(source)),(544,1,C.byref(source)),(544,3,C.byref(source)),(544,2,None)):
 assert lib.encode(out,n,s,family)<0 and bytes(out)==saved;bad+=1
invalid=Source.from_buffer_copy(bytes(source));struct.pack_into('<I',invalid.render,0x148,9)
assert lib.encode(out,544,C.byref(invalid),2)<0 and bytes(out)==saved;bad+=1
saved_src=bytes(source);assert lib.encode(C.byref(source),528,C.byref(source),2)<0 and bytes(source)==saved_src;bad+=1
report=dict(utc=datetime.now(timezone.utc).isoformat(),passed=True,
 windows_reference_sha256=w.pe.sha256,linux_reference_sha256=l.sha256,
 windows_random_register_cases=windows,linux_windows_c_common_state_cases=linux,
 full_linux_packet_cases=8,full_windows_record_cases=len(records),invalid_cases=bad,
 vertex_register_bytes=0xb0,fragment_register_bytes=0x160,
 windows_fixed_record_bytes=0x3370,windows_record_bytes=0x3580,
 linux_fixed_record_bytes=0x33b0,linux_record_bytes=0x35c0,linux_packet_bytes=0x46f0,
 records=records,
 modeled=['RAM allocators and libc memory operations',
  'Windows state-object getters expose supplied register fixtures',
  'Windows security cookie checked; no Windows OS or device calls',
  'Linux GetFeatures uses explicit QY1 fixture and cores=1',
  'cross-OS cases use matching common metadata words and disable DDK debug extension'],
 hardware_access=False,gpu_execution=False,
 limits='CPU register/descriptor construction only. Not a complete GFX packet converter, valid shader/VDM stream, render-context resource allocator, bridge ABI or working OpenGL/Vulkan draw.')
(ROOT/'reports/r35-gfx-register-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='records'},indent=2))
