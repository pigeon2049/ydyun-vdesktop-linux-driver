/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_BOOT_BO_H
#define MT_GUEST_BOOT_BO_H
#include "mt_bo_vram.h"
#include "mt_boot_resources.h"
#include "mt_process_resources.h"
#include "mt_reserved_pools.h"

#define MT_BOOT_BO_COUNT (MT_PROCESS_SHARED_COUNT + MT_GUEST_POOL_COUNT)

/* Slots are weak cache pointers, serialized by the buffer-store lock. No
 * permanent BO/module reference: an unused boot session remains unloadable. */
struct mt_boot_bo_store {
	struct mt_boot_resources *boot;
	struct mt_system_memory *paging_command;
	struct mt_reserved_pools *pools;
	struct mt_bo_store *buffers;
	struct mt_bo *slots[MT_BOOT_BO_COUNT];
};
struct mt_boot_bo {
	struct mt_bo bo;
	struct mt_bo **slot;
};

static void mt_boot_bo_release(struct mt_bo *bo)
{
	struct mt_boot_bo *b = container_of(bo, struct mt_boot_bo, bo);
	WARN_ON(*b->slot != bo);
	*b->slot = NULL;
	kfree(b);
}

static inline bool mt_boot_bo_bound(const struct mt_boot_bo_store *s,
		const struct mt_gpu_vm *vm)
{
	static const u32 fixed_index[MT_PROCESS_SHARED_COUNT] = {0, 4, 10, 11, 6, 3};
	struct mt_guest_heap_plan plan;
	struct mt_guest_pool_spec pools[MT_GUEST_POOL_COUNT];
	struct mt_bo *bos[MT_BOOT_BO_COUNT];
	u64 va[MT_BOOT_BO_COUNT];
	u32 bytes[MT_BOOT_BO_COUNT], i, j;
	if (!s || !s->boot || !s->pools || !s->paging_command || !s->buffers || !vm ||
	    !s->pools->prepared)
		return false;
	mt_guest_plan_heaps(&plan);
	mt_guest_plan_pools(pools);
	for (i = 0; i < MT_PROCESS_SHARED_COUNT; i++) {
		bos[i] = s->slots[i];
		va[i] = plan.resources[fixed_index[i]].va;
		bytes[i] = i == MT_SHARED_PAGING_COMMAND ? MT_PAGING_COMMAND_BYTES :
			plan.resources[fixed_index[i]].size;
	}
	for (i = 0; i < MT_GUEST_POOL_COUNT; i++) {
		bos[MT_PROCESS_SHARED_COUNT + i] = s->slots[MT_PROCESS_SHARED_COUNT + i];
		va[MT_PROCESS_SHARED_COUNT + i] = pools[i].va;
		bytes[MT_PROCESS_SHARED_COUNT + i] = pools[i].bytes;
	}
	for (i = 0; i < MT_BOOT_BO_COUNT; i++) {
		if (!bos[i])
			return false;
		for (j = 0; j < vm->count; j++)
			if (vm->bindings[j].bo == bos[i] && vm->bindings[j].va == va[i] &&
			    !vm->bindings[j].offset && vm->bindings[j].bytes == bytes[i] &&
			    !vm->bindings[j].flags)
				break;
		if (j == vm->count)
			return false;
	}
	return true;
}

static inline int mt_boot_bo_can_release(const struct mt_boot_bo_store *s)
{
	u32 i;
	for (i = 0; i < MT_BOOT_BO_COUNT; i++)
		if (s->slots[i])
			return -EBUSY;
	return 0;
}

static inline int mt_boot_bo_init(struct mt_boot_bo_store *s,
		struct mt_bo_store *buffers, struct mt_boot_resources *boot,
		struct mt_system_memory *paging_command, struct mt_reserved_pools *pools)
{
	if (!s || s->boot || !buffers || !boot || !boot->prepared ||
	    !paging_command || paging_command->bytes != MT_PAGING_COMMAND_BYTES || !pools || !pools->prepared)
		return -EINVAL;
	*s = (struct mt_boot_bo_store){.boot = boot, .buffers = buffers,
		.paging_command = paging_command, .pools = pools};
	return 0;
}

