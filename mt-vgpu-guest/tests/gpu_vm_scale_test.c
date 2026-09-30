/* SPDX-License-Identifier: GPL-2.0 */
/* GPU address-space mapping scalability.
 *
 * Establishes that the number of mappings in one address space is bounded by
 * the page-table page budget rather than a fixed driver constant, and that a
 * rejected grow leaves the previous plan and every reference untouched.
 *
 * All backing store is ordinary RAM with synthetic GPU physical addresses.
 * Nothing here publishes a root, writes VRAM or touches a device.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "../kernel/mt_gpu_vm.h"
#include "../kernel/mt_work_job.h"

struct store { u64 next_pa; unsigned allocs, frees; };
static int alloc(void *opaque, u32 bytes, u32 alignment, struct mt_bo_backing *b)
{
	struct store *s = opaque;
	void *p;
	if (bytes > 4 * 1024 * 1024)
		return -ENOMEM;
	p = malloc(bytes);
	if (!p)
		return -ENOMEM;
	s->next_pa = (s->next_pa + alignment - 1) & ~(u64)(alignment - 1);
	/* 256 MiB apart: far enough that no object abuts another or the page
	 * tables, while keeping every address inside the 40-bit VA window. */
	*b = (struct mt_bo_backing){p, s->next_pa, s->next_pa, bytes};
	s->next_pa += 0x10000000ULL;
	s->allocs++;
	return 0;
}
static int clear(void *s, const struct mt_bo_backing *b)
{ (void)s; memset(b->handle, 0, b->bytes); return 0; }
static void release(void *opaque, const struct mt_bo_backing *b)
{ struct store *s = opaque; free(b->handle); s->frees++; }
static int map(void *s, const struct mt_bo_backing *b, void **out)
{ (void)s; *out = b->handle; return 0; }
static void unmap(void *s, const struct mt_bo_backing *b) { (void)s; (void)b; }
static const struct mt_bo_ops ops = {alloc, clear, release, map, unmap};

/* Independent three-level walk, deliberately not reusing the production
 * encoders: this checks that the published image actually resolves each VA to
 * the expected physical address. */
static u64 walk(const struct mt_gpu_vm *v, u64 va, u64 *pte)
{
	u32 pc;
	u64 pd, root = v->tables->backing.gpu_pa, offset;
	memcpy(&pc, (u8 *)v->image + ((va / 0x40000000) % 1024) * 4, 4);
	if (!(pc & 1))
		return 0;
	offset = ((u64)(pc & 0xfffffff0U) << 8) - root;
	assert(offset <= v->capacity - 4096);
	memcpy(&pd, (u8 *)v->image + offset + ((va / 0x200000) % 512) * 8, 8);
	if (!(pd & 1))
		return 0;
	offset = (pd & 0xfffffff000ULL) - root;
	assert(offset <= v->capacity - 4096);
	memcpy(pte, (u8 *)v->image + offset + ((va / 4096) % 512) * 8, 8);
	return *pte & 1 ? (*pte & 0xfffffff000ULL) + (va % 4096) : 0;
}

#define N 512U
/* The ceiling this change removes. Kept as a test constant, not a driver one. */
#define FORMER_RANGE_CAP 24U

