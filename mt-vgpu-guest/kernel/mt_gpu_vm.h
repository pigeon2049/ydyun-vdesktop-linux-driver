/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_GPU_VM_H
#define MT_GUEST_GPU_VM_H
#ifndef __KERNEL__
#include <stdbool.h>
#include <stdlib.h>
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifndef WARN_ON
#define WARN_ON(condition) (!!(condition))
#endif
#define mt_gpu_vm_zalloc(n) calloc(1, (n))
#define mt_gpu_vm_free(p) free(p)
#else
#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#define mt_gpu_vm_zalloc(n) kvcalloc(1, (n), GFP_KERNEL)
#define mt_gpu_vm_free(p) kvfree(p)
#endif
#include "mt_bo.h"
#include "mt_mmu_bootstrap.h"

/* Private, initially unpublished address space. Editing is transactional in
 * CPU memory; it never rewrites an active root. A sealed VM cannot be edited
 * while live. Destroy of an idle sealed VM (no active uses or owners)
 * automatically reopens it first, so teardown never strands references;
 * direct fini stays strict and still refuses a sealed VM. There is still no
 * context-withdrawal/TLB protocol for anything fancier than idle teardown.
 * BO containers, not just their backing, must outlive all VM references.
 *
 * The binding table and the planner scratch are allocated separately from the
 * VM object and grown on demand, so mapping count is limited only by the
 * page-table page budget and never by a fixed inline array. The ceiling is
 * mt_gpu_vm_max_ranges(): the number of table entries the budget can describe.
 * Callers must not copy a live mt_gpu_vm; the arrays are shared, not duplicated.
 */
struct mt_vm_binding {
	struct mt_bo *bo;
	u64 va;
	u32 offset, bytes, flags;
};
struct mt_gpu_vm {
	struct mt_bo *tables;
	void *image, *scratch;
	u32 capacity, used_pages, count, active_uses, owners;
	bool uploaded, sealed;
	struct mt_vm_binding *bindings;
	struct mt_mmu_range *ranges;
	const u64 **page_lists;
	u32 binding_capacity;
	u32 max_ranges;
};

/* Mappings this table-page budget can describe. The compile-time page ceiling
 * is the only remaining cap, so the effective limit is thousands of mappings
 * rather than the previous fixed 24. */
static inline u32 mt_gpu_vm_max_ranges(u32 capacity)
{
	u32 budget = capacity / 4096;
	if (budget > MT_BOOT_MAX_TABLE_PAGES)
		budget = MT_BOOT_MAX_TABLE_PAGES;
	return mt_boot_max_ranges(budget);
}

/* Grow-on-demand storage. Rounds up to a power of two so repeated binds do not
 * reallocate on every call. On any failure the VM keeps its old arrays and
 * count, so a rejected bind cannot corrupt an existing plan. */
static inline int mt_gpu_vm_grow(struct mt_gpu_vm *vm, u32 need)
{
	u32 want = 1, bytes;
	struct mt_vm_binding *bindings;
	struct mt_mmu_range *ranges;
	const u64 **page_lists;
	if (need <= vm->binding_capacity)
		return 0;
	if (need > vm->max_ranges)
		return -ENOSPC;
	while (want < need) {
		if (want > 1u << 20)
			return -ENOSPC;
		want <<= 1;
	}
	bytes = (u32)(want * sizeof(*bindings));
	bindings = mt_gpu_vm_zalloc(bytes);
	ranges = mt_gpu_vm_zalloc((size_t)want * sizeof(*ranges));
	page_lists = mt_gpu_vm_zalloc((size_t)want * sizeof(*page_lists));
	if (!bindings || !ranges || !page_lists) {
		mt_gpu_vm_free(bindings);
		mt_gpu_vm_free(ranges);
		mt_gpu_vm_free(page_lists);
		return -ENOMEM;
	}
	if (vm->count)
		memcpy(bindings, vm->bindings, (size_t)vm->count * sizeof(*bindings));
	mt_gpu_vm_free(vm->bindings);
	mt_gpu_vm_free(vm->ranges);
	mt_gpu_vm_free(vm->page_lists);
	vm->bindings = bindings;
	vm->ranges = ranges;
	vm->page_lists = page_lists;
	vm->binding_capacity = want;
	return 0;
}

