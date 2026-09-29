#!/usr/bin/env python3
"""Build and test runtime integration without loading modules or accessing PCI."""
import hashlib
import json
from pathlib import Path
import subprocess
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]


def run(argv):
    result = subprocess.run(list(map(str, argv)), cwd=ROOT, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f'{argv}:\n{result.stdout}\n{result.stderr}')
    return result.stdout + result.stderr


def main():
    build = ROOT / 'build/runtime-test'
    inc = build / 'include/linux'
    inc.mkdir(parents=True, exist_ok=True)
    for name in ('delay.h', 'module.h', 'sched.h', 'lockdep.h', 'mutex.h', 'slab.h', 'vmalloc.h', 'mm.h'):
        (inc / name).write_text('/* OS primitives supplied by userspace lifetime harness. */\n')
    drm_inc = inc.parent / 'drm'
    drm_inc.mkdir(exist_ok=True)
    for name in ('drm_device.h', 'drm_drv.h', 'drm_file.h', 'drm_gem.h'):
        (drm_inc / name).write_text('/* DRM primitives modeled by GEM lifetime harness. */\n')
    main_source = (ROOT / 'kernel/mt_guest_probe.c').read_text()
    start = main_source.index('static int mt_runtime_can_submit(void *opaque)\n{')
    end = main_source.index('\n}\n', start) + 3
    (inc.parent / 'runtime_submit_gate.h').write_text(main_source[start:end])
    flags = ['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-O2',
             '-fsanitize=undefined', '-fno-sanitize-recover=all']
    checks = {}
    test_names = ('runtime_context_test', 'trial_lifetime_test', 'bo_lifetime_test', 'gpu_vm_test',
                  'gem_lifetime_test', 'work_job_test', 'execution_context_test', 'runtime_submit_gate_test', 'tqx_program_test', 'tqx_upload_test', 'tqx_dma_test', 'tqx_submission_test', 'boot_bo_lifetime_test', 'system_memory_test', 'boot_resource_stage_test')
    for source in test_names:
        binary = build / source
        run(flags + (['-fsanitize=address'] if source in ('bo_lifetime_test', 'gpu_vm_test', 'gem_lifetime_test', 'work_job_test', 'execution_context_test', 'tqx_program_test', 'tqx_upload_test', 'tqx_dma_test', 'tqx_submission_test', 'boot_bo_lifetime_test', 'system_memory_test', 'boot_resource_stage_test') else []) +
            ['-I', inc.parent, ROOT / 'tests' / (source + '.c'), '-o', binary])
        checks[source] = run([binary] + ([ROOT / 'reports/device-info.bin']
                                        if source == 'runtime_context_test' else []))
    checks['kernel_build'] = run(['make', '-C', ROOT / 'kernel', 'W=1', '-j2'])
    if 'warning:' in checks['kernel_build'].lower():
        raise RuntimeError(checks['kernel_build'])
    module = ROOT / 'kernel/mt_guest_probe.ko'
    backups = list((ROOT / 'build/recovery-channel').glob('loaded-6f259*.ko'))
    if len(backups) != 1:
        raise RuntimeError('Expected exact retained-module ABI backup')
    old, new = [run(['pahole', '-C', 'mt_guest', p]) for p in (backups[0], module)]
    if old != new:
        raise RuntimeError('Shared mt_guest ABI differs from the recovery module')
    report = dict(utc=datetime.now(timezone.utc).isoformat(), passed=True,
                  checks=checks, module_sha256=hashlib.sha256(module.read_bytes()).hexdigest(),
                  shared_mt_guest_abi_matches_backup=True,
                  abi_backup_sha256=hashlib.sha256(backups[0].read_bytes()).hexdigest(),
                  module_loaded=False, hardware_written=False,
                  limits='Userspace RAM/OS models and kernel compilation; no live successful connection, context publication, context withdrawal, or GPU rendering verified.')
    (ROOT / 'reports/runtime-integration-build.json').write_text(json.dumps(report, indent=2) + '\n')
    for name in test_names:
        print(checks[name].strip())
    print('PASS: W=1 module build and retained mt_guest ABI comparison; not loaded')


if __name__ == '__main__':
    main()
