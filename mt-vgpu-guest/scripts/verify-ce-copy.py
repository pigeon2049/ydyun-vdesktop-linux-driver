#!/usr/bin/env python3
"""Match isolated CE linear-copy payloads against original x86 instructions."""
import ctypes as C
import errno,json,random,struct,subprocess
from pathlib import Path
from reference_oracle import ReferenceOracle
ROOT=Path(__file__).resolve().parents[1]
OBJ,SRC,DST,REGION,OUT=0x200000,0x204000,0x205000,0x206000,0x208000
class Input(C.Structure):
 _fields_=[('src',C.c_uint64),('dst',C.c_uint64),('bytes',C.c_uint64),('version',C.c_uint32),('flags',C.c_uint32)]
def main():
 libpath=ROOT/'build/firmware/ce-copy.so'
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-shared','-fPIC',ROOT/'tests/ce_copy_oracle_wrapper.c','-o',libpath],check=True)
 lib=C.CDLL(str(libpath));lib.encode.argtypes=[C.c_void_p,C.c_uint32,C.POINTER(Input)]
 o=ReferenceOracle();rng=random.Random(3000222)
 for addr in (0x1400e33e4,0x1400dd6ac,0x1400dd39c,0x1400e8ef4,0x1400e4d04,0x1400e4b7c,0x140130c50):o.hooks[addr]=o.ret
 o.hooks[0x14004b2ac]=lambda:o.ret(0x210000)
 o.hooks[0x140098d14]=lambda:o.ret(1) # Make the bit59-capability branch observable.
 o.hooks[0x1400fd3a4]=lambda:o.ret(OUT)
 endings=[]
 o.hooks[0x1400fc600]=lambda:(endings.append(o.arg(1)),o.ret())
 packet=C.create_string_buffer(56);cases=0
 for version in (1,2,3):
  for flags in range(16):
   o.hooks[0x14004c2fc]=lambda:o.ret(bool(flags&8))
   o.hooks[0x140122590]=lambda:o.ret(bool(flags&4))
   for n in (1,4096,0xffffffff,0x100000001):
    src=rng.getrandbits(64);dst=rng.getrandbits(64)
    src_off=rng.randrange(4096);dst_off=rng.randrange(4096)
    o.put64(SRC+8,(src-src_off)&((1<<64)-1));o.put64(DST+8,(dst-dst_off)&((1<<64)-1))
    o.put32(SRC+8+0x4c,2 if flags&1 else 0);o.put32(DST+8+0x4c,2 if flags&2 else 0)
    o.uc.mem_write(REGION,struct.pack('<QQQ',src_off,dst_off,n))
    o.uc.mem_write(OUT,b'\xa5'*80);o.uc.mem_write(OBJ+0x101c,b'\x00')
    fn=0x1400e5df4 if version==3 else 0x1400df1a4
    end=0x1400e6350 if version==3 else 0x1400df776
    ranges=[(fn,end),(0x14004b2bc,0x14004b2cb),(0x1400a6168,0x1400a61be),
      (0x1400a8ce0,0x1400a8d5e),(0x1400c57c8,0x1400c58eb)]
    o.run(fn,[OBJ,SRC,DST,1,REGION],ranges)
    inp=Input(src,dst,n,version,flags)
    C.memset(packet,0xa5,56)
    assert lib.encode(packet,56,C.byref(inp))==0
    raw=bytes(o.uc.mem_read(OUT,56))
    assert raw==packet.raw,(version,flags,n,raw.hex(),packet.raw.hex())
    assert endings[-1]==OUT+40
    cases+=1
 saved=packet.raw
 for inp in (Input(0,0,1,0,0),Input(0,0,1,4,0),Input(0,0,1,1,16)):
  assert lib.encode(packet,56,C.byref(inp))==-errno.EINVAL and packet.raw==saved
 assert lib.encode(packet,39,C.byref(Input(0,0,1,1,0)))==-errno.EINVAL and packet.raw==saved
 report=dict(passed=True,reference_sha256=o.pe.sha256,copy_payloads=cases,hardware_accessed=False,
  versions=[1,2,3],payload_bytes=40,
  modeled=['stream preparation and barrier helpers','command-buffer allocation/commit','platform bit59/tag capabilities','memory property predicate'],
  limits='Isolated payload encoding only. Complete stream, optional prefix, Guest memory flags, live CE version selection and GPU execution remain unverified.')
 (ROOT/'reports/ce-copy-validation.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(report,indent=2))
if __name__=='__main__':main()