/* Image and scratch must not overlap (r276): the old gap check
 * (distance >= capacity) misfired on large VMs whose two adjacent
 * allocations sit closer than one full capacity apart.
 */
static inline int mt_gpu_vm_init(struct mt_gpu_vm *vm, struct mt_bo *tables,
		void *image, void *scratch, u32 capacity)
{
	u32 max_ranges;
	int ret;
	unsigned long a = (unsigned long)image, b = (unsigned long)scratch;
	if (!vm || vm->tables || !tables || !tables->refs || tables->page_pa || !image || !scratch ||
	    capacity < 4096 || capacity > MT_BOOT_MAX_TABLE_PAGES * 4096 ||
	    (capacity & 4095) || capacity > tables->backing.bytes ||
	    (a < b + capacity && b < a + capacity) ||
	    tables->backing.gpu_pa > (1ULL << MT_GPU_VA_BITS) - capacity)
		return -EINVAL;
	if (tables->cpu_users || tables->gpu_users)
		return -EBUSY;
	/* A budget that cannot describe a single mapping is unusable. */
	max_ranges = mt_gpu_vm_max_ranges(capacity);
	if (!max_ranges)
		return -EINVAL;
	ret = mt_bo_get(tables);
	if (ret)
		return ret;
	memset(image, 0, capacity);
	memset(scratch, 0, capacity);
	*vm = (struct mt_gpu_vm){.tables = tables, .image = image, .scratch = scratch,
		.capacity = capacity, .used_pages = 1, .max_ranges = max_ranges};
	return mt_gpu_vm_grow(vm, 1);
}

static inline int mt_gpu_vm_plan(struct mt_gpu_vm *vm,
		const struct mt_vm_binding *bindings, u32 count, u32 *pages)
{
	u32 i;
	memset(vm->scratch, 0, vm->capacity);
	if (!count) {
		*pages = 1;
		return 0;
	}
	/* The planner runs before any commit, so writing vm->ranges/page_lists here
	 * cannot disturb the published image or the current bindings. Each entry
	 * is assigned unconditionally: a stale pointer from a previous plan would
	 * otherwise be reused for a contiguous backing store. */
	for (i = 0; i < count; i++) {
		const struct mt_vm_binding *b = &bindings[i];
		vm->page_lists[i] = b->bo->page_pa ?
			b->bo->page_pa + b->offset / 4096 : NULL;
		vm->ranges[i] = (struct mt_mmu_range){.va = b->va,
			.pa = vm->page_lists[i] ? vm->page_lists[i][0] :
				b->bo->backing.gpu_pa + b->offset,
			.size = b->bytes, .flags = b->flags};
	}
	return mt_mmu_build_pages(vm->scratch, vm->capacity,
		vm->tables->backing.gpu_pa, vm->ranges, vm->page_lists, count, pages);
}

/* Publish a completed plan. Callers have already written the final binding
 * array in place and set vm->count, so this only clears the retired tail. */
static inline void mt_gpu_vm_commit(struct mt_gpu_vm *vm, u32 pages)
{
	/* Zeroed trailing pages are copied too: a smaller plan cannot retain
	 * stale leaf entries from an earlier unpublished upload. */
	memcpy(vm->image, vm->scratch, vm->capacity);
	if (vm->count < vm->binding_capacity)
		memset(vm->bindings + vm->count, 0,
		       (size_t)(vm->binding_capacity - vm->count) * sizeof(*vm->bindings));
	vm->used_pages = pages;
	vm->uploaded = false;
}

/* Install a complete shared-resource set atomically. No mapping or reference
 * from the batch survives a failed validation or reference acquisition. */
