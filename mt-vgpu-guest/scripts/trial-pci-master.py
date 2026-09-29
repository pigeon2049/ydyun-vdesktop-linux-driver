#!/usr/bin/env python3
"""One bounded Guest PCI COMMAND bit-2 experiment; default is read-only."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
PCI = Path('/sys/bus/pci/devices/0000:00:0e.0')
REPORT = ROOT / 'reports/pci-master-hardware.json'


def run(*args):
    return subprocess.check_output(args, text=True).strip()


def snapshot():
    return {name: (PCI / 'mt_guest' / name).read_text().strip()
            for name in ('connection', 'rpc_service', 'retained_status', 'trial', 'publication')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', action='store_true')
    args = parser.parse_args()
    expected = {'vendor': '0x1ed5', 'device': '0x0222',
                'subsystem_vendor': '0x1ed5', 'subsystem_device': '0x1101'}
    for name, value in expected.items():
        if (PCI / name).read_text().strip() != value:
            raise SystemExit('Unexpected PCI identity; unchanged')
    if (PCI / 'driver').resolve().name != 'mt_guest_probe':
        raise SystemExit('Unexpected driver; unchanged')
    if run('sudo', '-n', 'cat', '/sys/module/mt_guest_probe/parameters/recover_channels') != 'Y':
        raise SystemExit('Not recovery-only; unchanged')
    command = run('sudo', '-n', 'setpci', '-s', '0000:00:0e.0', 'COMMAND.w')
    if int(command, 16) != 3:
        raise SystemExit('Expected COMMAND=0003; unchanged')
    before = snapshot()
    expected_status = ['guest=1 firmware=1 started=0'] + [
        f'dm={dm} ring={ring} head={5 if dm == ring == 0 else 0} tail=0'
        for dm in range(6) for ring in range(3)]
    if before['retained_status'].splitlines() != expected_status:
        raise SystemExit('Unexpected retained firmware state; unchanged')
    if 'pinned=0 registered=15' not in before['trial'] or 'published=0' not in before['trial']:
        raise SystemExit('Unexpected resource ownership; unchanged')
    if not args.run:
        print(json.dumps(dict(eligible=True, command=command, before=before,
                              writes_planned=['COMMAND.w=0004:0004', 'COMMAND.w=0000:0004']), indent=2))
        return
    if REPORT.exists():
        raise SystemExit('An experiment record already exists; no repeat')
    report = dict(utc=datetime.now(timezone.utc).isoformat(),
                  boot_id=Path('/proc/sys/kernel/random/boot_id').read_text().strip(),
                  before=before, command_before=command, samples=[], attempted=False,
                  scope='Guest PCI COMMAND bit 2 only; no BAR/queue/reset/Host administration writes')

    def save():
        REPORT.write_text(json.dumps(report, indent=2) + '\n')

    report['attempted'] = True
    save()
    try:
        run('sudo', '-n', 'setpci', '-s', '0000:00:0e.0', 'COMMAND.w=0004:0004')
        report['command_enabled'] = run('sudo', '-n', 'setpci', '-s', '0000:00:0e.0', 'COMMAND.w')
        if report['command_enabled'] != '0007':
            raise RuntimeError('PCI enable readback failed')
        start = time.monotonic()
        for i in range(5):
            if i:
                time.sleep(3)
            report['samples'].append(dict(elapsed=time.monotonic() - start, **snapshot()))
            save()
    except BaseException as exc:
        report['error'] = repr(exc)
        raise
    finally:
        try:
            # Masked 16-bit operation never writes PCI STATUS or INTx state.
            run('sudo', '-n', 'setpci', '-s', '0000:00:0e.0', 'COMMAND.w=0000:0004')
            report['command_restored'] = run('sudo', '-n', 'setpci', '-s', '0000:00:0e.0', 'COMMAND.w')
            report['restored'] = report['command_restored'] == command
            report['after'] = snapshot()
            report['services'] = run('systemctl', 'is-active', 'sddm', 'spice-vdagentd', 'tailscaled')
            report['firmware_progress'] = any(s['retained_status'] != before['retained_status'] for s in report['samples'])
        finally:
            save()
    print(REPORT)
    print(json.dumps({k: report[k] for k in ('command_enabled', 'command_restored', 'restored', 'firmware_progress', 'services')}, indent=2))


if __name__ == '__main__':
    main()
