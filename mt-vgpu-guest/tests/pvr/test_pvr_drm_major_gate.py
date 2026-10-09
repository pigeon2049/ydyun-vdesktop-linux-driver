#!/usr/bin/env python3
"""Gate the DRM version_major knob that selects the vendor UMD's DDK path (r134).

RGXPopulateFeatureConfig (UMD 5.2.0, sha b3058c02...) sets features+0x54 to
(drm version_major == 2) + 1, and RGXCreateKickSyncContextCCB takes the legacy
path while that value is < 2. The bridge must therefore report major 0 by
default (validated legacy path) and only report 2 when asked via drm_major.
"""
import re
import unittest
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[2] / 'kernel/recovery/mt_pvr_bridge.c'


def code():
    text = SOURCE.read_text()
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


class DrmMajorGate(unittest.TestCase):
    def test_param_default_is_legacy(self):
        src = code()
        self.assertRegex(src, r'static unsigned int drm_major;')
        self.assertRegex(src, r'module_param\(drm_major, uint, 0400\)')

    def test_driver_major_comes_from_param(self):
        src = code()
        self.assertRegex(src, r'pvr_driver\.major\s*=\s*drm_major\s*;')
        # the static initialiser must not hard-wire a non-zero major
        init = re.search(r'struct drm_driver pvr_driver = \{(.*?)\n\};', src, re.S)
        self.assertIsNotNone(init)
        self.assertNotRegex(init.group(1), r'\.major\s*=\s*[1-9]')
        self.assertNotIn('const struct drm_driver pvr_driver', src)

    def test_major_set_before_alloc(self):
        src = code()
        self.assertLess(src.index('pvr_driver.major = drm_major'),
                        src.index('drm_dev_alloc(&pvr_driver'))


if __name__ == '__main__':
    unittest.main()
