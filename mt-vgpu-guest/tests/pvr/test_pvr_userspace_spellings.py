#!/usr/bin/env python3
"""Gate header-local userspace spellings (r331).

mt_mmu_bootstrap.h must be self-contained in userspace: allocation and
logging go through header-local macros (calloc/free/fprintf) instead of
relying on TU-provided kernel spellings (kvzalloc/GFP_KERNEL/pr_info),
which differ per harness TU. The kernel branch keeps the original
spellings, so page-table bytes are unchanged (verify-mmu-bootstrap.py
passes byte-identical). mt_gpu_vm.h carries the same triple by
convention. The old header fails a standalone -Werror userspace
compile; the new one passes.
"""
import re
import unittest
from pathlib import Path

KERNEL = Path(__file__).resolve().parents[2] / 'kernel'
BOOTSTRAP = KERNEL / 'mt_mmu_bootstrap.h'
GPU_VM = KERNEL / 'mt_gpu_vm.h'
WRAPPER = Path(__file__).resolve().parent.parent / 'c' / 'mmu_bootstrap_wrapper.c'

TRIPLE = ('zalloc', 'free', 'log')
BARE = r'kvzalloc|kvfree|GFP_KERNEL|pr_info'


def _code_without_macros_or_comments(text, prefix):
    no_comments = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    no_comments = re.sub(r'//.*', '', no_comments)
    kept = [ln for ln in no_comments.splitlines()
            if not re.match(r'\s*#\s*define\s+%s_(%s)\b' % (prefix, '|'.join(TRIPLE)), ln)]
    return '\n'.join(kept)


class BootstrapSpellings(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.src = BOOTSTRAP.read_text()
        cls.body = _code_without_macros_or_comments(cls.src, 'mt_mmu_bootstrap')

    def test_userspace_triple_uses_libc(self):
        for name, spelling in (('zalloc', r'calloc\(1, \(n\)\)'),
                               ('free', r'free\(p\)'),
                               ('log', r'fprintf\(stderr,')):
            self.assertRegex(self.src,
                             r'#define\s+mt_mmu_bootstrap_%s\(.*\)\s+%s' % (name, spelling),
                             'userspace %s must use libc spelling' % name)

    def test_kernel_triple_keeps_kernel_spellings(self):
        for name, spelling in (('zalloc', r'kvzalloc\(\(n\), GFP_KERNEL\)'),
                               ('free', r'kvfree\(p\)'),
                               ('log', r'pr_info\(')):
            self.assertRegex(self.src,
                             r'#define\s+mt_mmu_bootstrap_%s\(.*\)\s+%s' % (name, spelling),
                             'kernel %s must keep kernel spelling' % name)

    def test_no_bare_kernel_spellings_outside_macros(self):
        m = re.search(BARE, self.body)
        self.assertIsNone(m, 'bare kernel spelling outside macros: %s'
                          % (m.group(0) if m else ''))

    def test_build_pages_uses_macros(self):
        self.assertIn('mt_mmu_bootstrap_zalloc(', self.src)
        self.assertIn('mt_mmu_bootstrap_free(', self.src)
        self.assertIn('mt_mmu_bootstrap_log(', self.src)

    def test_wrapper_provides_no_stubs(self):
        src = WRAPPER.read_text()
        self.assertNotRegex(src, BARE,
                            'wrapper must stay stub-free (header is self-contained)')


class GpuVmMirror(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.src = GPU_VM.read_text()

    def test_gpu_vm_triple_mirrors_convention(self):
        for name in TRIPLE:
            self.assertRegex(self.src, r'#define\s+mt_gpu_vm_%s\(' % name,
                             'mt_gpu_vm.h must mirror the %s macro' % name)

    def test_no_bare_pr_info_outside_macros(self):
        body = _code_without_macros_or_comments(self.src, 'mt_gpu_vm')
        m = re.search(r'pr_info\s*\(', body)
        self.assertIsNone(m, 'shared code must log through mt_gpu_vm_log')

    def test_dependent_headers_log_through_gpu_vm_macro(self):
        for name in ('mt_process_resources.h', 'mt_boot_bo.h'):
            src = KERNEL / name
            text = src.read_text()
            m = re.search(r'pr_info\s*\(', text)
            self.assertIsNone(m, '%s must log through mt_gpu_vm_log' % name)


if __name__ == '__main__':
    unittest.main()
