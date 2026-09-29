#!/usr/bin/env python3
"""Verify saved r30 syscall receipts, final GPU bytes and runtime state offline."""
from pathlib import Path
import hashlib
import json
import struct

ROOT = Path(__file__).resolve().parents[1]
SAVED = ROOT / 'build/r30-live'

def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    syscall = json.loads((ROOT / 'reports/r30-user-check.json').read_text())
    query = json.loads((SAVED / 'query-after.json').read_text())
    file_copy = json.loads((SAVED / 'file-copy.json').read_text())
    assert syscall == {'invalid_or_unauthorized_rejected': 14, 'concurrent_processes': 4,
                       'verified_copies': 32, 'copyout_fault_after_gpu_completion': True,
                       'submitted_delta': 33, 'completed_delta': 33, 'last_sequence': 115}
    assert query == {'abi': 1, 'max_bytes': 4096, 'capabilities': 3, 'faulted': 0,
                     'submitted': 50, 'completed': 50, 'last_sequence': 132}
    assert file_copy == {'bytes': 65659, 'gpu_jobs': 17}
    original, output = [(SAVED / name).read_bytes() for name in ('input.bin', 'output.bin')]
    assert original == output and len(original) == 65659
    snapshot = (SAVED / 'tqx-memory.bin').read_bytes()
    assert len(snapshot) == 0x26000
    root = snapshot[:0x20000]
    assert root == (ROOT / 'build/r28-live/tqx-after-root.bin').read_bytes()
    final = original[65536:]
    assert snapshot[0x21000:0x22000] == final.ljust(4096, b'\0')
    assert snapshot[0x22000:0x23000] == final.ljust(4096, b'\xa5')
    fw = (SAVED / 'after-trial_firmware').read_bytes()
    cursor = 0x6ddd0 + 0x2e30 + 0x2e00
    pairs = [[struct.unpack_from('<I', fw, cursor + i*16 + j)[0] for j in (0,8)] for i in range(3)]
    assert pairs == [[4,4], [0,0], [4,4]]
    runtime = (SAVED / 'after-runtime').read_text().strip()
    completions = (SAVED / 'after-completions').read_text().strip()
    assert 'guest=2 firmware=2 started=1 event_result=0' in runtime
    assert completions == 'pending=0 completed=132 submit_enabled=0 workload_submit=0'
    report = {
        'boot_id': 'fe18deeb-d431-4c03-ac88-34235e9849c5',
        'hardware_access_by_verifier': False,
        'user_checks': syscall, 'bridge_query': query,
        'file_copy': file_copy, 'input_output_match': True,
        'file_sha256': sha(original),
        'final_gpu_source_and_target_verified': True,
        'root_unchanged': True, 'root_sha256': sha(root),
        'dm1_ring_pairs': pairs, 'runtime': runtime, 'completions': completions,
        'artifact_sha256': {name: sha((SAVED / name).read_bytes()) for name in
                           ['mt_live_copy_bridge.ko','mt-copy','mt-copy-check','tqx-memory.bin']},
        'limits': 'Privileged synchronous staging-copy interface on retained fixed VM; multiple callers serialized, not concurrent GPU execution. File copied in 17 one-page jobs. No DRM/UMD, graphics, zero-copy, large single transfer, reset or root teardown validation.',
    }
    (ROOT / 'reports/r30-copy-validation.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