static inline int mt_gpu_vm_bind_many(struct mt_gpu_vm *vm,
		const struct mt_vm_binding *bindings, u32 count)
{
	u32 total, pages, i, j;
	int ret;
	if (!vm || !vm->tables || !bindings || !count)
		return -EINVAL;
	if (vm->sealed || vm->active_uses)
		return -EBUSY;
	/* Reject an unrepresentable total before touching any state. */
	if (count > vm->max_ranges - vm->count)
		return -ENOSPC;
	/* Validate the whole batch before growing, so a rejected request leaves
	 * the array capacity, the image, the count and every reference as they
	 * were. Overlap is checked here rather than only inside the planner, so a
	 * conflicting request never grows the array either. */
	for (i = 0; i < count; i++) {
		const struct mt_vm_binding *b = &bindings[i];
		struct mt_bo *bo = b->bo;
		u64 va_end;
		if (!bo || !bo->refs || bo == vm->tables || !b->bytes ||
		    ((b->va | b->offset | b->bytes) & 4095) || (b->flags & ~0x1fU))
			return -EINVAL;
		if (bo->store != vm->tables->store || bo->ops != vm->tables->ops)
			return -EXDEV;
		ret = mt_bo_check_range(bo, b->offset, b->bytes);
		if (ret)
			return ret;
		for (j = 0; j < b->bytes; j += 4096) {
			u64 pa = bo->page_pa ? bo->page_pa[(b->offset + j) / 4096] :
				bo->backing.gpu_pa + b->offset + j;
			if (pa < vm->tables->backing.gpu_pa + vm->capacity &&
			    vm->tables->backing.gpu_pa < pa + 4096)
				return -EINVAL;
		}
		/* Same acceptance as the planner's own range preflight, so hoisting
		 * these checks here cannot change which errno a caller observes. */
		if (b->va >= (1ULL << MT_GPU_VA_BITS))
			return -EINVAL;
		if (b->bytes > (1ULL << MT_GPU_VA_BITS) - b->va)
			return -ERANGE;
		va_end = b->va + b->bytes;
		for (j = 0; j < vm->count; j++)
			if (b->va < vm->bindings[j].va + vm->bindings[j].bytes &&
			    vm->bindings[j].va < va_end)
				return -EEXIST;
		for (j = 0; j < i; j++)
			if (b->va < bindings[j].va + bindings[j].bytes &&
			    bindings[j].va < va_end)
				return -EEXIST;
	}
	total = vm->count + count;
	ret = mt_gpu_vm_grow(vm, total);
	if (ret)
		return ret;
	/* Stage into the tail of the live array. A failure past this point leaves
	 * vm->count untouched, so the staged tail is never observable. */
	for (i = 0; i < count; i++)
		vm->bindings[vm->count + i] = bindings[i];
	/* plan() reads the full range array; the tail is the staged batch. */
	ret = mt_gpu_vm_plan(vm, vm->bindings, total, &pages);
	if (ret)
		return ret;
	for (i = 0; i < count; i++) {
		ret = mt_bo_get(vm->bindings[vm->count + i].bo);
		if (ret) {
			while (i)
				mt_bo_put(vm->bindings[vm->count + --i].bo);
			return ret;
		}
	}
	vm->count = total;
	mt_gpu_vm_commit(vm, pages);
	return 0;
}

static inline int mt_gpu_vm_bind(struct mt_gpu_vm *vm, struct mt_bo *bo,
		u64 va, u32 offset, u32 bytes, u32 flags)
{
	struct mt_vm_binding b = {bo, va, offset, bytes, flags};
	return mt_gpu_vm_bind_many(vm, &b, 1);
}

/* Exact-range unbind, not silent partial removal. Callers may split BOs into
 * separate bindings when partial-range lifetime is needed. */
