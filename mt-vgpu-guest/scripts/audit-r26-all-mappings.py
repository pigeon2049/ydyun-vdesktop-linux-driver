#!/usr/bin/env python3
"""Walk every leaf of the saved r23 GPU page tables without device access.

The expected shared ranges come from the actual C layout planner, compiled
in user RAM. This confirms coverage/permissions in the saved tables, not
physical backing validity after a Guest reboot or a successful GPU access.
"""
import collections
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TABLE_PA = 0x61000d000
PA_MASK = 0xfffffff000


def walk(data):
    if len(data) != 32 * 4096:
        raise ValueError('Expected the complete 32-page r23 root reservation')
    pages, leaves = {0: 'PC'}, {}

    def table(pa, role):
        offset = pa - TABLE_PA
        if offset < 0 or offset + 4096 > len(data) or offset % 4096:
            raise ValueError(f'{role} outside saved reservation: {pa:#x}')
        if offset in pages:
            raise ValueError(f'Unexpected repeated/cyclic table: {pa:#x}')
        pages[offset] = role
        return offset

    for pc_i in range(1024):
        pc = struct.unpack_from('<I', data, pc_i * 4)[0]
        if not pc:
            continue
        if pc & 15 != 1:
            raise ValueError(f'Unexpected PC flags: {pc:#x}')
        pd_base = table((pc & 0xfffffff0) << 8, 'PD')
        for pd_i in range(512):
            pd = struct.unpack_from('<Q', data, pd_base + pd_i * 8)[0]
            if not pd:
                continue
            if pd & ~PA_MASK != 1:
                raise ValueError(f'Unexpected PD flags: {pd:#x}')
            pt_base = table(pd & PA_MASK, 'PT')
            for pt_i in range(512):
                pt = struct.unpack_from('<Q', data, pt_base + pt_i * 8)[0]
                if not pt:
                    continue
                if not pt & 1:
                    raise ValueError(f'Nonzero invalid PTE: {pt:#x}')
                va = (pc_i << 30) | (pd_i << 21) | (pt_i << 12)
                leaves[va] = (pt & PA_MASK, pt & ~PA_MASK)
    return pages, leaves


def expected_ranges():
    source = r'''
#include <stdio.h>
#include "mt_guest_heaps.h"
int main(void) {
    struct mt_guest_heap_plan plan;
    struct mt_guest_pool_spec pools[MT_GUEST_POOL_COUNT];
    unsigned int ids[] = {0, 4, 10, 11, 6, 3};
    const char *names[] = {"PB", "PDS", "YUV", "kill", "fence", "paging-command"};
    mt_guest_plan_heaps(&plan);
    mt_guest_plan_pools(pools);
    for (unsigned int i = 0; i < 6; i++)
        printf("%s %llx %x\n", names[i], (unsigned long long)plan.resources[ids[i]].va,
               plan.resources[ids[i]].size);
    for (unsigned int i = 0; i < MT_GUEST_POOL_COUNT; i++)
        printf("pool-%u %llx %x\n", i, (unsigned long long)pools[i].va, pools[i].bytes);
    return 0;
}
'''
    with tempfile.TemporaryDirectory() as directory:
        c = Path(directory) / 'layout.c'
        exe = Path(directory) / 'layout'
        c.write_text(source)
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-I', str(ROOT / 'kernel'), str(c), '-o', str(exe)], check=True)
        lines = subprocess.check_output([str(exe)], text=True).splitlines()
    return [(n, int(va, 16), int(size, 16), 1) for n, va, size in
            (line.split() for line in lines)] + [
        ('command', 0x40000000, 4096, 7), ('source', 0x40100000, 4096, 7),
        ('destination', 0x40200000, 4096, 7), ('DMA', 0x40010000, 8192, 7),
        ('engine-state', 0x40020000, 4096, 7)]


def main():
    path = ROOT / 'build/r23-live/tqx-after-root.bin'
    data = path.read_bytes()
    assert hashlib.sha256(data).hexdigest() == '1e6334521f98b29d7fefc394822ee0899eb2cd9a12903f93baa2f13feddd248d'
    tables, leaves = walk(data)
    seen, ranges = set(), []
    for name, va, size, flags in expected_ranges():
        vas = set(range(va, va + size, 4096))
        assert not seen & vas
        assert all(address in leaves and leaves[address][1] == flags for address in vas), name
        seen |= vas
        ranges.append({'name': name, 'va': hex(va), 'bytes': size, 'pages': size // 4096,
                       'pte_flags': flags, 'read_only': bool(flags & 2),
                       'coherent': bool(flags & 4), 'candidate_guest_map_flags': 0})
    assert seen == set(leaves)
    assert len(tables) == 15 and len(leaves) == 3335
    # Unallocated reservation pages must not hide another table/image.
    assert all(not any(data[i:i+4096]) for i in range(0, len(data), 4096) if i not in tables)
    report = {'passed': True, 'hardware_access': False,
              'root_sha256': hashlib.sha256(data).hexdigest(), 'table_pages': len(tables),
              'mapped_pages': len(leaves), 'ranges': ranges,
              'pte_flag_counts': dict(collections.Counter(flags for _, flags in leaves.values())),
              'conclusion': 'Only the six pages of five ordinary BOs have read-only/coherent PTEs. All 3329 shared pages already use writable default PTEs. r24 changes exactly those five ordinary binds.',
              'limits': 'Historical table coverage and permissions only. No post-reboot GPA validity, actual GPU TLB state, safe recovery or successful copy is established.'}
    (ROOT / 'reports/r26-all-mappings.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: {len(leaves)} PTEs / {len(tables)} tables / {len(ranges)} bindings; 6 RO pages, 3329 writable')


if __name__ == '__main__':
    main()