/* Bind the existing boot allocations, preserving their bytes and ownership.
 * No upload/readiness/publication is implied by a successful mapping. */
static inline int mt_boot_bo_bind(struct mt_boot_bo_store *s,
		struct mt_gpu_vm *vm, const struct mt_device_profile *profile)
{
	static const u32 indices[MT_PROCESS_SHARED_COUNT] = {
		MT_BOOT_PB, MT_BOOT_PDS, MT_BOOT_YUV, MT_BOOT_KILL,
		MT_BOOT_FENCE, 0 /* paging command is separately owned */
	};
	struct mt_bo *bo[MT_BOOT_BO_COUNT] = {0};
	u32 i;
	int ret;
	if (!s || !s->boot || !s->boot->prepared || !s->paging_command || !s->buffers || !vm ||
	    !s->pools || !s->pools->prepared || !vm->tables || vm->tables->store != s->buffers)
		return -EXDEV;
	lockdep_assert_held(s->buffers->lock);
	if (mt_boot_bo_bound(s, vm))
		return 0;
	for (i = 0; i < MT_BOOT_BO_COUNT; i++) {
		if (s->slots[i]) {
			ret = mt_bo_get(s->slots[i]);
			if (ret)
				goto put;
			bo[i] = s->slots[i];
		} else {
			struct mt_boot_bo *b = kzalloc(sizeof(*b), GFP_KERNEL);
			if (!b) { ret = -ENOMEM; goto put; }
			ret = i == MT_SHARED_PAGING_COMMAND ?
				mt_bo_system_borrow(&b->bo, s->buffers, s->paging_command) :
				mt_bo_vram_borrow(&b->bo, s->buffers, i < MT_PROCESS_SHARED_COUNT ?
					&s->boot->blocks[indices[i]] : &s->pools->blocks[i - MT_PROCESS_SHARED_COUNT]);
			if (ret) { kfree(b); goto put; }
			b->slot = &s->slots[i];
			b->bo.release = mt_boot_bo_release;
			bo[i] = s->slots[i] = &b->bo;
		}
	}
	ret = mt_process_resources_bind_pools(vm, profile, bo, bo + MT_PROCESS_SHARED_COUNT);
put:
	for (i = 0; i < MT_BOOT_BO_COUNT; i++)
		if (bo[i])
			mt_bo_put(bo[i]);
	return ret;
}

/* 041cc4 pool-backed resource kinds. The caller supplies the request size
 * selected by its context policy; this does not invent the SDK policy. */
static inline int mt_boot_pool_alloc(struct mt_boot_bo_store *s,
		struct mt_execution_context *context, u32 kind, u32 bytes, struct mt_pool_slice *out)
{
	struct mt_guest_pool_spec specs[MT_GUEST_POOL_COUNT];
	u32 index;
	if (!s || !s->pools || !s->pools->prepared || !s->buffers || !context || !context->process)
		return -EINVAL;
	lockdep_assert_held(s->buffers->lock);
	if (context->process->store->buffers != s->buffers)
		return -EXDEV;
	if (context->process->store->profile.family != 2 ||
	    context->process->store->profile.transfer_version != 1)
		return -EOPNOTSUPP;
	switch (kind) {
	case 3: index = 2; break; /* Texture state, heap 10. */
	case 4: index = 0; break; /* PDS state, heap 1. */
	case 6: index = 1; break; /* USC code, heap 2. */
	default: return -EOPNOTSUPP;
	}
	if (!s->slots[MT_PROCESS_SHARED_COUNT + index])
		return -ENOENT;
	mt_guest_plan_pools(specs);
	return mt_pool_slice_alloc(&s->pools->slices[index], context,
		s->slots[MT_PROCESS_SHARED_COUNT + index], &specs[index], bytes, out);
}
#endif