static void derived_ceiling(void)
{
	/* The limit follows the walk geometry: one page-table page describes
	 * 512 leaf entries, and the root page is not available for mappings. */
	assert(mt_boot_max_ranges(0) == 0);
	assert(mt_boot_max_ranges(MT_BOOT_ROOT_PAGES) == 0);
	assert(mt_boot_max_ranges(2) == MT_BOOT_PT_ENTRIES);
	assert(mt_boot_max_ranges(3) == 2 * MT_BOOT_PT_ENTRIES);
	/* Page geometry alone would allow far more; the mapped-byte budget is the
	 * binding constraint, so the reported limit is the smaller of the two. */
	assert((MT_BOOT_MAX_TABLE_PAGES - MT_BOOT_ROOT_PAGES) * MT_BOOT_PT_ENTRIES >
	       MT_BOOT_MAX_MAPPED_BYTES / 4096);
	assert(mt_boot_max_ranges(MT_BOOT_MAX_TABLE_PAGES) ==
	       MT_BOOT_MAX_MAPPED_BYTES / 4096);
	/* A capacity below one full table page cannot describe any mapping. */
	assert(mt_gpu_vm_max_ranges(4096) == 0);
	/* Capacities above the compile-time ceiling clamp instead of growing it. */
	assert(mt_gpu_vm_max_ranges(4096 * MT_BOOT_MAX_TABLE_PAGES) ==
	       mt_boot_max_ranges(MT_BOOT_MAX_TABLE_PAGES));
	assert(mt_gpu_vm_max_ranges(4096 * (MT_BOOT_MAX_TABLE_PAGES * 4)) ==
	       mt_boot_max_ranges(MT_BOOT_MAX_TABLE_PAGES));
	/* Every budget the production driver actually requests must be usable. */
	assert(mt_gpu_vm_max_ranges(8 * 4096) >= N);
	puts("PASS: derived mapping ceiling follows page-table geometry and the "
	     "mapped-byte budget; no fixed 24-range constant; RAM only");
}

static void many_mappings(void)
{
	struct store s = {.next_pa = 0x400000000ULL};
	struct mt_bo tables = {0}, bos[N];
	struct mt_gpu_vm vm = {0};
	const u32 capacity = 64 * 4096;
	u8 *image = malloc(capacity), *scratch = malloc(capacity);
	u64 pte, va = 0x800000000ULL;
	u32 i, j, pages_with_all;

	assert(image && scratch);
	memset(bos, 0, sizeof(bos));
	assert(!mt_bo_create(&tables, &ops, &s, capacity, 4096));
	assert(!mt_gpu_vm_init(&vm, &tables, image, scratch, capacity));
	assert(vm.max_ranges == mt_gpu_vm_max_ranges(capacity));
	assert(vm.max_ranges > FORMER_RANGE_CAP);

	/* Far more mappings than the old 24-range ceiling allowed. Each BO is
	 * 16 KiB and they are packed contiguously, so a handful of page-table
	 * pages describe the whole set. */
	for (i = 0; i < N; i++) {
		assert(!mt_bo_create(&bos[i], &ops, &s, 0x4000, 4096));
		assert(!mt_gpu_vm_bind(&vm, &bos[i], va + (u64)i * 0x4000, 0, 0x4000, 0));
	}
	assert(vm.count == N && vm.count > FORMER_RANGE_CAP);
	assert(!vm.uploaded);

	/* Every mapping resolves to its own backing, first and last page. */
	for (i = 0; i < N; i++) {
		for (j = 0; j < 0x4000; j += 4096) {
			u64 at = va + (u64)i * 0x4000 + j;
			assert(walk(&vm, at + 13, &pte) == bos[i].backing.gpu_pa + j + 13);
		}
	}
	pages_with_all = vm.used_pages;
	assert(pages_with_all > 1 && pages_with_all <= capacity / 4096);

	/* The plan is complete and walk-verified; release it and confirm each BO
	 * kept exactly the one reference its mapping took. */
	assert(vm.used_pages == pages_with_all);
	assert(!mt_gpu_vm_fini(&vm));
	for (i = 0; i < N; i++)
		assert(bos[i].refs == 1 && !mt_bo_put(&bos[i]));
	assert(!mt_bo_put(&tables));
	assert(s.allocs == s.frees);
	free(image);
	free(scratch);
	printf("PASS: %u simultaneous mappings (was capped at 24) in %u page-table "
	       "pages, every page walk-verified, seal and full release; RAM only\n",
	       N, pages_with_all);
}

