/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_BO_H
#define MT_GUEST_BO_H
#include "mt_mmu.h"

/* Internal backing object, not a userspace ABI. All calls for a store must
 * hold its session lock. CPU mappings and submitted GPU uses own references.
 * A GPU use is a lifetime pin only: VA binding, fences and submission belong
 * to the subsequent VM/scheduler layer and must precede hardware use.
 */
struct mt_bo_backing {
	void *handle;
	u64 gpu_pa, bar_offset;
	u32 bytes;
};
struct mt_bo_ops {
	int (*alloc)(void *, u32 bytes, u32 alignment, struct mt_bo_backing *);
	int (*clear)(void *, const struct mt_bo_backing *);
	void (*free)(void *, const struct mt_bo_backing *);
	int (*map)(void *, const struct mt_bo_backing *, void **);
	void (*unmap)(void *, const struct mt_bo_backing *);
};
struct mt_bo {
	struct mt_bo_backing backing;
	const struct mt_bo_ops *ops;
	void *store;
	u64 requested_bytes;
	u32 refs, cpu_users, gpu_users;
	/* Optional container destructor, installed by the creator before sharing.
	 * Called after backing release and clearing, with the store lock held.
	 * May free bo itself; no caller may dereference it after a final put. */
	void (*release)(struct mt_bo *bo);
	/* Optional immutable 4 KiB GPU PA vector, owned by the backing.
	 * backing.gpu_pa is its first page, never a contiguous-range promise. */
	const u64 *page_pa;
};

static inline int mt_bo_create(struct mt_bo *bo, const struct mt_bo_ops *ops,
		void *store, u64 bytes, u32 alignment)
{
	struct mt_bo_backing backing = {0};
	u64 rounded;
	int ret;
	if (!bo || bo->refs || bo->backing.handle || !ops || !ops->alloc ||
	    !ops->clear || !ops->free || !ops->map || !ops->unmap || !bytes ||
	    bytes > 0xfffff000ULL || alignment < 4096 || alignment > 0x200000 ||
	    (alignment & (alignment - 1)))
		return -EINVAL;
	rounded = (bytes + 4095) & ~4095ULL;
	ret = ops->alloc(store, rounded, alignment, &backing);
	if (ret)
		return ret;
	/* Treat a malformed backend success as failure, without exposing it. */
	if (!backing.handle || backing.bytes != rounded ||
	    (backing.gpu_pa & (alignment - 1)) ||
	    (backing.bar_offset & 4095) || backing.gpu_pa >= (1ULL << MT_GPU_VA_BITS) ||
	    rounded > (1ULL << MT_GPU_VA_BITS) - backing.gpu_pa) {
		if (backing.handle)
			ops->free(store, &backing);
		return -ERANGE;
	}
	ret = ops->clear(store, &backing);
	if (ret) {
		ops->free(store, &backing);
		return ret;
	}
	*bo = (struct mt_bo){.backing = backing, .ops = ops, .store = store,
		.requested_bytes = bytes, .refs = 1};
	return 0;
}

static inline int mt_bo_get(struct mt_bo *bo)
{
	if (!bo || !bo->refs)
		return -ENOENT;
	if (bo->refs == ~(u32)0)
		return -EOVERFLOW;
	bo->refs++;
	return 0;
}

static inline int mt_bo_put(struct mt_bo *bo)
{
	void (*release)(struct mt_bo *);
	if (!bo || !bo->refs)
		return -ENOENT;
	/* Owner references cannot consume the references owned by active uses. */
	if (bo->refs <= bo->cpu_users + (u64)bo->gpu_users)
		return -EBUSY;
	if (--bo->refs)
		return 0;
	release = bo->release;
	bo->ops->free(bo->store, &bo->backing);
	memset(bo, 0, sizeof(*bo));
	if (release)
		release(bo);
	return 0;
}

static inline int mt_bo_cpu_begin(struct mt_bo *bo, void **mapping)
{
	void *address = NULL;
	int ret;
	if (!bo || !bo->refs || !mapping)
		return -EINVAL;
	if (bo->gpu_users)
		return -EBUSY;
	ret = mt_bo_get(bo);
	if (ret)
		return ret;
	ret = bo->ops->map(bo->store, &bo->backing, &address);
	if (ret || !address) {
		if (!ret)
			bo->ops->unmap(bo->store, &bo->backing);
		mt_bo_put(bo);
		return ret ? ret : -EFAULT;
	}
	bo->cpu_users++;
	*mapping = address;
	return 0;
}

static inline int mt_bo_cpu_end(struct mt_bo *bo)
{
	if (!bo || !bo->cpu_users)
		return -EINVAL;
	bo->ops->unmap(bo->store, &bo->backing);
	bo->cpu_users--;
	return mt_bo_put(bo);
}

static inline int mt_bo_gpu_begin(struct mt_bo *bo)
{
	int ret;
	if (!bo || !bo->refs)
		return -ENOENT;
	/* Exclusive until the scheduler provides read/write fence ordering. */
	if (bo->cpu_users || bo->gpu_users)
		return -EBUSY;
	ret = mt_bo_get(bo);
	if (!ret)
		bo->gpu_users++;
	return ret;
}

static inline int mt_bo_gpu_end(struct mt_bo *bo)
{
	if (!bo || !bo->gpu_users)
		return -EINVAL;
	bo->gpu_users--;
	return mt_bo_put(bo);
}

/* Mmap/copy callers must limit access to the allocated, page-rounded object;
 * every byte including trailing padding was cleared before create succeeds. */
static inline int mt_bo_check_range(const struct mt_bo *bo, u64 offset, u64 bytes)
{
	if (!bo || !bo->refs)
		return -ENOENT;
	return offset > bo->backing.bytes || bytes > bo->backing.bytes - offset ?
		-ERANGE : 0;
}
#endif
