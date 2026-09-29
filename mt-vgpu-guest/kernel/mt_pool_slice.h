/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_POOL_SLICE_H
#define MT_GUEST_POOL_SLICE_H
#include "mt_execution_context.h"
#include "mt_guest_heaps.h"

struct mt_pool_slice;
struct mt_pool_state {
	u64 used[8]; /* Largest current pool: 512 4 KiB pages. */
	struct mt_pool_slice *slices;
};
/* Caller-owned, address-stable and non-copyable until released. All methods
 * hold the BO/VM session lock. Linux uses first-fit; Windows uses size bins.
 * Returned addresses, not identical placement under fragmentation, are the
 * contract. No CPU mapping, content writes or GPU publication occur here. */
struct mt_pool_slice {
	struct mt_pool_state *state;
	struct mt_pool_slice *next;
	struct mt_execution_context *context;
	struct mt_bo *bo;
	u64 va;
	u32 offset, bytes;
};

static inline int mt_pool_slice_alloc(struct mt_pool_state *state,
		struct mt_execution_context *context, struct mt_bo *bo,
		const struct mt_guest_pool_spec *spec, u32 bytes, struct mt_pool_slice *out)
{
	struct mt_gpu_vm *vm;
	u32 first, i, pages, limit;
	int ret;
	if (!state || !out || out->state || !context || !context->process || !bo || !spec ||
	    !bytes || !spec->bytes || spec->bytes > 512 * 4096 || (spec->bytes & 4095) ||
	    bytes > spec->bytes || bo->backing.bytes < spec->bytes)
		return -EINVAL;
	vm = context->process->vm;
	if (bo->store != context->process->store->buffers)
		return -EXDEV;
	if (state->slices && (state->slices->bo != bo ||
	    state->slices->va - state->slices->offset != spec->va))
		return -EXDEV;
	/* A sealed root may lease pages inside an already-bound pool BO: this only
	 * changes allocator metadata, not VM mappings. Require the sealed root to
	 * have been uploaded, and keep the usual no-active-use exclusion. */
	if (vm->sealed && !vm->uploaded)
		return -EINVAL;
	if (vm->active_uses || context->active_jobs || bo->cpu_users || bo->gpu_users)
		return -EBUSY;
	if (context->pool_slices == ~(u32)0)
		return -EOVERFLOW;
	for (i = 0; i < vm->count; i++)
		if (vm->bindings[i].bo == bo && vm->bindings[i].va == spec->va &&
		    !vm->bindings[i].offset && vm->bindings[i].bytes == spec->bytes)
			break;
	if (i == vm->count)
		return -ENOENT;
	pages = (bytes + 4095) / 4096;
	limit = spec->bytes / 4096;
	for (first = 0; first <= limit - pages; first++) {
		for (i = 0; i < pages; i++)
			if (state->used[(first + i) / 64] & (1ULL << ((first + i) % 64)))
				break;
		if (i == pages)
			break;
		first += i;
	}
	if (first > limit - pages)
		return -ENOSPC;
	ret = mt_bo_get(bo);
	if (ret)
		return ret;
	for (i = 0; i < pages; i++)
		state->used[(first + i) / 64] |= 1ULL << ((first + i) % 64);
	*out = (struct mt_pool_slice){.state = state, .next = state->slices,
		.context = context, .bo = bo, .va = spec->va + first * 4096ULL,
		.offset = first * 4096, .bytes = pages * 4096};
	state->slices = out;
	context->pool_slices++;
	return 0;
}

static inline int mt_pool_slice_free(struct mt_pool_slice *slice)
{
	struct mt_pool_slice **link;
	u32 i, first, pages;
	int ret;
	if (!slice || !slice->state)
		return -EINVAL;
	for (link = &slice->state->slices; *link && *link != slice; link = &(*link)->next)
		;
	if (!*link) /* Reject a copied lease rather than releasing the real owner. */
		return -EINVAL;
	/* Freeing a slice only returns its pages to the pool bitmap. It does not
	 * unbind or rewrite the sealed VM's page table. */
	if (slice->context->active_jobs || slice->bo->cpu_users || slice->bo->gpu_users)
		return -EBUSY;
	ret = mt_bo_put(slice->bo);
	if (ret)
		return ret;
	first = slice->offset / 4096;
	pages = slice->bytes / 4096;
	for (i = 0; i < pages; i++)
		slice->state->used[(first + i) / 64] &= ~(1ULL << ((first + i) % 64));
	*link = slice->next;
	slice->context->pool_slices--;
	memset(slice, 0, sizeof(*slice));
	return 0;
}
#endif
