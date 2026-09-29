#!/usr/bin/env python3
"""Recheck saved real-device 1080p receipts, VRAM image, and all retained roots."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
ROOT=Path(__file__).resolve().parents[1];S=ROOT/'build/r34-live'
def records(name):return [json.loads(x) for x in (S/name).read_text().splitlines()]
def main():
 smoke=records('surface-smoke.jsonl');exercise=records('surface-exercise.jsonl')
 assert smoke[-2]==dict(fill_cases=2,copy_cases=1,invalid_cases=2,lease_clear_bytes=8388608)
 assert exercise[-2]==dict(fill_cases=23,copy_cases=2,invalid_cases=2,lease_clear_bytes=8388608)
 receipts=smoke[:-2]+exercise[:-2]
 assert [x['sequence'] for x in receipts]==list(range(232,260))
 fills=[x for x in receipts if x.get('native_fill')];copies=[x for x in receipts if 'copy_bytes' in x]
 assert len(fills)==25 and len(copies)==3
 assert all(x['verified_bytes']==8388608 and x['native_gpu_sync_file'] for x in fills)
 assert all(x['source_and_guards'] and x['native_gpu_sync_file'] for x in copies)
 assert [(x['copy_bytes'],x['source_offset']) for x in copies]==[(4194304,0),(4194304,0),(4100096,4194304)]
 pixels=bytearray(8388608)
 for r in exercise[:-2]:
  if not r.get('native_fill'):continue
  assert (r['width'],r['height'],r['offset'])==(1920,1080,0)
  row=struct.pack('<I',r['color'])*r['rect_width']
  for y in range(r['y'],r['y']+r['rect_height']):
   off=(y*1920+r['x'])*4;pixels[off:off+len(row)]=row
 rgb=bytearray(1920*1080*3)
 rgb[0::3]=pixels[2:8294400:4];rgb[1::3]=pixels[1:8294400:4];rgb[2::3]=pixels[:8294400:4]
 ppm=(S/'gpu-native-1080p.ppm').read_bytes();assert ppm==b'P6\n1920 1080\n255\n'+rgb
 png=(S/'gpu-native-1080p.png').read_bytes();assert png[:8]==b'\x89PNG\r\n\x1a\n' and struct.unpack_from('>II',png,16)==(1920,1080)
 old=records('old-fill-smoke.jsonl');switched=records('new-copy-smoke.jsonl')
 assert old[0]['sequence']==260 and old[0]['data_and_guards'] and old[-1]['sequence']==261
 assert switched[0]['sequence']==262 and switched[0]['data_and_guards'] and switched[0]['native_gpu_sync_file']
 q=switched[-1];assert q==dict(abi=1,slots=8,slot_bytes=8388608,leased=0,retained=1,faulted=0,submitted=29,completed=29,sequence=262)
 for name in ('surface-smoke.stderr','surface-exercise.stderr','old-fill-smoke.stderr','new-copy-smoke.stderr'):
  assert not (S/name).read_bytes()
 raw=(S/'surface-root-after.bin').read_bytes()
 assert raw==(S/'surface-root-before.bin').read_bytes()
 assert len(raw)==135168 and raw[:8]==b'MTDRMR31'
 pa,capacity,used,count=struct.unpack_from('<QIII',raw,8)
 assert (pa,capacity,used,count)==(0x60617b000,131072,26,20)
 assert struct.unpack_from('<Q',raw,32)[0]==3
 assert (S/'old-drm-root.bin').read_bytes()==(ROOT/'build/r31-live/drm-root.bin').read_bytes()
 assert (S/'old-graphics-root.bin').read_bytes()==(ROOT/'build/r32-live/graphics-root.bin').read_bytes()
 oldroot=(S/'old-context-memory.bin').read_bytes()[:131072]
 assert oldroot==(ROOT/'build/r28-live/tqx-after-root.bin').read_bytes()
 spec=importlib.util.spec_from_file_location('mmu',ROOT/'scripts/audit-r26-all-mappings.py')
 mmu=importlib.util.module_from_spec(spec);spec.loader.exec_module(mmu)
 mmu.TABLE_PA=pa;tables,leaves=mmu.walk(raw[4096:])
 mmu.TABLE_PA=0x60600d000;_,oldleaves=mmu.walk(oldroot)
 seen=set();private=shared=0;bindings=[]
 for i in range(count):
  va,backing,offset,size,flags,vector=struct.unpack_from('<QQIIII',raw,64+32*i)
  assert offset==flags==0 and size%4096==0
  pages=set(range(va,va+size,4096));assert not seen&pages;seen|=pages
  if i<11:
   assert vector==0 and 0x605800000<=backing and backing+size<=0x607000000
   assert all(leaves[a]==(backing+a-va,1) for a in pages);private+=len(pages)
   if i>=3:
    j=i-3;assert va==0x41000000+j*0x1000000
    assert size==(8388608 if j==0 else 4194304 if j==1 else 65536)
  else:
   assert all(leaves[a]==oldleaves[a] for a in pages);shared+=len(pages)
  bindings.append(dict(va=hex(va),pa=hex(backing),bytes=size))
 assert (len(tables),len(leaves),private,shared)==(26,6501,3172,3329) and seen==set(leaves)
 assert all(not any(raw[4096+o:4096+o+4096]) for o in range(0,131072,4096) if o not in tables)
 session=json.loads((S/'final-session.json').read_text())
 assert session['graphics']['completed']==29 and session['graphics']['sequence']==262
 assert session['completions']=='pending=0 completed=262 submit_enabled=0 workload_submit=0'
 assert 'guest=2 firmware=2 started=1 event_result=0' in session['runtime']
 assert session['identities']['mt_live_surface']=='3962d291ed95b17df92f870d89315cf6c1776612'
 assert hashlib.sha256((S/'mt_live_surface.ko').read_bytes()).hexdigest()=='fefc8b5957eb6a9999d74c82aa667bac00647af1d25af95990dddbe204ae0d6d'
 r=dict(passed=True,hardware_access_by_verifier=False,boot_id=(S/'boot-id').read_text().strip(),
  new_frontend_native_fills=25,new_frontend_copies=4,old_frontend_fills=1,old_frontend_copies=1,
  native_1080p_fills=23,full_surface_readback_verified_bytes_per_fill=8388608,
  image=dict(path=str(S/'gpu-native-1080p.png'),width=1920,height=1080,
   all_pixels_match_native_gpu_fill_receipts=True,ppm_sha256=hashlib.sha256(ppm).hexdigest()),
  pre_and_post_root_identical=True,three_prior_roots_unchanged=True,
  root_pa=hex(pa),root_sha256=hashlib.sha256(raw[4096:]).hexdigest(),table_pages=26,
  mapped_pages=6501,private_pages=private,shared_pages=shared,all_pte_flags=1,
  bindings=bindings,session=session,
  limitations='Private privileged synchronous GEM fill/copy only. One 8 MiB slot, one 4 MiB slot, six 64 KiB slots. No mmap/PRIME, PVR ABI, OpenGL/Vulkan, modesetting or desktop acceleration. All four roots remain retained.')
 (ROOT/'reports/r34-large-surface-validation.json').write_text(json.dumps(r,indent=2)+'\n')
 print(json.dumps({k:v for k,v in r.items() if k not in ('bindings','session')},indent=2))
if __name__=='__main__':main()
