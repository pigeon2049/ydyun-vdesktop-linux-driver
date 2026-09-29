#!/usr/bin/env python3
"""Offline verification of direct-VRAM GEM copies, real sync_file receipts and roots."""
from pathlib import Path
import collections
import hashlib
import importlib.util
import json
import struct

ROOT = Path(__file__).resolve().parents[1]
SAVED = ROOT / 'build/r31-live'


def records(name):
    return [json.loads(x) for x in (SAVED / name).read_text().splitlines()]


def main():
    smoke, exercise, switched = [records(x) for x in ('smoke.jsonl','exercise.jsonl','switch-back.jsonl')]
    copies = smoke[:-1] + exercise[:-1] + switched[:-1]
    expected = [(256,0,0,133),(256,0,0,134),(65536,0,0,135),(32768,0,0,136),
                (8192,4096,8192,137),(8207,3,17,138),(4097,4095,12289,139),
                (31,65505,65505,140),(256,0,0,158)]
    assert len(copies) == len(expected) == 9
    for receipt, e in zip(copies, expected):
        assert tuple(receipt[k] for k in ['copy_bytes','source_offset','destination_offset','sequence']) == e
        assert receipt['data_and_guards'] and receipt['native_gpu_sync_file']
    q = json.loads((SAVED / 'query-after.json').read_text())
    assert q == {'abi':1,'slots':8,'slot_bytes':65536,'leased':0,'retained':1,
                 'faulted':0,'submitted':9,'completed':9,'sequence':158}
    old = json.loads((SAVED / 'old-context-copy.json').read_text())
    assert old == {'bytes':65659,'gpu_jobs':17}
    original = (ROOT / 'build/r30-live/input.bin').read_bytes()
    assert original == (SAVED / 'old-context-output.bin').read_bytes()
    raw = (SAVED / 'drm-root.bin').read_bytes()
    assert len(raw) == 135168 and raw[:8] == b'MTDRMR31'
    root_pa, capacity, used, count = struct.unpack_from('<QIII',raw,8)
    assert (root_pa,capacity,used,count) == (0x606033000,131072,18,20)
    assert struct.unpack_from('<Q',raw,32)[0] == 1
    spec = importlib.util.spec_from_file_location('mmu_audit', ROOT/'scripts/audit-r26-all-mappings.py')
    audit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit)
    audit.TABLE_PA = root_pa
    tables, leaves = audit.walk(raw[4096:])
    old_memory = (SAVED / 'old-context-memory.bin').read_bytes()
    old_root = old_memory[:131072]
    assert old_root == (ROOT/'build/r28-live/tqx-after-root.bin').read_bytes()
    audit.TABLE_PA = 0x60600d000
    _, old_leaves = audit.walk(old_root)
    seen, bindings, private_pages, shared_pages = set(), [], 0, 0
    for i in range(count):
        va,pa,offset,size,flags,vector = struct.unpack_from('<QQIIII',raw,64+i*32)
        assert flags == 0 and offset == 0 and size % 4096 == 0
        addresses = set(range(va,va+size,4096))
        assert not seen & addresses
        seen |= addresses
        if i < 11:
            assert vector == 0
            for address in addresses:
                assert leaves[address] == (pa+address-va,1)
            private_pages += len(addresses)
        else:
            assert all(leaves[address] == old_leaves[address] for address in addresses)
            shared_pages += len(addresses)
        bindings.append({'va':hex(va),'gpu_pa_first_page':hex(pa),'bytes':size,'page_vector':bool(vector)})
    assert seen == set(leaves) and len(tables)==18 and len(leaves)==3461
    assert private_pages==132 and shared_pages==3329
    assert all(not any(raw[4096+off:4096+off+4096]) for off in range(0,131072,4096) if off not in tables)
    runtime = (SAVED/'after-runtime').read_text().strip()
    completions = (SAVED/'after-completions').read_text().strip()
    assert 'guest=2 firmware=2 started=1 event_result=0' in runtime
    assert completions == 'pending=0 completed=158 submit_enabled=0 workload_submit=0'
    state = json.loads((SAVED/'final-state.json').read_text())
    assert state['drm_nodes']['renderD128']['device'].endswith('/0000:00:0e.0')
    assert state['drm_nodes']['card0']['driver'].endswith('/qxl')
    report = {
        'hardware_access_by_verifier':False,
        'boot_id':state['boot_id'], 'drm_query':q, 'verified_drm_copies':copies,
        'two_context_switch':{'old_context_gpu_jobs':17,'new_context_after_switch_sequence':158,'file_equal':True},
        'root_gpu_pa':hex(root_pa),'root_sha256':hashlib.sha256(raw[4096:]).hexdigest(),
        'old_root_unchanged':True,'table_pages':18,'mapped_pages':3461,
        'private_pages_checked_against_backing':private_pages,
        'shared_pages_identical_to_old_context':shared_pages,
        'pte_flags':dict(collections.Counter(flags for _,flags in leaves.values())),
        'bindings':bindings,'runtime':runtime,'completions':completions,
        'module_sha256':hashlib.sha256((SAVED/'mt_live_drm.ko').read_bytes()).hexdigest(),
        'limitations':'Experimental privileged DRM GEM/transfer/syncobj only. Fixed persistent root and eight 64 KiB slots. No OpenGL/Vulkan/desktop rendering, PRIME, mmap, async scheduler or root teardown.',
    }
    (ROOT/'reports/r31-drm-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ['bindings','verified_drm_copies']},indent=2))


if __name__=='__main__':
    main()
