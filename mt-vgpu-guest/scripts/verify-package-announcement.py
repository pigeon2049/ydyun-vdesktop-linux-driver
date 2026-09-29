#!/usr/bin/env python3
"""Run reference mode/package/version negotiation with RPC calls intercepted."""
import ctypes
import json
from pathlib import Path
import struct
import subprocess
from reference_oracle import ReferenceOracle

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/host-query'
BUILD.mkdir(parents=True, exist_ok=True)
(BUILD / 'package.c').write_text('#include "mt_guest_announcements.h"\n'
    'int package(u64 mode,u8 *out) { return mt_package_announcement(mode,out); }\n')
subprocess.run(['cc','-Wall','-Wextra','-Werror','-shared','-fPIC','-I',str(ROOT/'kernel'),
    str(BUILD/'package.c'),'-o',str(BUILD/'package.so')],check=True)
fn = ctypes.CDLL(str(BUILD/'package.so')).package
fn.argtypes = [ctypes.c_uint64,ctypes.c_void_p]
o = ReferenceOracle()
CTX, RPC = 0x210000, 0x220000
o.put64(CTX+0x1d18,RPC)
trace = []
mode, version, magic, query_error, announce_error = 1, 0x105000500070002, 0x2b70c3a, 0, 0

def query():
    assert (o.arg(0),o.arg(1) & 255,o.arg(3) & 255) == (RPC,0,1)
    raw=bytes(o.uc.mem_read(o.arg(2),32))
    value,kind,subtype=struct.unpack_from('<QBB',raw)
    assert kind==0
    # Mode query value is unspecified in the reference stack; don't compare it.
    trace.append(('query',subtype, None if subtype==1 else value))
    answer={1:mode,2:version,0:magic}[subtype]
    o.uc.mem_write(o.arg(4),struct.pack('<Q',answer)+bytes(24))
    o.ret(query_error & 0xffffffff)

def notify():
    assert (o.arg(0),o.arg(1) & 255,o.arg(3) & 255) == (RPC,0,1)
    raw=bytes(o.uc.mem_read(o.arg(2),32))
    value,kind,subtype=struct.unpack_from('<QBB',raw)
    assert kind==0
    trace.append(('notify',subtype,value))
    if subtype==4:
        output=ctypes.create_string_buffer(b'Z'*32,32)
        assert fn(mode,output)==0
        assert output.raw[:10]==raw[:10]
        assert output.raw[10:]==bytes(22)
    o.ret(announce_error & 0xffffffff)

o.hooks[0x14002b874]=query
o.hooks[0x14002b96c]=notify
compat=[o.get64(0x141106560+i*8) for i in range(9)]
scenarios=[('mode0',0,version,magic,0,0,True),('current',1,version,magic,0,0,True),
 ('mode2',2,version,magic,0,0,True),('wrong-magic',2,version,0,0,0,False),
 ('query-error',1,version,magic,-1,0,False),
 ('version-rejected',1,0x0000ffff0000ffff,magic,0,0,False),
 ('notify-error-reference-ignores',1,version,magic,0,-1,True)]
scenarios += [('historical-%d'%i,1,v,magic,0,0,True) for i,v in enumerate(compat)]
results=[]
for name,mode,version,magic,query_error,announce_error,success in scenarios:
    trace.clear()
    ret=o.run(0x1400268d0,[CTX],[(0x14002680c,0x140026a24),(0x140130c50,0x140130c70)]) & 0xffffffff
    assert (ret==0)==success,(name,hex(ret))
    assert trace[0]==('query',1,None)
    if query_error:
        assert len(trace)==1
    elif mode==2:
        assert trace==[('query',1,None),('query',0,0x2b70c3a)]
    else:
        assert trace[1]==('notify',4,0x48809490d)
        if mode==1:
            assert trace[2]==('query',2,0x0005000500070002)
            if not success:
                assert trace[3]==('notify',3,0x0005000500070002)
    results.append({'case':name,'result':hex(ret),'trace':list(trace)})
for mode in [2,3,(1<<64)-1]:
    output=ctypes.create_string_buffer(b'Z'*32,32)
    assert fn(mode,output)<0 and output.raw==b'Z'*32
assert fn(1,None)<0
report={'reference':'1400268d0 -> 14002680c','reference_sha256':o.pe.sha256,
 'cases':results,'invalid_builder_cases':4,'passed':True,'hardware_written':False,
 'scope':'RPC helpers modeled; no update query, downloads, or imported OS code executed',
 'linux_difference':'Fail startup if the package record cannot be queued; reference ignores notify failure'}
(ROOT/'reports/package-announcement-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'reference_cases':len(results),'invalid_builder_cases':4,'passed':True}))
