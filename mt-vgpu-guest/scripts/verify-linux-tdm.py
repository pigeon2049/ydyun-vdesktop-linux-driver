#!/usr/bin/env python3
"""Execute both original binaries and validate a bounded Linux->Guest normalizer."""
import ctypes as C
from datetime import datetime, timezone
import hashlib
import json
import random
import struct
import subprocess
from pathlib import Path
from linux_submission_reference import LinuxSubmissionOracle
from tqx_fill_reference import FillOracle
from tqx_dma_reference import DmaOracle
ROOT=Path(__file__).resolve().parents[1]
class WindowsOracle(FillOracle,DmaOracle): pass
class Source(C.Structure):
    _fields_=[('record',C.c_ubyte*296),('page_record',C.c_ubyte*16),('root_export',C.c_ubyte*16)]
class Input(C.Structure):
    _fields_=[('dma_va',C.c_uint64),('state_va',C.c_uint64),('cores',C.c_uint32)]
class Image(C.Structure):
    _fields_=[('bytes',C.c_uint32),('reserved',C.c_uint32),('descriptor',C.c_ubyte*8192),('view',C.c_ubyte*24)]
output=ROOT/'build/r33-linux-tdm';output.mkdir(exist_ok=True)
libpath=output/'normalizer.so'
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-shared','-fPIC',
    ROOT/'tests/linux_tdm_oracle_wrapper.c','-o',libpath],check=True)
lib=C.CDLL(str(libpath))
lib.normalize.argtypes=[C.c_void_p,C.c_uint32,C.c_void_p,C.c_uint32,C.c_uint64,C.c_uint64,C.POINTER(Source),C.POINTER(Input)]
lin=LinuxSubmissionOracle();win=WindowsOracle();rng=random.Random(0x151b20)
# These are table fixtures from the ELF, not the live machine's BVNC.
bvncs=[lin.get64(0x54da60+i*24) for i in range(19)]
selectors=[]
for bvnc in bvncs:
    for major in (0,1,2,3):
        mode=lin.select_protocol(major,bvnc)
        assert mode==(2 if major==2 else 1)
        selectors.append(dict(bvnc=hex(bvnc),drm_major=major,mode=mode))
