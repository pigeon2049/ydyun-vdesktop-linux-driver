/* SPDX-License-Identifier: GPL-2.0 */
/* Arena answer to byte-tight PVR PMRs: per-PMR vzalloc cannot represent
 * sub-page sharing (two neighbors rounded down share one VA page), while a
 * single arena backing with per-page bindings covers every live range
 * byte-exact through the unchanged mt_gpu_vm_bind_many.
 *
 * Ranges are the real rung8 kick inventory (reports/r54): 12 PMRs, 295 KiB,
 * 5 unaligned VAs, 9 unaligned sizes across 4 heaps.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../kernel/mt_gpu_vm.h"

struct store { u64 next_pa; };
static int alloc(void *opaque, u32 bytes, u32 alignment, struct mt_bo_backing *b)
{
	struct store *s = opaque;
	void *p = malloc(bytes);
	if (!p)
		return -ENOMEM;
	s->next_pa = (s->next_pa + alignment - 1) & ~(u64)(alignment - 1);
	*b = (struct mt_bo_backing){p, s->next_pa, s->next_pa, bytes};
	s->next_pa += bytes;
	return 0;
}
static int clear(void *s, const struct mt_bo_backing *b)
{
	(void)s;
	memset(b->handle, 0, b->bytes);
	return 0;
}
static void release(void *opaque, const struct mt_bo_backing *b)
{
	(void)opaque;
	free(b->handle);
}
static int map(void *s, const struct mt_bo_backing *b, void **out)
{
	(void)s;
	*out = b->handle;
	return 0;
}
static void unmap(void *s, const struct mt_bo_backing *b)
{
	(void)s;
	(void)b;
}
static const struct mt_bo_ops ops = {alloc, clear, release, map, unmap};

/* Independent three-level walk (same shape as gpu_vm_test's, not the
 * production encoder): VA -> PA or 0 when unmapped. */
static u64 walk(const struct mt_gpu_vm *v, u64 va, u64 *pte)
{
	u32 pc;
	u64 pd, root = v->tables->backing.gpu_pa, offset;
	memcpy(&pc, (u8 *)v->image + ((va / 0x40000000) % 1024) * 4, 4);
	if (!(pc & 1))
		return 0;
	offset = ((u64)(pc & 0xfffffff0U) << 8) - root;
	memcpy(&pd, (u8 *)v->image + offset + ((va / 0x200000) % 512) * 8, 8);
	if (!(pd & 1))
		return 0;
	offset = (pd & 0xfffffff000ULL) - root;
	memcpy(pte, (u8 *)v->image + offset + ((va / 4096) % 512) * 8, 8);
	return *pte & 1 ? (*pte & 0xfffffff000ULL) + (va % 4096) : 0;
}

static const u64 ranges[][2] = {
	{0xda00000000ULL, 0x1000}, {0x8000000000ULL, 0x1000},
	{0xe000000000ULL, 0x1000}, {0xda00010000ULL, 0x9bff},
	{0xe000010000ULL, 0x2a7ff}, {0xeb00000000ULL, 0x387},
	{0x8000010000ULL, 0x253}, {0x8000010253ULL, 0x408f},
	{0x80000142e2ULL, 0x408f}, {0x8000018371ULL, 0x408f},
	{0x800001c400ULL, 0x408f}, {0x800002048fULL, 0x207f},
};
#define NRANGES (sizeof(ranges) / sizeof(ranges[0]))

/* Deterministic content addressed by GPU VA: what the UMD believes lives
 * at each VA byte. The arena is filled through VA-colored writes, so a
 * correct plan reads back the same coloring through the page tables. */
static u8 pattern(u64 va)
{
	return (u8)((va * 2654435761ULL) >> 32 ^ (va & 0xff) ^ 0x5a);
}

