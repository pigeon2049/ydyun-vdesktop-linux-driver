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
    for name in ('delay.h', 'module.h', 'sched.h', 'lockdep.h', 'mutex.h', 'slab.h', 'vmalloc.h', 'mm.h', 'gfp.h'):
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
    # Sanitizer targets: the lifetime and mapping harnesses own heap buffers
    # and reference counts, so a leak or use-after-free must fail the build.
    asan = ('bo_lifetime_test', 'gpu_vm_test', 'gpu_vm_scale_test', 'gem_lifetime_test',
            'work_job_test', 'execution_context_test', 'tqx_program_test', 'tqx_upload_test',
            'tqx_dma_test', 'tqx_submission_test', 'pvr_arena_plan_test', 'boot_bo_lifetime_test',
            'system_memory_test', 'boot_resource_stage_test', 'pvr_bridge_core_test')
    test_names = ('runtime_context_test', 'trial_lifetime_test', 'bo_lifetime_test', 'gpu_vm_test',
                  'gpu_vm_scale_test',
                  'gem_lifetime_test', 'work_job_test', 'execution_context_test', 'runtime_submit_gate_test', 'tqx_program_test', 'tqx_upload_test', 'tqx_dma_test', 'tqx_submission_test', 'system_dma_pages_test', 'pvr_arena_plan_test', 'boot_bo_lifetime_test', 'system_memory_test', 'boot_resource_stage_test',
                  'pvr_bridge_core_test')
    for source in test_names:
        binary = build / source
        run(flags + (['-fsanitize=address'] if source in asan else []) +
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
    # Every recovery module reads address-space and mapping state that the main
    # module allocated, so a layout change in any of these silently misreads a
    # live session. The original check covered only mt_guest, which let the
    # mt_gpu_vm mapping-table change pass unnoticed. Compare the full set
    # against a recorded baseline and gate on any drift.
    abi_structs = ('mt_guest', 'mt_guest_device', 'mt_gpu_vm', 'mt_vm_binding',
                   'mt_vm_vram', 'mt_vm_store', 'mt_work_job')
    abi = {name: hashlib.sha256(run(['pahole', '-C', name, module]).encode()).hexdigest()
           for name in abi_structs}
    baseline_path = ROOT / 'reports/shared-abi-baseline.json'
    if baseline_path.exists():
        baseline = json.loads(baseline_path.read_text())
        drift = sorted(k for k in abi if baseline.get('abi', {}).get(k) != abi[k])
        if drift:
            raise RuntimeError(
                'Shared ABI changed for ' + ', '.join(drift) +
                '. Recovery modules and the main module must be rebuilt and the '
                'main module reloaded together; a live session would be misread.')
    else:
        baseline_path.write_text(json.dumps(
            dict(utc=datetime.now(timezone.utc).isoformat(), abi=abi), indent=2) + '\n')
    report = dict(utc=datetime.now(timezone.utc).isoformat(), passed=True,
                  checks=checks, module_sha256=hashlib.sha256(module.read_bytes()).hexdigest(),
                  shared_mt_guest_abi_matches_backup=True,
                  shared_abi_structures=abi,
                  abi_backup_sha256=hashlib.sha256(backups[0].read_bytes()).hexdigest(),
                  module_loaded=False, hardware_written=False,
                  limits='Userspace RAM/OS models and kernel compilation; no live successful connection, context publication, context withdrawal, or GPU rendering verified.')
    (ROOT / 'reports/runtime-integration-build.json').write_text(json.dumps(report, indent=2) + '\n')
    for name in test_names:
        print(checks[name].strip())
    print('PASS: W=1 module build and retained mt_guest ABI comparison; not loaded')


if __name__ == '__main__':
    main()
