/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_GPU_VM_H
#define MT_GUEST_GPU_VM_H
#ifndef __KERNEL__
#include <stdbool.h>
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifndef WARN_ON
#define WARN_ON(condition) (!!(condition))
#endif
#else
#include <linux/bug.h>
#include <linux/kernel.h>
#endif
#include "mt_bo.h"
#include "mt_mmu_bootstrap.h"

/* Private, initially unpublished address space. Editing is transactional in
 * CPU memory; it never rewrites an active root. A sealed VM cannot be edited
 * or freed until a real context-withdrawal/TLB protocol is implemented.
 * BO containers, not just their backing, must outlive all VM references.
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
	struct mt_vm_binding bindings[MT_BOOT_MAX_RANGES];
};

static inline int mt_gpu_vm_init(struct mt_gpu_vm *vm, struct mt_bo *tables,
		void *image, void *scratch, u32 capacity)
{
	unsigned long a = (unsigned long)image, b = (unsigned long)scratch;
	int ret;
	if (!vm || vm->tables || !tables || !tables->refs || tables->page_pa || !image || !scratch ||
	    capacity < 4096 || capacity > MT_BOOT_MAX_TABLE_PAGES * 4096 ||
	    (capacity & 4095) || capacity > tables->backing.bytes ||
	    (a > b ? a - b : b - a) < capacity ||
	    tables->backing.gpu_pa > (1ULL << MT_GPU_VA_BITS) - capacity)
		return -EINVAL;
	if (tables->cpu_users || tables->gpu_users)
		return -EBUSY;
	ret = mt_bo_get(tables);
	if (ret)
		return ret;
	memset(image, 0, capacity);
	memset(scratch, 0, capacity);
	*vm = (struct mt_gpu_vm){.tables = tables, .image = image, .scratch = scratch,
		.capacity = capacity, .used_pages = 1};
	return 0;
}

static inline int mt_gpu_vm_plan(struct mt_gpu_vm *vm,
		const struct mt_vm_binding *bindings, u32 count, u32 *pages)
{
	struct mt_mmu_range ranges[MT_BOOT_MAX_RANGES];
	const u64 *page_lists[MT_BOOT_MAX_RANGES] = {0};
	u32 i;
	memset(vm->scratch, 0, vm->capacity);
	if (!count) {
		*pages = 1;
		return 0;
	}
	for (i = 0; i < count; i++) {
		const struct mt_vm_binding *b = &bindings[i];
		if (b->bo->page_pa)
			page_lists[i] = b->bo->page_pa + b->offset / 4096;
		ranges[i] = (struct mt_mmu_range){.va = b->va,
			.pa = page_lists[i] ? page_lists[i][0] : b->bo->backing.gpu_pa + b->offset,
			.size = b->bytes, .flags = b->flags};
	}
	return mt_mmu_build_pages(vm->scratch, vm->capacity,
		vm->tables->backing.gpu_pa, ranges, page_lists, count, pages);
}

static inline void mt_gpu_vm_commit(struct mt_gpu_vm *vm,
		const struct mt_vm_binding *bindings, u32 count, u32 pages)
{
	/* Zeroed trailing pages are copied too: a smaller plan cannot retain
	 * stale leaf entries from an earlier unpublished upload. */
	memcpy(vm->image, vm->scratch, vm->capacity);
	memset(vm->bindings, 0, sizeof(vm->bindings));
	memcpy(vm->bindings, bindings, count * sizeof(*bindings));
	vm->count = count;
	vm->used_pages = pages;
	vm->uploaded = false;
}

/* Install a complete shared-resource set atomically. No mapping or reference
 * from the batch survives a failed validation or reference acquisition. */
static inline int mt_gpu_vm_bind_many(struct mt_gpu_vm *vm,
		const struct mt_vm_binding *bindings, u32 count)
{
	struct mt_vm_binding next[MT_BOOT_MAX_RANGES];
	u32 pages, i, j;
	int ret;
	if (!vm || !vm->tables || !bindings || !count)
		return -EINVAL;
	if (vm->sealed || vm->active_uses)
		return -EBUSY;
	if (count > MT_BOOT_MAX_RANGES - vm->count)
		return -ENOSPC;
	memcpy(next, vm->bindings, vm->count * sizeof(*next));
	for (i = 0; i < count; i++) {
		const struct mt_vm_binding *b = &bindings[i];
		struct mt_bo *bo = b->bo;
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
		next[vm->count + i] = *b;
	}
	ret = mt_gpu_vm_plan(vm, next, vm->count + count, &pages);
	if (ret)
		return ret;
	for (i = 0; i < count; i++) {
		ret = mt_bo_get(next[vm->count + i].bo);
		if (ret) {
			while (i)
				mt_bo_put(next[vm->count + --i].bo);
			return ret;
		}
	}
	mt_gpu_vm_commit(vm, next, vm->count + count, pages);
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
	struct mt_vm_binding next[MT_BOOT_MAX_RANGES];
	struct mt_bo *bo;
	u32 i, at, pages;
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
	for (i = 0; i < vm->count; i++)
		if (i != at)
			next[i - (i > at)] = vm->bindings[i];
	ret = mt_gpu_vm_plan(vm, next, vm->count - 1, &pages);
	if (ret)
		return ret;
	/* This VM owns an ordinary reference in addition to any active uses. */
	ret = mt_bo_put(bo);
	if (ret)
		return ret;
	mt_gpu_vm_commit(vm, next, vm->count - 1, pages);
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

static inline int mt_gpu_vm_fini(struct mt_gpu_vm *vm)
{
	u32 i, j, owned;
	if (!vm || !vm->tables)
		return -EINVAL;
	if (vm->sealed || vm->active_uses || vm->owners)
		return -EBUSY;
	if (vm->count > ARRAY_SIZE(vm->bindings))
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
	memset(vm, 0, sizeof(*vm));
	return 0;
}
#endif
