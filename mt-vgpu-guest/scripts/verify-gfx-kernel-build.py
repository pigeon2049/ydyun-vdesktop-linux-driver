#!/usr/bin/env python3
"""Compile the GFX CPU encoder with the running kernel's types and W=1.

The generated module has no init/exit function or hardware access, and is never
loaded. This checks the same callable wrapper used by the instruction oracle.
"""
from pathlib import Path
import platform
import subprocess

ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'build/r35-gfx/kernel-compile';out.mkdir(parents=True,exist_ok=True)
(out/'gfx_kernel_compile.c').write_text('#include <linux/module.h>\n'
    '#include "'+str(ROOT/'tests/gfx_packet_oracle_wrapper.c')+'"\n'
    '#include "'+str(ROOT/'tests/gfx_context_oracle_wrapper.c')+'"\n'
    'MODULE_LICENSE("GPL");\n'
    'MODULE_DESCRIPTION("Compile-only GFX CPU encoder and context verification");\n')
(out/'Makefile').write_text('obj-m += gfx_kernel_compile.o\n')
result=subprocess.run(['make','-C',f'/lib/modules/{platform.release()}/build',
    f'M={out}','W=1','modules'],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(ROOT/'reports/r35-gfx-kernel-build.log').write_text(result.stdout)
print(result.stdout,end='')
result.check_returncode()
assert 'warning:' not in result.stdout.lower(), 'Kernel compilation must be warning-free'
print('Kernel compile passed; module was not loaded.')
