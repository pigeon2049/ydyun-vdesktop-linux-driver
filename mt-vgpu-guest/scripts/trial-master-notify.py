#!/usr/bin/env python3
"""Bounded, once-recorded MASTER + existing queue notification, with rollback."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
PCI = Path('/sys/bus/pci/devices/0000:00:0e.0')
REPORT = ROOT / 'reports/pci-master-notify-hardware.json'
MODULE = ROOT / 'kernel/recovery/mt_master_notify.ko'
LOADED = ROOT / 'build/recovery-channel/loaded-6f2599bec31bc199bd0dcce1bbb0caa61a022aecbe8aa92294ee3e7b87d1553b.ko'


def run(*args):
    return subprocess.check_output(list(map(str, args)), text=True).strip()


def sample():
    result = {name: (PCI / 'mt_guest' / name).read_text().strip()
              for name in ('connection', 'rpc_service', 'retained_status', 'trial', 'publication')}
    if (PCI / 'mt_master_notify/status').exists():
        result['helper'] = (PCI / 'mt_master_notify/status').read_text().strip()
    result['command'] = run('sudo', '-n', 'setpci', '-s', '0000:00:0e.0', 'COMMAND.w')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', action='store_true')
    args = parser.parse_args()
    if (PCI / 'driver').resolve().name != 'mt_guest_probe':
        raise SystemExit('Wrong bound driver')
    if run('sudo', '-n', 'cat', '/sys/module/mt_guest_probe/parameters/recover_channels') != 'Y':
        raise SystemExit('Wrong recovery mode')
    note = ROOT / 'build/recovery-channel/master-loaded-build-id.bin'
    run('objcopy', '--dump-section', f'.note.gnu.build-id={note}', LOADED, '/dev/null')
    if note.read_bytes() != Path('/sys/module/mt_guest_probe/notes/.note.gnu.build-id').read_bytes():
        raise SystemExit('Loaded module does not match the audited backup')
    abi = run('pahole', '-C', 'mt_guest', LOADED)
    if abi != run('pahole', '-C', 'mt_guest', MODULE):
        raise SystemExit('Helper ABI mismatch')
    if Path('/sys/module/mt_master_notify').exists():
        raise SystemExit('Helper already loaded')
    before = sample()
    if before['command'] != '0003' or 'running=1 ready=1' not in before['rpc_service']:
        raise SystemExit('Unexpected PCI/service state')
    if not args.run:
        print('Preflight passed; --run performs one notification experiment with automatic 20-second rollback')
        return
    if REPORT.exists():
        raise SystemExit('Experiment record exists; no repeated attempt')
    report = dict(utc=datetime.now(timezone.utc).isoformat(), before=before, samples=[],
                  boot_id=Path('/proc/sys/kernel/random/boot_id').read_text().strip(),
                  module_sha256=hashlib.sha256(MODULE.read_bytes()).hexdigest(),
                  loaded_build_id=note.read_bytes().hex(), abi_matched=True,
                  scope='Guest MASTER + BAR1 0x148 and BAR0 0xb00; no reset or queue append',
                  attempted=False)

    def save():
        REPORT.write_text(json.dumps(report, indent=2) + '\n')

    save()
    loaded = False
    try:
        run('sudo', '-n', 'insmod', MODULE, 'enable=1')
        loaded = True
        report['readonly_preflight'] = sample()
        if report['readonly_preflight']['helper'] != 'attempted=0 owns_master=0 command=0003':
            raise RuntimeError('Unexpected helper preflight')
        report['attempted'] = True
        save()
        subprocess.run(['sudo', '-n', 'tee', str(PCI / 'mt_master_notify/control')],
                       input=b'enable-and-notify\n', stdout=subprocess.DEVNULL, check=True)
        start = time.monotonic()
        for target in (0, 5, 10, 15, 22):
            time.sleep(max(0, target - (time.monotonic() - start)))
            report['samples'].append(dict(elapsed=time.monotonic() - start, **sample()))
            save()
        report['automatic_rollback_verified'] = report['samples'][-1]['helper'] == 'attempted=1 owns_master=0 command=0003'
        report['firmware_progress'] = any(s['retained_status'] != before['retained_status'] for s in report['samples'])
    except BaseException as exc:
        report['error'] = repr(exc)
        raise
    finally:
        try:
            if loaded:
                run('sudo', '-n', 'rmmod', 'mt_master_notify')
            report['helper_unloaded'] = not Path('/sys/module/mt_master_notify').exists()
            report['after'] = sample()
            report['services'] = run('systemctl', 'is-active', 'sddm', 'spice-vdagentd', 'tailscaled')
            report['recent_dmesg'] = run('sudo', '-n', 'dmesg', '--since', '5 minutes ago')
        finally:
            save()
    print(REPORT)
    print(json.dumps({k: report[k] for k in ('automatic_rollback_verified', 'firmware_progress', 'helper_unloaded', 'services')}, indent=2))


if __name__ == '__main__':
    main()
