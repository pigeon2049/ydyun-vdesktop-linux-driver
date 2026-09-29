#!/usr/bin/env python3
"""Check the exact retained experimental session; optionally load its copy bridge.

Does not initialize a new session, change mappings, trigger resets or execute
GPU jobs. Refuses incompatible/absent owners instead of attempting recovery.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SYS = Path('/sys/bus/pci/devices/0000:00:0e.0')
OWNERS = {
    'mt_guest_probe': 'c1c97955663a86d3f56703e69cfcfb799f7c1bbdd1552544c8238a9902b1a662',
    'mt_live_tqx': '3d2f118a168db68bc4a7edd9276fae6e0cfd1e7deecc96d30045f3343e4dc87d',
}
BRIDGE = ROOT / 'build/r30-live/mt_live_copy_bridge.ko'
BRIDGE_SHA = '6a71ba1dca9223f5581009541e281dea93844ad93a66ae671e5881a67dca1cd1'


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def live_build_id(name):
    data = (Path('/sys/module') / name / 'notes/.note.gnu.build-id').read_bytes()
    namesz, descsz, note_type = struct.unpack_from('<III', data)
    require(note_type == 3 and data[12:12+namesz] == b'GNU\0', 'Invalid build-id note')
    offset = 12 + ((namesz + 3) & ~3)
    result = data[offset:offset+descsz]
    require(len(result) == descsz, 'Truncated build-id')
    return result.hex()


def candidate_build_id(path):
    output = subprocess.check_output(['readelf', '-n', str(path)], text=True)
    matches = re.findall(r'Build ID: ([0-9a-f]+)', output)
    require(len(matches) == 1, f'Missing or ambiguous build-id: {path}')
    return matches[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--load', action='store_true', help='load the verified bridge if absent')
    args = parser.parse_args()
    require((SYS / 'driver').resolve(strict=True).name == 'mt_guest_probe', 'Retained driver absent')
    require((SYS / 'vendor').read_text().strip() == '0x1ed5' and
            (SYS / 'device').read_text().strip() == '0x0222', 'Unexpected PCI identity')
    verified = {}
    for name, digest in OWNERS.items():
        path = ROOT / 'build/r28-live' / (name + '.ko')
        require(hashlib.sha256(path.read_bytes()).hexdigest() == digest, f'{name} saved binary changed')
        expected = candidate_build_id(path)
        require(live_build_id(name) == expected, f'{name} loaded ABI differs')
        verified[name] = expected
    params = Path('/sys/module/mt_live_tqx/parameters')
    for name, expected in [('prepared','Y'),('submitted','Y'),('verified','Y'),('result','0')]:
        require((params / name).read_text().strip() == expected, f'Retained context {name} mismatch')
    require(not Path('/sys/module/mt_live_tqx_repeat').exists(), 'Repeat experiment is still loaded')
    runtime = (SYS / 'mt_guest/runtime').read_text().strip()
    completion = (SYS / 'mt_guest/completions').read_text().strip()
    require('guest=2 firmware=2 started=1 event_result=0' in runtime, 'Firmware not active')
    require(completion.startswith('pending=0 ') and
            completion.endswith('submit_enabled=0 workload_submit=0'), 'Submission store not idle')
    require(hashlib.sha256(BRIDGE.read_bytes()).hexdigest() == BRIDGE_SHA, 'Bridge binary changed')
    existing = Path('/sys/module/mt_live_copy_bridge').exists()
    if args.load and not existing:
        require(os.geteuid() == 0, '--load requires root')
        subprocess.run(['insmod', str(BRIDGE), 'enable=1'], check=True)
        existing = True
    if existing:
        require(live_build_id('mt_live_copy_bridge') == candidate_build_id(BRIDGE), 'Loaded bridge differs')
        require(Path('/dev/mt-vgpu-copy').is_char_device(), 'Copy device missing')
    print(json.dumps({'owners': verified, 'runtime': runtime, 'completions': completion,
                      'bridge_loaded': existing, 'device': '/dev/mt-vgpu-copy' if existing else None,
                      'gpu_jobs_executed_by_loader': 0}, indent=2))


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(f'Copy bridge unavailable: {error}')