counts={'fill':0,'copy':0};core_cases={}
for i in range(80):
    command=0x40000000+rng.randrange(4096)*4096
    dma=0x50000000+rng.randrange(4096)*4096
    state=0x60000000+rng.randrange(4096)*4096
    native_dma=0x70000000+rng.randrange(4096)*4096
    native_array=0x80000000+rng.randrange(4096)*128
    common=dict(command_va=command,shader_base=rng.randrange(1<<20)*128,
        pds_base=rng.randrange(1<<20)*16,state_base=rng.randrange(1<<20)*16)
    if i%2:
        r=win.run(0x100000001+i,0x200000003,1,1,1,copy_bytes=[1,65536,8294400,33554432,0xffffffff][i%5],**common)
        kind='copy'
    else:
        w,h=[rng.randrange(1,32769) for _ in range(2)]
        x,y=rng.randrange(w),rng.randrange(h)
        r=win.fill(0x200000000,4,w,h,[rng.getrandbits(32),0,0,0],
            (x,y,rng.randrange(1,w-x+1),rng.randrange(1,h-y+1)),**common)
        kind='fill'
    src=Source.from_buffer_copy(r['record']+r['page_record']+r['root_export'])
    din=Input(dma,state,1+(i//2)%8)
    d=win.serialize(dma,state,din.cores)
    padded,code,initial,flags=struct.unpack_from('<4I',r['record'],0x30)
    native=lin.tdm(command,native_array,state,padded,code,initial,flags,native_dma,din.cores)
    regs=bytearray(native['registers']);struct.pack_into('<Q',regs,8,dma+0x200)
    assert bytes(regs)==d['data'][0x160:0x188]
    assert native['packet'][0x1190:]==native['registers']
    packet=C.create_string_buffer(native['packet'])
    out=(C.c_ubyte*(C.sizeof(Image)+16))();C.memset(out,0xa5,C.sizeof(out))
    ret=lib.normalize(out,C.sizeof(out),packet,len(native['packet']),native_dma,native_array,C.byref(src),C.byref(din))
    assert ret==0,(i,ret,native['packet'].hex())
    normalized=Image.from_buffer_copy(out)
    assert normalized.bytes==len(d['data']) and normalized.reserved==0
    assert bytes(normalized.descriptor)==d['data']+bytes(8192-len(d['data']))
    assert bytes(normalized.view)==d['view'] and bytes(out)[C.sizeof(Image):]==b'\xa5'*16
    counts[kind]+=1;core_cases[din.cores]=core_cases.get(din.cores,0)+1
    if i<2:
        for label,data in [('native',native['packet']),('guest',d['data']),('command',r['command'])]:
            (output/f'{kind}-{label}.bin').write_bytes(data)
# Every byte, including optional fields and register tails, is accounted for.
saved=bytes(out);bad=0
for offset in range(len(native['packet'])):
    altered=bytearray(native['packet']);altered[offset]^=1
    b=C.create_string_buffer(bytes(altered))
    assert lib.normalize(out,C.sizeof(out),b,len(altered),native_dma,native_array,C.byref(src),C.byref(din))<0,hex(offset)
    assert bytes(out)==saved
    bad+=1
for n in (0,0x11b7,0x11b9):
    assert lib.normalize(out,C.sizeof(out),packet,n,native_dma,native_array,C.byref(src),C.byref(din))<0
    assert bytes(out)==saved;bad+=1
for addr,array in ((native_dma+8,native_array),(native_dma,native_array+128),(1<<40,native_array),(native_dma,native_array+1)):
    assert lib.normalize(out,C.sizeof(out),packet,0x11b8,addr,array,C.byref(src),C.byref(din))<0
    assert bytes(out)==saved;bad+=1
assert lib.normalize(out,C.sizeof(Image)-1,packet,0x11b8,native_dma,native_array,C.byref(src),C.byref(din))<0
assert bytes(out)==saved;bad+=1
shapes={}
for region,n in ((2,0x210),(3,0x28),(4,0x80),(5,0x210)):
    marker=bytes((i*29+7)&255 for i in range(n))
    b=lin.shape(region,marker)
    assert b[-n:]==marker
    shapes[region]=dict(total_bytes=len(b),opaque_register_bytes=n,sha256=hashlib.sha256(b).hexdigest())
    (output/f'region-{region}-shape-only.bin').write_bytes(b)
report=dict(utc=datetime.now(timezone.utc).isoformat(),passed=True,
    linux_reference_sha256=lin.sha256,windows_reference_sha256=win.x.pe.sha256,
    protocol_selection_cases=len(selectors),selectors=selectors,normalization_cases=counts,
    full_guest_packet_byte_equality=True,core_counts=core_cases,invalid_packets_rejected=bad,
    native_tdm_bytes=0x11b8,native_header_bytes=0x58,guest_header_bytes=0x48,
    register_tail_bytes=40,other_region_serialization_shapes=shapes,
    modeled=['core count and known-table BVNC supplied as fixtures, not hardware discovery',
        'DRM version queried through explicit RAM hook',
        'RAM allocators, libc memory operations and GPU-allocation metadata',
        'fresh zeroed single kick; no synchronization, PID, job ID or nonzero CSW'],
    unimplemented_tls_relocations=lin.tls_relocations,
    hardware_access=False,gpu_execution=False,
    limits='Normalizer is not connected to a device ioctl or used by the installed UMD. No PVR bridge, GL/Vulkan, GFX register translation or desktop acceleration is provided.')
(ROOT/'reports/r33-linux-tdm-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='selectors'},indent=2))