int main(void)
{
	struct store s = {.next_pa = 0x620000000ULL};
	struct mt_bo tables = {0}, arena = {0};
	struct mt_gpu_vm vm = {0};
	u8 *image = malloc(131072), *scratch = malloc(131072);
	u32 npages = 0, i;
	u64 *page_pa, pte;
	u8 *backing;
	assert(image && scratch);
	assert(!mt_bo_create(&tables, &ops, &s, 131072, 4096));
	assert(!mt_gpu_vm_init(&vm, &tables, image, scratch, 131072));

	/* First, prove the hazard: two per-PMR facades rounded down share VA
	 * page 0x8000010000, so the second bind must refuse (-EEXIST).
	 * Silent rounding would alias both PMRs onto one page. */
	{
		struct mt_bo a = {0}, b = {0};
		struct mt_vm_binding first, second;
		assert(!mt_bo_create(&a, &ops, &s, 4096, 4096));
		assert(!mt_bo_create(&b, &ops, &s, 5 * 4096, 4096));
		first = (struct mt_vm_binding){&a, 0x8000010000ULL, 0, 4096, 0};
		assert(!mt_gpu_vm_bind_many(&vm, &first, 1));
		second = (struct mt_vm_binding){&b, 0x8000010000ULL, 0,
						5 * 4096, 0};
		assert(mt_gpu_vm_bind_many(&vm, &second, 1) == -EEXIST);
		assert(!mt_gpu_vm_unbind(&vm, 0x8000010000ULL, 4096));
		assert(!mt_bo_put(&a) && !mt_bo_put(&b));
		assert(vm.count == 0);
	}
	/* Unaligned VA or bytes cannot bind at all. */
	{
		struct mt_bo c = {0};
		struct mt_vm_binding bad;
		assert(!mt_bo_create(&c, &ops, &s, 8192, 4096));
		bad = (struct mt_vm_binding){&c, 0x8000010253ULL, 0, 0x408f, 0};
		assert(mt_gpu_vm_bind_many(&vm, &bad, 1) == -EINVAL);
		assert(!mt_bo_put(&c));
		assert(vm.count == 0);
	}

	/* Cover-set: every VA page touched by any range. Shared pages appear
	 * once -- that single page carries both neighbors' sub-page bytes,
	 * exactly what per-PMR backing cannot express. */
	{
		u64 cover[128];
		u32 ncover = 0, k;
		for (i = 0; i < NRANGES; i++) {
			u64 va = ranges[i][0], end = va + ranges[i][1];
			u64 p;
			for (p = va & ~4095ULL; p < end; p += 4096) {
				for (k = 0; k < ncover; k++)
					if (cover[k] == p)
						break;
				if (k == ncover)
					cover[ncover++] = p;
			}
		}
		assert(ncover <= 128);
		/* One arena backing every cover page. */
		assert(!mt_bo_create(&arena, &ops, &s, ncover * 4096, 4096));
		page_pa = malloc(ncover * sizeof(*page_pa));
		assert(page_pa);
		for (k = 0; k < ncover; k++)
			page_pa[k] = arena.backing.gpu_pa + (u64)k * 4096;
		arena.page_pa = (const u64 *)page_pa;
		backing = arena.backing.handle;
		/* VA-colored fill through the ranges (disjoint byte sets: the
		 * reservation ledger guarantees no two ranges overlap). */
		for (i = 0; i < NRANGES; i++) {
			u64 va = ranges[i][0];
			u32 len = (u32)ranges[i][1], o;
			for (o = 0; o < len; o++) {
				u64 v = va + o, p = v & ~4095ULL;
				for (k = 0; k < ncover; k++)
					if (cover[k] == p)
						break;
				assert(k < ncover);
				backing[k * 4096 + (v & 4095)] = pattern(v);
			}
		}
		/* One 4 KiB binding per cover page, all against the arena. */
		for (k = 0; k < ncover; k++) {
			struct mt_vm_binding b = {&arena, cover[k],
						  k * 4096, 4096, 0};
			int ret = mt_gpu_vm_bind_many(&vm, &b, 1);
			assert(!ret);
		}
		assert(vm.count == ncover);
		npages = ncover;
		/* Byte-exact readback of all 12 ranges through the tables. */
		for (i = 0; i < NRANGES; i++) {
			u64 va = ranges[i][0];
			u32 len = (u32)ranges[i][1], o;
			for (o = 0; o < len; o++) {
				u64 v = va + o, pa = walk(&vm, v, &pte);
				u64 got;
				assert(pa);
				got = pa - arena.backing.gpu_pa;
				assert(got < (u64)ncover * 4096);
				assert(backing[got] == pattern(v));
			}
		}
		printf("cover pages: %u for 12 ranges, %llu bytes verified\n",
		       ncover, (unsigned long long)0x49c93);
		free(page_pa);
		arena.page_pa = NULL;
	}
	assert(vm.count == npages);
	assert(!mt_gpu_vm_fini(&vm));
	assert(!mt_bo_put(&tables) && !mt_bo_put(&arena));
	free(image);
	free(scratch);
	puts("PASS: naive round-down overlaps (-EEXIST), unaligned refused (-EINVAL); "
	     "arena plus per-page bindings covers all 12 kick ranges byte-exact");
	return 0;
}