static inline int mt_gpu_vm_unbind(struct mt_gpu_vm *vm, u64 va, u32 bytes)
{
	struct mt_bo *bo;
	u32 i, at, pages, kept = 0;
	int ret;
	if (!vm || !vm->tables)
		return -EINVAL;
	if (vm->sealed || vm->active_uses)
		return -EBUSY;
	for (at = 0; at < vm->count; at++)
		if (vm->bindings[at].va == va && vm->bindings[at].bytes == bytes)
			break;
	if (at == vm->count)
		return -ENOENT;
	bo = vm->bindings[at].bo;
	/* Close the gap in place. The removed slot is still readable at index
	 * `at` if the plan below fails, so vm->count stays authoritative. */
	for (i = 0; i < vm->count; i++) {
		if (i == at)
			continue;
		vm->bindings[kept++] = vm->bindings[i];
	}
	ret = mt_gpu_vm_plan(vm, vm->bindings, kept, &pages);
	if (ret)
		return ret;
	/* This VM owns an ordinary reference in addition to any active uses. */
	ret = mt_bo_put(bo);
	if (ret)
		return ret;
	vm->count = kept;
	mt_gpu_vm_commit(vm, pages);
	return 0;
}

static inline int mt_gpu_vm_seal(struct mt_gpu_vm *vm)
{
	if (!vm || !vm->tables || !vm->count || !vm->uploaded)
		return -EINVAL;
	if (vm->sealed)
		return -EALREADY;
	vm->sealed = true;
	return 0;
}

/* Teardown assist: reopen an idle sealed VM so destroy can release every
 * reference it owns. Never reopens a VM the GPU may still use; direct fini
 * stays strict and still returns -EBUSY on a sealed VM. */
static inline int mt_gpu_vm_unseal(struct mt_gpu_vm *vm)
{
	if (!vm || !vm->tables)
		return -EINVAL;
	if (!vm->sealed)
		return -EALREADY;
	if (vm->active_uses || vm->owners)
		return -EBUSY;
	vm->sealed = false;
	return 0;
}

static inline int mt_gpu_vm_fini(struct mt_gpu_vm *vm)
{
	u32 i, j, owned;
	if (!vm || !vm->tables)
		return -EINVAL;
	if (vm->sealed || vm->active_uses || vm->owners)
		return -EBUSY;
	if (!vm->bindings || vm->count > vm->binding_capacity || vm->count > vm->max_ranges)
		return -EUCLEAN;
	/* Preflight every reference drop before releasing any BO. A damaged or
	 * otherwise unbalanced reference count must not leave a half-destroyed VM.
	 * Each binding owns one ref; CPU/GPU users own their own independent refs.
	 */
	if (!vm->tables->refs || vm->tables->refs <=
	    (u64)vm->tables->cpu_users + vm->tables->gpu_users)
		return -EUCLEAN;
	for (i = 0; i < vm->count; i++) {
		struct mt_bo *bo = vm->bindings[i].bo;
		bool seen = false;

		if (!bo || !bo->refs || bo == vm->tables)
			return -EUCLEAN;
		for (j = 0; j < i; j++)
			if (vm->bindings[j].bo == bo) {
				seen = true;
				break;
			}
		if (seen)
			continue;
		owned = 0;
		for (j = i; j < vm->count; j++)
			owned += vm->bindings[j].bo == bo;
		if ((u64)bo->refs < (u64)bo->cpu_users + bo->gpu_users + owned)
			return -EUCLEAN;
	}
	/* The caller owns the CPU buffers. No GPU can use this unsealed root. */
	for (i = 0; i < vm->count; i++) {
		int ret = mt_bo_put(vm->bindings[i].bo);

		if (WARN_ON(ret))
			return ret;
	}
	{
		int ret = mt_bo_put(vm->tables);

		if (WARN_ON(ret))
			return ret;
	}
	mt_gpu_vm_free(vm->bindings);
	mt_gpu_vm_free(vm->ranges);
	mt_gpu_vm_free(vm->page_lists);
	memset(vm, 0, sizeof(*vm));
	return 0;
}
#endif
