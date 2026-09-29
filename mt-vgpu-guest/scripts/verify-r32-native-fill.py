#!/usr/bin/env python3
"""Offline audit of r32 native clear receipts, GPU readback image and all roots."""
import collections
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
ROOT=Path(__file__).resolve().parents[1]
SAVED=ROOT/'build/r32-live'

def lines(name):
    return [json.loads(s) for s in (SAVED/name).read_text().splitlines()]

def main():
    sets=[lines('fill-'+name+'.jsonl') for name in ('smoke','exercise','demo')]
    all_fills=[]
    for receipts,count,last in zip(sets,(1,40,21),(160,201,223)):
        fills=receipts[:-2]
        assert len(fills)==count and receipts[-2]==dict(fill_cases=count,invalid_cases=13,fill_to_copy=True)
        assert receipts[-1]['sequence']==last and receipts[-1]['faulted']==0
        assert all(r['native_fill'] and r['data_and_guards'] and r['native_gpu_sync_file'] for r in fills)
        assert [r['sequence'] for r in fills]==list(range(last-count,last))
        all_fills+=fills
    # This image is exported from the second GEM after a native GPU copy.
    # Construct an independent expected raster from the native-fill receipts.
    pixels=bytearray(65536)
    for r in sets[2][:-2]:
        assert r['width']==r['height']==128 and r['offset']==0
        for y in range(r['y'],r['y']+r['rect_height']):
            for x in range(r['x'],r['x']+r['rect_width']):
                struct.pack_into('<I',pixels,(y*128+x)*4,r['color'])
    header=b'P6\n128 128\n255\n'
    rgb=bytes(b for i in range(16384) for b in pixels[i*4:i*4+3][::-1])
    ppm=(SAVED/'gpu-native-fill.ppm').read_bytes()
    assert ppm==header+rgb
    old=lines('old-drm-smoke.jsonl');copies=lines('new-drm-copies.jsonl')
    assert old[0]['sequence']==224 and old[0]['data_and_guards']
    assert [r['sequence'] for r in copies[:-1]]==list(range(225,232))
    assert all(r['data_and_guards'] and r['native_gpu_sync_file'] for r in copies[:-1])
    q=json.loads((SAVED/'query-after.json').read_text())
    assert q==dict(abi=1,slots=8,slot_bytes=65536,leased=0,retained=1,faulted=0,submitted=72,completed=72,sequence=231)
    assert (SAVED/'old-drm-root.bin').read_bytes()==(ROOT/'build/r31-live/drm-root.bin').read_bytes()
    old_root=(SAVED/'old-context-memory.bin').read_bytes()[:131072]
    assert old_root==(ROOT/'build/r28-live/tqx-after-root.bin').read_bytes()
    raw=(SAVED/'graphics-root.bin').read_bytes()
    assert len(raw)==135168 and raw[:8]==b'MTDRMR31'
    pa,capacity,used,count=struct.unpack_from('<QIII',raw,8)
    assert (pa,capacity,used,count)==(0x6060d7000,131072,18,20)
    assert struct.unpack_from('<Q',raw,32)[0]==2
    spec=importlib.util.spec_from_file_location('mmu',ROOT/'scripts/audit-r26-all-mappings.py')
    mmu=importlib.util.module_from_spec(spec);spec.loader.exec_module(mmu)
    mmu.TABLE_PA=pa;tables,leaves=mmu.walk(raw[4096:])
    mmu.TABLE_PA=0x60600d000;_,old_leaves=mmu.walk(old_root)
    private=shared=0;seen=set();bindings=[]
    for i in range(count):
        va,backing,offset,size,flags,vector=struct.unpack_from('<QQIIII',raw,64+i*32)
        assert offset==flags==0 and size%4096==0
        addresses=set(range(va,va+size,4096));assert not seen&addresses;seen|=addresses
        if i<11:
            assert vector==0
            assert all(leaves[a]==(backing+a-va,1) for a in addresses)
            private+=len(addresses)
        else:
            assert all(leaves[a]==old_leaves[a] for a in addresses)
            shared+=len(addresses)
        bindings.append(dict(va=hex(va),physical=hex(backing),bytes=size))
    assert len(tables)==18 and len(leaves)==3461 and private==132 and shared==3329 and seen==set(leaves)
    assert all(not any(raw[4096+o:4096+o+4096]) for o in range(0,131072,4096) if o not in tables)
    runtime=(SAVED/'after-runtime').read_text().strip()
    completions=(SAVED/'after-completions').read_text().strip()
    assert 'guest=2 firmware=2 started=1 event_result=0' in runtime
    assert completions=='pending=0 completed=231 submit_enabled=0 workload_submit=0'
    state=json.loads((SAVED/'final-state.json').read_text())
    assert state['drm_nodes']['card0']['driver'].endswith('/qxl')
    assert state['drm_nodes']['renderD129']['device'].endswith('/0000:00:0e.0')
    assert state['modules']['mt_live_graphics']['refcnt']=='1'
    encoding=json.loads((ROOT/'reports/r32-fill-encoding-validation.json').read_text())
    assert encoding['passed'] and encoding['native_fill_cases']==encoding['full_dma_cases']==220
    report=dict(passed=True,hardware_access_by_verifier=False,boot_id=state['boot_id'],
        native_gpu_fills=62,new_frontend_gpu_copies=10,old_frontend_gpu_copies=1,
        invalid_fill_requests_rejected=39,rectangle_cases=sets[1][:-2][:10],
        drm_query=q,runtime=runtime,completions=completions,
        gpu_image=dict(path=str(SAVED/'gpu-native-fill.png'),width=128,height=128,
            all_pixels_match_native_fill_receipts=True,ppm_sha256=hashlib.sha256(ppm).hexdigest()),
        root_gpu_pa=hex(pa),root_sha256=hashlib.sha256(raw[4096:]).hexdigest(),
        both_prior_roots_unchanged=True,table_pages=18,mapped_pages=3461,
        private_pages_verified=private,shared_pages_identical=shared,
        pte_flags=dict(collections.Counter(flags for _,flags in leaves.values())),bindings=bindings,
        module_sha256=hashlib.sha256((SAVED/'mt_live_graphics.ko').read_bytes()).hexdigest(),
        limitations='Native 32-bit linear rectangle fill and copies only. No OpenGL/Vulkan/Mesa, scanout or desktop acceleration. Root-only ioctl, fixed eight 64 KiB slots, synchronous waits, retained root without withdrawal.')
    (ROOT/'reports/r32-native-fill-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('bindings','rectangle_cases')},indent=2))
if __name__=='__main__':main()
