#!/usr/bin/env python3
"""Build, and optionally run, the RAM-only GEM integration test module."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    return subprocess.run(args, cwd=ROOT, check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT).stdout


def snapshot():
    driver = Path('/sys/bus/pci/devices/0000:00:0e.0/driver')
    version = Path('/sys/module/mt_guest_probe/srcversion')
    return dict(driver=str(driver.resolve()) if driver.exists() else None,
                live_probe_srcversion=version.read_text().strip() if version.exists() else None,
                drm_nodes=sorted(p.name for p in Path('/dev/dri').glob('*')))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', action='store_true', help='Load RAM-only selftest; always unload afterward')
    parser.add_argument('--kind', choices=('gem', 'fence'), default='gem')
    args = parser.parse_args()
    name = f'mt_{args.kind}_selftest'
    output = run('make', '-C', str(ROOT / 'kernel/selftest'), 'W=1', '-j2')
    if 'warning:' in output.lower():
        raise RuntimeError(output)
    module = ROOT / f'kernel/selftest/{name}.ko'
    report = dict(utc=datetime.now(timezone.utc).isoformat(), build_output=output,
                  module_sha256=hashlib.sha256(module.read_bytes()).hexdigest(),
                  executed=False, backing='ordinary kernel RAM',
                  scope=('Real DRM/GEM object callbacks and VM/BO lifetime; no real DRM file handles or GPU execution'
                         if args.kind == 'gem' else 'Real dma_fence callbacks with RAM queues; no hardware events or GPU execution'))
    if args.run:
        path = Path('/sys/module') / name
        if path.exists():
            raise RuntimeError('Selftest already loaded; do not replace a running test')
        report['before'] = snapshot()
        before_log = run('sudo', '-n', 'dmesg')
        run('sudo', '-n', '/sbin/insmod', str(module))
        try:
            report['executed'] = True
            report['result'] = int((path / 'parameters/result').read_text())
            report['checks'] = int((path / 'parameters/checks').read_text())
            report['failure_line'] = int((path / 'parameters/failure_line').read_text())
            if args.kind == 'fence':
                report['callback_count'] = int((path / 'parameters/callback_count').read_text())
        finally:
            run('sudo', '-n', '/sbin/rmmod', name)
        after_log = run('sudo', '-n', 'dmesg')
        # Keep full post-test log only when the ring rotated during the test.
        delta = after_log[len(before_log):] if after_log.startswith(before_log) else after_log
        report['kernel_log'] = delta
        report['after'] = snapshot()
        report['test_module_unloaded'] = not path.exists()
        report['temporary_parent_removed'] = not Path('/sys/devices/mt-guest-gem-selftest').exists()
        report['passed'] = (report['result'] == 0 and report['checks'] >= (13 if args.kind == 'gem' else 160) and
                            not report['failure_line'] and report['test_module_unloaded'] and
                            report['temporary_parent_removed'] and report['before'] == report['after'] and
                            'WARNING:' not in delta and 'BUG:' not in delta and 'Oops:' not in delta)
    (ROOT / f'reports/{args.kind}-kernel-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    if args.run and not report['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
