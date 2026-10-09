#!/usr/bin/env python3
"""Check prerequisites, or explicitly run one retained-evidence firmware trial.

Never resets, unbinds, unloads, reboots, or installs startup configuration.
--run loads a module that enables PCI BusMaster for the trial, uploads firmware,
and may retain a pinned module if disconnect is not acknowledged.
An unbound device in Guest=0/FW=1 is necessary, not proof of Host reset success.
"""
import argparse
from datetime import datetime, timezone
import fcntl
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[1]
PCI = Path('/sys/bus/pci/devices/0000:00:0e.0')
MODULE = ROOT / 'kernel/mt_guest_probe.ko'
FIRMWARE = Path('/lib/firmware/mt-vgpu-guest/gen1-guest-loader.bin')
def parameters(runtime_context):
    return ['enable_probe=1', 'query_info=1', 'probe_rpc=1',
            'reserve_memory=1', 'prepare_resources=1',
            'load_firmware=1', 'trial_connect=1',
            f'runtime_context={int(runtime_context)}']


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def command(argv):
    p = subprocess.run(argv, capture_output=True)
    return {'argv': list(map(str, argv)), 'returncode': p.returncode,
            'stdout': p.stdout.decode(errors='replace'),
            'stderr': p.stderr.decode(errors='replace')}


def preflight(runtime_context=False):
    report = {'utc': datetime.now(timezone.utc).isoformat(),
              'kernel': platform.release(), 'parameters': parameters(runtime_context),
              'runtime_context_requested': runtime_context,
              'insmod_invoked': False, 'hardware_acceleration_verified': False}
    errors = []
    for attr, expected in (('vendor', '0x1ed5'), ('device', '0x0222'),
                           ('subsystem_vendor', '0x1ed5'), ('subsystem_device', '0x1101')):
        actual = (PCI / attr).read_text().strip() if (PCI / attr).exists() else None
        report[attr] = actual
        if actual != expected:
            errors.append(f'{attr}: expected {expected}, found {actual}')
    report['driver'] = (PCI / 'driver').resolve().name if (PCI / 'driver').is_symlink() else None
    report['loaded_modules'] = [name for name in ('mt_guest_probe', 'mt_live_service', 'mt_irq_recover')
                                if Path('/sys/module', name).exists()]
    if report['driver'] or report['loaded_modules']:
        errors.append('Existing driver/module session must remain intact; no replacement attempted')
    for name in ('trial', 'connection', 'rpc_service'):
        p = PCI / 'mt_guest' / name
        if p.exists():
            report['current_' + name] = p.read_text()
    if not MODULE.is_file():
        errors.append('Build kernel/mt_guest_probe.ko first')
    else:
        report['module_sha256'] = digest(MODULE)
        result = command([shutil.which('modinfo') or '/usr/sbin/modinfo', '-F', 'vermagic', MODULE])
        report['vermagic'] = result
        if result['returncode'] or result['stdout'].split()[:1] != [platform.release()]:
            errors.append('Module kernel version does not match running kernel')
        audit = json.loads((ROOT / 'reports/runtime-integration-build.json').read_text())
        if report['module_sha256'] != audit['module_sha256']:
            errors.append('Module differs from the tested runtime integration build; rerun verify-runtime-integration.py')
    expected_fw = '35d40f75099aaf7ff74040736ee7ee34e09e26f3556b7ea9040fa143bad9cee9'
    report['firmware_sha256'] = digest(FIRMWARE) if FIRMWARE.is_file() else None
    if report['firmware_sha256'] != expected_fw:
        errors.append('Installed firmware is missing or differs from the verified loader image')
    # Avoid concurrent BAR reads while any driver owns this PCI function.
    if not errors:
        state = command(['sudo', '-n', ROOT / 'build/probe/mt-status', '--read-status'])
        report['unbound_state_read'] = state
        if state['returncode']:
            errors.append('Could not read unbound device state')
        else:
            data = json.loads(state['stdout'])
            if (data['driver_state'], data['firmware_state']) not in ((0, 1), (2, 1)):
                errors.append('Device must report Guest=0/2 and FW=READY(1)')
    report['errors'] = errors
    report['eligible_for_trial'] = not errors
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', action='store_true', help='upload and attempt exactly one connect/disconnect trial')
    parser.add_argument('--runtime-context', action='store_true',
                        help='retain a successful connection and publish its runtime context')
    args = parser.parse_args()
    trials = ROOT / 'build/fresh-trials'
    trials.mkdir(parents=True, exist_ok=True)
    with (trials / '.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        report = preflight(args.runtime_context)
        if args.run and report['eligible_for_trial']:
            out = trials / (datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ-') + uuid.uuid4().hex[:8])
            out.mkdir(mode=0o700)
            (out / 'preflight.json').write_text(json.dumps(report, indent=2) + '\n')
            # The module repeats identity/state checks during probe. Do not
            # time out insmod or interpret a failing result as safe to unload.
            report['insmod_invoked'] = True
            report['load'] = command(['sudo', '-n', shutil.which('insmod') or '/usr/sbin/insmod',
                                      MODULE, *report['parameters']])
            report['evidence_directory'] = str(out)
            report['snapshots'] = {}
            report['trial_connection_verified'] = False
            report['trial_completed_and_restored'] = False
            for name in ('trial', 'runtime', 'connection', 'rpc_service', 'bootstrap', 'vram',
                         'info_raw', 'channels_raw', 'firmware_image', 'trial_firmware',
                         'trial_backups', 'trial_events', 'bootstrap_raw'):
                p = subprocess.run(['sudo', '-n', 'cat', PCI / 'mt_guest' / name], capture_output=True)
                if p.returncode:
                    report['snapshots'][name] = {'error': p.stderr.decode(errors='replace')}
                else:
                    (out / name).write_bytes(p.stdout)
                    report['snapshots'][name] = {'bytes': len(p.stdout),
                                                'sha256': hashlib.sha256(p.stdout).hexdigest()}
                    if name == 'trial':
                        fields = dict(item.split('=', 1) for item in p.stdout.decode().splitlines()[0].split())
                        report['trial_connection_verified'] = fields.get('connected') == '1'
                        report['trial_completed_and_restored'] = all(fields.get(key) == value for key, value in
                            (('connected', '1'), ('result', '0'), ('restored', '1'), ('pinned', '0')))
                        report['trial_fields'] = fields
                    elif name == 'runtime':
                        fields = dict(item.split('=', 1) for item in p.stdout.decode().splitlines()[0].split())
                        report['runtime_context_published'] = (
                            fields.get('published') == '1' and fields.get('result') == '0')
                        report['runtime_fields'] = fields
            report['kernel_log'] = command(['sudo', '-n', 'dmesg', '--color=never'])
            report['services'] = command(['systemctl', 'is-active', 'sddm', 'spice-vdagentd', 'tailscaled'])
            report['finished_utc'] = datetime.now(timezone.utc).isoformat()
            (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=2))
        if report['errors']:
            return 2
        if report['insmod_invoked']:
            # Loading a module is not connection success. Leave diagnosis to
            # the recorded trial fields; resources are deliberately retained.
            if args.runtime_context:
                return report['load']['returncode'] or (
                    0 if report.get('trial_connection_verified') and
                    report.get('runtime_context_published') else 1)
            return report['load']['returncode'] or (0 if report['trial_completed_and_restored'] else 1)
        return 0


if __name__ == '__main__':
    raise SystemExit(main())
