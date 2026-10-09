#!/usr/bin/env python3
"""Gate the VM image/scratch overlap check (r276).

mt_gpu_vm_init must reject truly overlapping image/scratch ranges,
not merely nearby ones: the old gap check (distance >= capacity)
misfired on large VMs whose two adjacent allocations sit closer
than one full capacity apart, blocking the 2112-page translator
scene with -EINVAL.
"""
import re
import unittest
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.helpers import get_repo_root

HEADER = get_repo_root() / 'kernel' / 'mt_gpu_vm.h'


class VmOverlap(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.src = HEADER.read_text()

    def test_rejects_true_overlap(self):
        self.assertRegex(
            self.src,
            r'\(a < b \+ capacity && b < a \+ capacity\)',
            'init must reject overlapping image/scratch')

    def test_gap_check_gone(self):
        self.assertNotRegex(
            self.src,
            r'\(a > b \? a - b : b - a\) < capacity',
            'distance check must not remain')


if __name__ == '__main__':
    unittest.main()
