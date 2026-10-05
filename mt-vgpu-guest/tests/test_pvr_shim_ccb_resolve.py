"""Gate the fabricated CCB VA->PMR resolve without hardware (r158)."""
import json
import os
import struct
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

static void *umap(int fd, off_t off, size_t len)
{
    return (void *)syscall(SYS_mmap, 0, len, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE, fd, off);
}

int main(void)
{
    int fd = open("/dev/dri/renderD128", O_RDWR);
    uint8_t pmr_in[72] = {0};
    uint8_t pmr_out[24] = {0};
    uint8_t res_in[24] = {0};
    uint8_t res_out[12] = {0};
    uint8_t map_in[32] = {0};
    uint8_t map_out[12] = {0};
    uint8_t submit_in[108] = {0};
    uint32_t submit_out = 0;
    uint64_t pmr = 0, res = 0, addr = 0x8000a00000ULL, len = 0x2000;
    uint64_t heap = 2, ccb_va;
    uint32_t ccb_bytes = 0x100;
    uint8_t *base;
    size_t i;
    if (fd < 0) return 10;
    if (bridge(fd, 0x6, 0x9, pmr_in, sizeof(pmr_in), pmr_out, sizeof(pmr_out)))
        return 11;
    memcpy(&pmr, pmr_out, 8);
    if (!pmr) return 12;
    memcpy(res_in, &addr, 8);
    memcpy(res_in + 8, &len, 8);
    memcpy(res_in + 16, &heap, 8);
    if (bridge(fd, 0x6, 0x15, res_in, sizeof(res_in), res_out, sizeof(res_out)))
        return 13;
    memcpy(&res, res_out, 8);
    if (!res) return 14;
    memcpy(map_in, &heap, 8);
    memcpy(map_in + 8, &pmr, 8);
    memcpy(map_in + 16, &res, 8);
    if (bridge(fd, 0x6, 0x13, map_in, sizeof(map_in), map_out, sizeof(map_out)))
        return 15;
    base = umap(fd, (off_t)(pmr << 12), 0x2000);
    if (base == MAP_FAILED) return 16;
    /* CCB pattern at reservation offset 0x100. */
    for (i = 0; i < 0x100; i++)
        base[0x100 + i] = (uint8_t)((i % 255) + 1);
    ccb_va = addr + 0x100;
    memset(submit_in, 0, sizeof(submit_in));
    memcpy(submit_in + 88, &ccb_va, 8);
    memcpy(submit_in + 104, &ccb_bytes, 4);
    if (bridge(fd, 0x89, 0xa, submit_in, sizeof(submit_in),
               &submit_out, sizeof(submit_out))) return 17;
    /* Unresolvable VA: outside every reservation. */
    ccb_va = 0x7000000000ULL;
    memcpy(submit_in + 88, &ccb_va, 8);
    if (bridge(fd, 0x89, 0xa, submit_in, sizeof(submit_in),
               &submit_out, sizeof(submit_out))) return 18;
    munmap(base, 0x2000);
    close(fd);
    return 0;
}
'''


class FabricatedCcbResolve(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='mt-ccb-resolve-')
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

    def test_ccb_va_resolves_to_pmr_backing(self):
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
                         'ccb resolve probe failed: %s%s' %
                         (result.stdout, result.stderr))
        rows = [json.loads(line) for line in self.trace.read_text().splitlines()]
        tids = {row.get('tid') for row in rows if 'tid' in row}
        self.assertTrue(tids, 'trace records must carry tid (r168)')
        self.assertTrue(all(isinstance(t, int) and t > 0 for t in tids))
        resolves = [row for row in rows if row.get('op') == 'ccb_resolve']
        self.assertEqual(len(resolves), 2)
        ok, miss = resolves
        self.assertEqual(ok.get('resolved'), 1)
        self.assertEqual(ok.get('ccb_va'), '0x8000a00100')
        self.assertEqual(ok.get('ccb_bytes'), 0x100)
        self.assertEqual(ok.get('backing_offset'), 0x100)
        self.assertEqual(ok.get('nonzero_bytes'), 0x100)
        self.assertEqual(ok.get('runs'),
                         [{'o': 0,
                           'b': ''.join('%02x' % ((i % 255) + 1)
                                        for i in range(64))}])
        self.assertEqual(miss.get('resolved'), 0)

        # Reverse check: without shared backing no resolve record exists.
        trace2 = self.work / 'trace2.jsonl'
        env2 = os.environ.copy()
        env2.update({
            'LD_PRELOAD': str(self.shim),
            'UMD_TRACE': str(trace2),
        })
        env2.pop('UMD_SHARED_BACKING', None)
        env2.pop('UMD_SHARED_SNAPSHOT', None)
        result = subprocess.run([str(self.client)], env=env2,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0)
        rows2 = [json.loads(line) for line in trace2.read_text().splitlines()]
        self.assertFalse(any(row.get('op') == 'ccb_resolve' for row in rows2),
                         'resolve must be gated on shared backing')


if __name__ == '__main__':
    unittest.main()
