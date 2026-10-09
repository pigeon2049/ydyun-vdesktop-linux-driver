#!/usr/bin/env python3
"""Keep the fabricated UMD DRM-major gate opt-in and hardware-free."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SHIM = ROOT / 'probe' / 'umd_bridge_shim.c'

CLIENT = r'''#define _GNU_SOURCE
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct drm_version {
    int32_t major, minor, patch;
    uint64_t name_len;
    char *name;
    uint64_t date_len;
    char *date;
    uint64_t desc_len;
    char *desc;
};

int main(void)
{
    char name[8] = {0};
    struct drm_version v = {0};
    int32_t probe_major;
    int fd = open("/dev/dri/renderD128", O_RDWR);
    if (fd < 0) return 10;
    if (ioctl(fd, 0xc0406400UL, &v)) return 11;
    probe_major = v.major;
    v.name_len = sizeof(name);
    v.name = name;
    if (ioctl(fd, 0xc0406400UL, &v)) return 12;
    printf("probe=%d claim=%d name=%s\n", probe_major, v.major, name);
    close(fd);
    return 0;
}
'''


class FabricatedDrmMajor(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='mt-drm-major-')
        cls.work = Path(cls.temp.name)
        cls.shim = cls.work / 'umd_bridge_shim.so'
        cls.client_c = cls.work / 'client.c'
        cls.client = cls.work / 'client'
        cls.client_c.write_text(CLIENT)
        subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-shared', '-fPIC', str(SHIM), '-o', str(cls.shim),
                        '-ldl'], check=True, capture_output=True)
        subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        str(cls.client_c), '-o', str(cls.client)],
                       check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_client(self, major=None):
        env = os.environ.copy()
        env['LD_PRELOAD'] = str(self.shim)
        env.pop('UMD_DRM_MAJOR', None)
        if major is not None:
            env['UMD_DRM_MAJOR'] = str(major)
        return subprocess.run([str(self.client)], env=env, capture_output=True,
                              text=True, check=True).stdout.strip()

    def test_legacy_major_remains_default(self):
        self.assertEqual(self.run_client(), 'probe=1 claim=1 name=pvr')

    def test_ddk2_major_is_opt_in(self):
        self.assertEqual(self.run_client(2), 'probe=2 claim=2 name=pvr')

    def test_invalid_major_falls_back_to_legacy(self):
        self.assertEqual(self.run_client(3), 'probe=1 claim=1 name=pvr')


if __name__ == '__main__':
    unittest.main()
