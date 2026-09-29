#!/usr/bin/env python3
"""Compare original family-2 destination emission with the Linux encoder."""
import ctypes as C
from datetime import datetime, timezone
import json
from pathlib import Path
import random
import subprocess
from tqx_destination_reference import DestinationOracle

ROOT=Path(__file__).resolve().parents[1]
class Input(C.Structure):
    _fields_=[('destination_va',C.c_uint64)]+[(n,C.c_uint32) for n in
        ('element_bytes','width','height','pds_code','pds_execution_state','pds_constant_state')]
libpath=ROOT/'build/firmware/tqx-destination.so'
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-shared','-fPIC',
                ROOT/'tests/tqx_destination_oracle_wrapper.c','-o',libpath],check=True)
lib=C.CDLL(str(libpath));lib.build_destination.argtypes=[C.c_void_p,C.c_uint32,C.POINTER(Input)]
x=DestinationOracle();rng=random.Random(0xdc638);cases=[]
for element in (1,2,4,8,16):
    for w,h in ((1,1),(1,32768),(32768,1),(32768,32768),(17,32),(4095,3)):
        for dst in (0,0x123456781,(1<<40)-w*h*element):
            cases.append((dst,element,w,h))
    for _ in range(64):
        cases.append((rng.randrange(1<<38),element,rng.randrange(1,32769),rng.randrange(1,32769)))
    for low in range(16):cases.append((0x100000+low,element,16,16))
for i,args in enumerate(cases):
    pds=[rng.randrange((1<<28)-3)*16 for _ in range(3)]
    req=Input(*args,*pds);out=(C.c_ubyte*96)();C.memset(out,0xa5,96)
    assert lib.build_destination(out,96,C.byref(req))==0
    expected=x.run(*args,*pds)
    assert bytes(out)[:80]==expected,(i,args,bytes(out).hex(),expected.hex())
    assert bytes(out)[80:]==bytes([0xa5])*16
saved=bytes(out)
bad=[]
base=[0,1,1,1,0x7050,0x9000,0xc000]
for field,value in ((0,1<<40),(0,(1<<64)-1),(1,0),(1,3),(1,32),
                    (2,0),(3,0),(2,32769),(3,32769),(4,1),(5,1),(6,1),(5,0xfffffff0)):
    args=base.copy();args[field]=value;bad.append(Input(*args))
bad.append(Input((1<<40)-1,2,1,1,0x7050,0x9000,0xc000))
for req in bad:assert lib.build_destination(out,96,C.byref(req))<0 and bytes(out)==saved
assert lib.build_destination(out,79,C.byref(Input(*base)))<0 and bytes(out)==saved
assert lib.build_destination(out,96,None)<0 and bytes(out)==saved
assert lib.build_destination(None,96,C.byref(Input(*base)))<0
report=dict(utc=datetime.now(timezone.utc).isoformat(),passed=True,
    reference_sha256=x.x.pe.sha256,cases=len(cases),rejected_cases=len(bad)+3,
    command_bytes=80,formats=[1,2,4,8,16],family=2,
    executed=['destination emitter 1400dc638','geometry/format 1400d9eec/14011eb2c',
              'control 1400da4a8','copy/accounting 1400d98c8','tail pointer 1400dbf64'],
    modeled=['family-2 device fields','RAM command allocation and commit','memcpy and stack cookie'],
    hardware_access=False,gpu_execution=False,
    limits='Single-layer linear-copy destination emission only; initial command state, finalization, heap mapping and execution remain incomplete.')
(ROOT/'reports/tqx-destination-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
