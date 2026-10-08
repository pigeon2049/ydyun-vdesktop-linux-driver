#!/usr/bin/env python3
"""r376 gate: bridge-side proper VM init."""
import re
import unittest

BRIDGE = '/opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/kernel/recovery/mt_pvr_bridge.c'

def read(path):
    with open(path) as f:
        return f.read()

class TestR376(unittest.TestCase):
    def test_bridge_proper_init(self):
        c = read(BRIDGE)
        self.assertRegex(c, r'mt_gpu_vm_init\(&tvm->vm')
        self.assertIn('Synthetic BO', c)
        self.assertNotIn('mt_ta_vm_impl', c)

    def test_no_probe_api_calls(self):
        c = read(BRIDGE)
        # No actual CALLS to probe API (comments ok)
        self.assertNotRegex(c, r'[^_*a-zA-Z]mt_probe_ta_vm_create\(\)')
        self.assertNotRegex(c, r'[^_*a-zA-Z]mt_probe_ta_vm_bind\(')

    def test_r375_hacks_removed(self):
        c = read(BRIDGE)
        self.assertNotIn('mt_ta_bo_borrow', c)

if __name__ == '__main__':
    unittest.main()
