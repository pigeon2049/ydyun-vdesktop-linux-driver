#!/usr/bin/env python3
"""Read-only identity and capability check of the retained graphics sessions."""
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('identity',ROOT/'scripts/load-copy-bridge.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
paths={
    'mt_guest_probe':('r28-live',m.OWNERS['mt_guest_probe']),
    'mt_live_tqx':('r28-live',m.OWNERS['mt_live_tqx']),
    'mt_live_drm':('r31-live','4f3db3739a695dd876483e900314b89e4a9f78f83a73746b394f895c372de484'),
    'mt_live_graphics':('r32-live','f37057d399405e7ee8b9bdc2c5320bf10027aee947227e32d27618c934b22ef5'),
}
surface=Path('/sys/module/mt_live_surface').exists()
if surface:
    paths['mt_live_surface']=('r34-live','fefc8b5957eb6a9999d74c82aa667bac00647af1d25af95990dddbe204ae0d6d')
ids={}
for name,(folder,digest) in paths.items():
    binary=ROOT/'build'/folder/(name+'.ko')
    m.require(hashlib.sha256(binary.read_bytes()).hexdigest()==digest,f'{name} binary changed')
    ids[name]=m.candidate_build_id(binary)
    m.require(m.live_build_id(name)==ids[name],f'{name} loaded build differs')
runtime=(m.SYS/'mt_guest/runtime').read_text().strip()
completions=(m.SYS/'mt_guest/completions').read_text().strip()
m.require('guest=2 firmware=2 started=1 event_result=0' in runtime,'Firmware is not active')
m.require(completions.startswith('pending=0 '),'GPU jobs are pending')
found=[]
for node in Path('/sys/class/drm').glob('renderD*'):
    if (node/'device').resolve()!=m.SYS.resolve():continue
    fd=os.open('/dev/dri/'+node.name,os.O_RDWR|os.O_CLOEXEC)
    try:
        raw=bytearray(56)
        # DRM_IOR(DRM_COMMAND_BASE+0, drm_mt_query), 56-byte ABI 1.
        fcntl.ioctl(fd,0x80386440,raw,True)
        abi,slots,size,leased,faulted,retained,caps,reserved,submitted,completed,sequence=struct.unpack('<8I3Q',raw)
        if caps&2:
            m.require(abi==1 and slots==8 and size in (65536,8388608) and caps==3 and reserved==0,'Unexpected ABI')
            m.require(not faulted and not leased and submitted==completed,'Graphics device not idle')
            found.append(dict(device='/dev/dri/'+node.name,capabilities=['copy','native_fill'],max_object_bytes=size,
                submitted=submitted,completed=completed,sequence=sequence,retained=bool(retained)))
    finally:os.close(fd)
m.require(sorted(x['max_object_bytes'] for x in found)==([65536,8388608] if surface else [65536]),'Unexpected native-fill front ends')
found.sort(key=lambda x:x['max_object_bytes'],reverse=True)
print(json.dumps(dict(identities=ids,graphics=found[0],all_graphics=found,runtime=runtime,completions=completions,
                     gpu_jobs_executed_by_checker=0),indent=2))
