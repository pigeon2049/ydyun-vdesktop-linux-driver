"""Exercise fabricated PMR mmap aliasing without a GPU or kernel module."""
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHIM = ROOT / 'probe' / 'umd_bridge_shim.c'

CLIENT = r'''#define _GNU_SOURCE
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#define SRVKM_CMD 0xc0206440UL
struct srvkm_cmd {
    uint32_t bridge_id, bridge_func_id;
    uint64_t in_ptr, out_ptr;
    uint32_t in_size, out_size;
};

static int bridge(int fd, uint32_t id, uint32_t func,
                  const void *in, uint32_t in_size,
                  void *out, uint32_t out_size)
{
    struct srvkm_cmd cmd = {id, func, (uint64_t)(uintptr_t)in,
                            (uint64_t)(uintptr_t)out, in_size, out_size};
    return ioctl(fd, SRVKM_CMD, &cmd);
}

static void *map(int fd, off_t off, size_t len)
{
    return (void *)syscall(SYS_mmap, 0, len, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE, fd, off);
}

int main(void)
{
    int fd = open("/dev/dri/renderD128", O_RDWR);
    uint64_t info_pmr = 0xbad, pmr_pmr = 0xb000, other_pmr = 0xb001;
    uint64_t info_map = 0, pmr_map = 0, other_map = 0;
    uint8_t meta[28];
    uint8_t submit_in[108] = {0};
    uint32_t info_out[3] = {0};
    uint32_t submit_out = 0;
    uint32_t *a, *alias, *next, *info;
    if (fd < 0) return 10;
    if (bridge(fd, 0x1, 0xf, 0, 0, info_out, sizeof(info_out))) return 16;
    info_pmr = *(uint64_t *)info_out;
    if (bridge(fd, 0x6, 0x6, &info_pmr, sizeof(info_pmr),
               meta, sizeof(meta))) return 17;
    memcpy(&info_map, meta + 16, sizeof(info_map));
    if (bridge(fd, 0x6, 0x6, &pmr_pmr, sizeof(pmr_pmr),
               meta, sizeof(meta))) return 18;
    memcpy(&pmr_map, meta + 16, sizeof(pmr_map));
    if (bridge(fd, 0x6, 0x6, &other_pmr, sizeof(other_pmr),
               meta, sizeof(meta))) return 20;
    memcpy(&other_map, meta + 16, sizeof(other_map));
    if (info_map == pmr_map || pmr_map == other_map) return 19;
    info = map(fd, (off_t)(info_map << 12), 4096);
    a = map(fd, (off_t)(pmr_map << 12), 8192);
    alias = map(fd, (off_t)(pmr_map << 12), 8192);
    next = map(fd, (off_t)(other_map << 12), 8192);
    if (a == MAP_FAILED || alias == MAP_FAILED ||
        next == MAP_FAILED || info == MAP_FAILED) return 11;
    if (a[0] != 0 || next[0] != 0) return 12;
    a[1024] = 0x51a7c0de;
    if (alias[1024] != 0x51a7c0de) return 13;
    if (next[0] != 0) return 21;
    if (next[1024] != 0) return 14;
    if (info[0] != 1 || info[0x44 / 4] != 0xb57 ||
        info[0x48 / 4] != 0x688a847) return 15;
    a[0] = 0x157c0de;
    munmap(a, 8192);
    if (bridge(fd, 0x89, 0xa, submit_in, sizeof(submit_in),
               &submit_out, sizeof(submit_out))) return 22;
    munmap(alias, 8192);
    munmap(next, 8192); munmap(info, 4096);
    close(fd);
    return 0;
}
'''


class FabricatedSharedPmrBacking(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='mt-shared-pmr-')
        cls.work = Path(cls.temp.name)
        cls.shim = cls.work / 'umd_bridge_shim.so'
        cls.client_c = cls.work / 'client.c'
        cls.client = cls.work / 'client'
        cls.trace = cls.work / 'trace.jsonl'
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

    def test_same_offset_aliases_and_pmr_is_not_info_page(self):
        env = os.environ.copy()
        env.update({
            'LD_PRELOAD': str(self.shim),
            'UMD_SHARED_BACKING': '1',
            'UMD_SHARED_SNAPSHOT': '1',
            'UMD_TRACE': str(self.trace),
        })
        result = subprocess.run([str(self.client)], env=env,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0,
                         'shared PMR alias probe failed: %s%s' %
                         (result.stdout, result.stderr))
        rows = [json.loads(line) for line in self.trace.read_text().splitlines()]
        maps = [row for row in rows if row.get('op') == 'mmap_fabricated']
        self.assertEqual(len(maps), 4)
        self.assertTrue(all(row.get('backing') == 'shared' for row in maps))
        snapshots = [row for row in rows if row.get('op') == 'pmr_snapshot'
                     and row.get('submit') == '0x89:0xa']
        self.assertEqual(len(snapshots), 3,
                         'SubmitTransfer3 must omit an unmapped PMR view')
        self.assertTrue(any(row.get('nonzero_bytes', 0) for row in snapshots),
                        'SubmitTransfer3 snapshots must observe written bytes')


if __name__ == '__main__':
    unittest.main()
