#!/usr/bin/env python3
"""Trace Guest information-page core count through original query/install code."""
import ctypes as C
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from reference_oracle import ReferenceOracle
from unicorn.x86_const import UC_X86_REG_RAX

ROOT = Path(__file__).resolve().parents[1]
class Profile(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ('vendor','device','family',
        'primary','ce','transfer','compute')]
libpath = ROOT/'build/firmware/tqx-topology.so'
libpath.parent.mkdir(parents=True, exist_ok=True)
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-shared','-fPIC',
    ROOT/'tests/device_profile_oracle_wrapper.c','-o',libpath], check=True)
lib = C.CDLL(str(libpath))
lib.topology.argtypes = [C.POINTER(C.c_uint32), C.POINTER(Profile), C.c_void_p, C.c_uint32]
x = ReferenceOracle()
names = '025338 0205ac 042000 0228b4 04d558 094b2c 0b1074'
entries = sorted(int(e['address'],16) for e in map(json.loads,
    (ROOT/'decompiled/mtkm64.sys/functions.jsonl').read_text().splitlines()) if not e.get('external'))
ranges = [(a,next(e for e in entries if e>a)) for a in (int('140'+s,16) for s in names.split())]
x.hooks[0x140130c50] = lambda: x.ret(x.uc.reg_read(UC_X86_REG_RAX))
x.hooks[0x140044efc] = lambda: (_ for _ in ()).throw(AssertionError('reference assertion'))
PLATFORM,INFO,OPAQUE,ADAPTER,DEVICE,VERSIONS = 0x200000,0x210000,0x220000,0x221000,0x230000,0x240000
x.uc.mem_write(PLATFORM+8+0x44,b'\1') # Actual Guest branch only, no register access.
x.put64(PLATFORM+8+0x1cb0,INFO)
x.put64(OPAQUE+8,ADAPTER);x.put64(ADAPTER+8,PLATFORM)
x.uc.mem_write(PLATFORM+0x90,struct.pack('<HH',0x1ed5,0x222))
x.uc.mem_write(VERSIONS,struct.pack('<9I',0,2,0,1,1,0,0,0,0))
raw = (ROOT/'reports/device-info.bin').read_bytes()
profile = Profile(0x1ed5,0x222,2,2,0,1,1)
observed=[]
for cores in [*range(1,9),0,9,0xffffffff]:
    info=bytearray(raw);struct.pack_into('<I',info,0xc9c,cores)
    x.uc.mem_write(INFO,bytes(info))
    x.run(0x140025338,[PLATFORM+8],ranges)
    assert struct.unpack('<I',x.uc.mem_read(PLATFORM+0x94,4))[0]==cores
    x.run(0x1400205ac,[OPAQUE,DEVICE+0x3f60],ranges)
    assert struct.unpack('<I',x.uc.mem_read(DEVICE+0x3f7c,4))[0]==cores
    assert x.run(0x14004d558,[DEVICE,2,VERSIONS],ranges)==0
    assert struct.unpack('<I',x.uc.mem_read(DEVICE+0x3b4,4))[0]==cores
    output=C.c_uint32(0xa5a5a5a5);buf=C.create_string_buffer(bytes(info))
    ret=lib.topology(C.byref(output),C.byref(profile),buf,len(info))
    assert (ret==0 and output.value==cores) if 1<=cores<=8 else (ret==-34 and output.value==0xa5a5a5a5)
    observed.append(dict(reference_count=cores,linux_accepted=ret==0))
# Validate framing, caller pointers, unsupported profile, and unchanged output.
bad=0
for offset,value in [(0,0),(4,1),(4,3)]:
    info=bytearray(raw);struct.pack_into('<I',info,offset,value)
    buf=C.create_string_buffer(bytes(info));out=C.c_uint32(0x12345678)
    assert lib.topology(C.byref(out),C.byref(profile),buf,len(info))==-71 and out.value==0x12345678;bad+=1
buf=C.create_string_buffer(raw)
for size in [0,4,0xc9c,0xca0,0xcc7]:
    assert lib.topology(C.byref(out),C.byref(profile),buf,size)==-22 and out.value==0x12345678;bad+=1
assert lib.topology(None,C.byref(profile),buf,len(raw))==-22;bad+=1
assert lib.topology(C.byref(out),C.byref(profile),None,len(raw))==-22;bad+=1
assert lib.topology(C.byref(out),None,buf,len(raw))==-95;bad+=1
profile.family=3
assert lib.topology(C.byref(out),C.byref(profile),buf,len(raw))==-95 and out.value==0x12345678;bad+=1
profile.family=2
assert lib.topology(C.byref(out),C.byref(profile),buf,len(raw))==0
report=dict(utc=datetime.now(timezone.utc).isoformat(),passed=True,
    reference_sha256=x.pe.sha256,original_path_cases=observed,rejected_framing_cases=bad,
    saved_info_sha256=hashlib.sha256(raw).hexdigest(),saved_info_cores=out.value,
    executed=names.split(),modeled=['Guest platform objects and query object pointer links',
        'version table for already established family2 profile'],
    hardware_access=False,gpu_execution=False,
    limits='Uses the previously captured info page, not a fresh Host query. Proves layout-count provenance, not firmware readiness.')
(ROOT/'reports/tqx-topology-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
