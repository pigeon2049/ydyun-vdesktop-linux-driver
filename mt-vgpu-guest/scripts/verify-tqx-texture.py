#!/usr/bin/env python3
"""Compare complete original TQX source texture/sampler encoding with Linux."""
import ctypes as C
from datetime import datetime, timezone
import json
from pathlib import Path
import random
import subprocess
from tqx_texture_reference import TextureOracle

ROOT=Path(__file__).resolve().parents[1]
class Input(C.Structure):
    _fields_=[('source_va',C.c_uint64),('element_bytes',C.c_uint32),
              ('width',C.c_uint32),('height',C.c_uint32)]
libpath=ROOT/'build/firmware/tqx-texture.so'
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-shared','-fPIC',
                ROOT/'tests/tqx_texture_oracle_wrapper.c','-o',libpath],check=True)
lib=C.CDLL(str(libpath));lib.build_texture.argtypes=[C.c_void_p,C.c_uint32,C.POINTER(Input)]
x=TextureOracle();rng=random.Random(0x11bbe4)
cases=[]
for element in (1,2,4,8,16):
    for w,h in ((1,1),(1,32768),(32768,1),(32768,32768),(17,32),(4095,3)):
        span=w*h*element
        for src in (0,0x123456780,(1<<40)-span):
            cases.append((src,element,w,h))
    for _ in range(64):
        cases.append((rng.randrange(1<<38),element,rng.randrange(1,32769),rng.randrange(1,32769)))
    for low in range(16):
        cases.append((0x100000+low,element,16,16))
for i,(src,element,w,h) in enumerate(cases):
    req=Input(src,element,w,h);out=(C.c_ubyte*80)();C.memset(out,0xa5,80)
    assert lib.build_texture(out,80,C.byref(req))==0
    expected=x.run(src,element,w,h,rng.randrange(1<<27)*32)
    assert bytes(out)[:48]==expected[:48],(i,src,element,w,h,bytes(out).hex(),expected.hex())
    assert bytes(out)[48:64]==bytes(16) and bytes(out)[64:]==bytes([0xa5])*16
saved=bytes(out)
bad=[Input(0,0,1,1),Input(0,3,1,1),Input(0,32,1,1),Input(0,1,0,1),
     Input(0,1,1,0),Input(0,1,32769,1),Input(0,1,1,32769),
     Input(1<<40,1,1,1),Input((1<<40)-1,1,2,1),Input((1<<64)-1,1,1,1)]
for req in bad:assert lib.build_texture(out,80,C.byref(req))<0 and bytes(out)==saved
assert lib.build_texture(out,63,C.byref(Input(0,1,1,1)))<0 and bytes(out)==saved
assert lib.build_texture(out,80,None)<0 and bytes(out)==saved
assert lib.build_texture(None,80,C.byref(Input(0,1,1,1)))<0
report=dict(utc=datetime.now(timezone.utc).isoformat(),passed=True,
            reference_sha256=x.x.pe.sha256,cases=len(cases),rejected_cases=len(bad)+3,
            encoded_bytes=48,allocation_bytes=64,linux_zero_padding_bytes=16,
            formats=[1,2,4,8,16],family=2,
            executed=['callback installation 1400948c8','source preparation 14011bbe4',
                      'texture callback 14008deb8/1400ae5f8/1400acd90',
                      'default sampler 1400949fc/140078d18/1400aec68'],
            modeled=['family-2 device fields','64-byte CPU/GPU/heap-relative allocation',
                     'memcpy and stack cookie'],hardware_access=False,gpu_execution=False,
            limits='Linear byte-copy geometry and default sampler only; live descriptor heap mapping, upload and execution unverified.')
(ROOT/'reports/tqx-texture-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
