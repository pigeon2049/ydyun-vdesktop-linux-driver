#!/usr/bin/env python3
"""Read the bound recovery session and preserve evidence; never modify a device."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import struct
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
PCI = Path('/sys/bus/pci/devices/0000:00:0e.0')


def command(argv):
    p = subprocess.run(list(map(str, argv)), capture_output=True)
    return {'returncode': p.returncode, 'stdout': p.stdout.decode(errors='replace'),
            'stderr': p.stderr.decode(errors='replace')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=int, default=25, choices=range(0, 61), metavar='0..60')
    args = parser.parse_args()
    if not (PCI / 'mt_guest').is_dir():
        raise SystemExit('No bound mt_guest session; nothing changed')
    parameter = command(['sudo', '-n', 'cat', '/sys/module/mt_guest_probe/parameters/recover_channels'])
    if parameter['returncode'] or parameter['stdout'].strip() != 'Y':
        raise SystemExit('Not a recovery-only session; nothing changed')
    out = ROOT / 'build/recovery-channel' / ('capture-' + uuid.uuid4().hex[:12])
    out.mkdir(parents=True)
    report = {'utc': datetime.now(timezone.utc).isoformat(),
              'boot_id': Path('/proc/sys/kernel/random/boot_id').read_text().strip(),
              'device_access': 'read-only sysfs', 'hardware_acceleration_verified': False,
              'memory_raw_is_probe_time_snapshot': True, 'samples': [], 'files': {}}
    started = time.monotonic()
    while True:
        report['samples'].append({'elapsed_seconds': time.monotonic() - started,
            **{name: (PCI / 'mt_guest' / name).read_text().strip()
               for name in ('connection', 'rpc_service', 'trial', 'vram',
                            'publication', 'retained_status')
               if (PCI / 'mt_guest' / name).exists()}})
        remaining = args.seconds - (time.monotonic() - started)
        if remaining <= 0:
            break
        time.sleep(min(5, remaining))
    for name in ('info_raw', 'channels_raw', 'memory_raw'):
        p = subprocess.run(['sudo', '-n', 'cat', str(PCI / 'mt_guest' / name)], capture_output=True)
        if p.returncode:
            report['files'][name] = {'error': p.stderr.decode(errors='replace')}
            continue
        (out / (name + '.bin')).write_bytes(p.stdout)
        report['files'][name] = {'bytes': len(p.stdout), 'sha256': hashlib.sha256(p.stdout).hexdigest()}
        if name == 'channels_raw' and len(p.stdout) >= 4096:
            report['shared_page0'] = {
                'gpu_normal_raw': p.stdout[0],
                'interrupt_status': struct.unpack_from('<I', p.stdout, 4)[0],
                'interrupt_ack_count': struct.unpack_from('<Q', p.stdout, 8)[0],
                # Keep raw names: these differ from the old Host 2.3 headers.
                'control_10_raw': struct.unpack_from('<Q', p.stdout, 0x10)[0],
                'control_18_raw': struct.unpack_from('<Q', p.stdout, 0x18)[0],
                'snapshot_is_not_atomic_with_host': True,
            }
    report['services'] = command(['systemctl', 'is-active', 'sddm', 'spice-vdagentd', 'tailscaled'])
    report['pci_command'] = command(['sudo', '-n', 'setpci', '-s', '00:0e.0', 'COMMAND'])
    report['drm_nodes'] = sorted(p.name for p in Path('/sys/class/drm').iterdir())
    logs = command(['sudo', '-n', 'dmesg'])
    (out / 'kernel.log').write_text(logs['stdout'])
    report['kernel_irq_errors'] = [line for line in logs['stdout'].splitlines()
        if 'nobody cared' in line or 'Disabling IRQ #10' in line]
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(out / 'report.json')
    print(json.dumps(report['samples'][-1], indent=2))


if __name__ == '__main__':
    main()