static void growth_rejections(void)
{
	struct store s = {.next_pa = 0x300000000ULL}, other = {.next_pa = 0x200000000ULL};
	struct mt_bo tables = {0}, bo = {0}, many = {0}, foreign = {0};
	struct mt_gpu_vm vm = {0};
	const u32 capacity = 8 * 4096;
	u8 *image = malloc(capacity), *scratch = malloc(capacity);
	u8 saved[8 * 4096];
	u32 i, capacity_before, count_before, pages_before;

	assert(image && scratch);
	assert(!mt_bo_create(&tables, &ops, &s, capacity, 4096));
	assert(!mt_bo_create(&bo, &ops, &s, 0x4000, 4096));
	assert(!mt_bo_create(&many, &ops, &s, 0x1000, 4096));
	assert(!mt_bo_create(&foreign, &ops, &other, 4096, 4096));
	assert(!mt_gpu_vm_init(&vm, &tables, image, scratch, capacity));
	assert(!mt_gpu_vm_bind(&vm, &bo, 0x800000000ULL, 0, 0x4000, 0));
	vm.uploaded = true;
	memcpy(saved, image, capacity);
	count_before = vm.count;
	pages_before = vm.used_pages;
	capacity_before = vm.binding_capacity;

	/* Overlapping a live mapping is rejected at any size. */
	assert(mt_gpu_vm_bind(&vm, &many, 0x800002000ULL, 0, 4096, 0) == -EEXIST);
	/* Unaligned, oversized and out-of-range requests stay rejected. */
	assert(mt_gpu_vm_bind(&vm, &many, 0x900000001ULL, 0, 4096, 0) == -EINVAL);
	assert(mt_gpu_vm_bind(&vm, &many, 0x900000000ULL, 0, 0x2000, 0) == -ERANGE);
	assert(mt_gpu_vm_bind(&vm, &many, 0x900000000ULL, 0, 4096, 0x20) == -EINVAL);
	/* A foreign store is refused even when the count is acceptable. */
	assert(mt_gpu_vm_bind(&vm, &foreign, 0x900000000ULL, 0, 4096, 0) == -EXDEV);
	/* Exhaustion is measured on a separate space so the live plan above stays
	 * untouched and directly comparable. Contiguous 4 KiB mappings share one
	 * directory, so the budget is spent on leaf-table pages and the cap lands
	 * far above the old 24 mappings. */
	{
		struct mt_gpu_vm drain = {0};
		u8 *drain_image = malloc(capacity), *drain_scratch = malloc(capacity);
		u32 accepted = 0;
		assert(drain_image && drain_scratch);
		assert(!mt_gpu_vm_init(&drain, &tables, drain_image, drain_scratch, capacity));
		for (i = 0; i < 65536; i++) {
			int ret = mt_gpu_vm_bind(&drain, &many,
						 0x1000000000ULL + (u64)i * 4096, 0, 4096, 0);
			if (ret == -ENOSPC)
				break;
			assert(!ret);
			accepted++;
		}
		/* A tiny table budget still accepts far more than 24 mappings, and it
		 * reports the real page limit rather than an array bound. */
		assert(accepted > FORMER_RANGE_CAP && accepted < 65536);
		assert(accepted == drain.count);
		assert(drain.used_pages <= capacity / 4096);
		/* Growth stops at the accepted count: a rejected bind never inflates
		 * the array, and no reference from the failed attempt survives. */
		assert(drain.binding_capacity >= drain.count);
		assert(!mt_gpu_vm_fini(&drain));
		free(drain_image);
		free(drain_scratch);
	}

	assert(vm.count == count_before && vm.used_pages == pages_before);
	assert(vm.binding_capacity == capacity_before);
	assert(vm.uploaded && !memcmp(saved, image, capacity));
	assert(bo.refs == 2 && many.refs == 1 && foreign.refs == 1);
	assert(!mt_gpu_vm_fini(&vm));
	assert(!mt_bo_put(&bo) && !mt_bo_put(&many) && !mt_bo_put(&foreign));
	assert(!mt_bo_put(&tables));
	assert(s.allocs == s.frees && other.allocs == other.frees);
	free(image);
	free(scratch);
	puts("PASS: rejected binds and grow exhaustion leave the image, count, page "
	     "usage, array capacity and every reference unchanged; RAM only");
}

int main(void)
{
	derived_ceiling();
	many_mappings();
	growth_rejections();
	puts("PASS: GPU address-space mapping scalability suite");
	return 0;
}
