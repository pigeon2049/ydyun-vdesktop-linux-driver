#!/usr/bin/env python3
"""Compare complete original shared-resource contents against Linux staging."""
import ctypes as C
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import random
import subprocess
from static_programs_reference import StaticProgramsOracle
ROOT=Path(__file__).resolve().parents[1]
SIZE=0x100000
library=ROOT/'build/firmware/static-programs.so'
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-shared','-fPIC',
                ROOT/'tests/static_programs_wrapper.c','-o',library],check=True)
lib=C.CDLL(str(library));lib.build_static.argtypes=[C.c_void_p,C.c_uint32,C.c_void_p,C.c_uint32,C.c_uint32]
x=StaticProgramsOracle();rng=random.Random(0x1a898);buffers=[C.create_string_buffer(SIZE) for _ in range(2)]
for n,baseline in enumerate((bytes(SIZE),b'\xa5'*SIZE,rng.randbytes(SIZE))):
    expected=x.initialize(baseline)
    for b in buffers:C.memmove(b,baseline,SIZE)
    assert lib.build_static(buffers[0],SIZE,buffers[1],SIZE,2)==0
    assert [b.raw for b in buffers]==expected
    assert expected[0][176:]==baseline[176:] and expected[1][20096:]==baseline[20096:]
    if n==0:
        hashes={}
        for name,data,used in zip(('pds','usc'),expected,(176,20096)):
            (ROOT/f'build/firmware/static-{name}-program.bin').write_bytes(data[:used])
            hashes[name]=hashlib.sha256(data[:used]).hexdigest()
saved=[b.raw for b in buffers]
for psize,usize,family in ((175,SIZE,2),(SIZE,20095,2),(SIZE,SIZE,6)):
    assert lib.build_static(buffers[0],psize,buffers[1],usize,family)<0
    assert [b.raw for b in buffers]==saved
for p,u in ((0,0),(0,175),(20095,0)):
    assert lib.build_static(C.byref(buffers[0],p),SIZE-p,C.byref(buffers[0],u),SIZE-u,2)<0
    assert [b.raw for b in buffers]==saved
report=dict(utc=datetime.now(timezone.utc).isoformat(),passed=True,reference_sha256=x.x.pe.sha256,
    full_resource_cases=3,bytes_compared=3*2*SIZE,program_bytes=dict(pds=176,usc=20096),
    program_sha256=hashes,invalid_cases=6,preserves_unspecified_tails=True,
    executed=['01a898 shared PDS/USC copy','021888/02186c device virtual dispatch',
              '04d758/04d708 transfer-module dispatch','09c7a8/09c788 getters and release',
              '0d9518/0fbca4 static-table self-check','0d93ac/119250 bank generation',
              '0d94d0/04567c release routing'],
    modeled=['CPU memory allocation/map/free boundaries','CPU memcpy/memcmp','preselected family-2 device/module objects'],
    hardware_access=False,gpu_execution=False,
    limits='Paging-context creation and GPU publication not executed; only its proven shared-program copy entry.')
(ROOT/'reports/static-programs-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
