/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_BO_VRAM_H
#define MT_GUEST_BO_VRAM_H
#include <linux/lockdep.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include "mt_bo.h"
#include "mt_vram.h"
#include "mt_system_memory.h"

struct mt_bo_store {
	struct mt_vram *vram;
	struct mutex *lock;
	const struct mt_bo_ops *ops;
	u64 allocated_bytes;
	u32 objects;
};

/* A borrowed block retains its original allocation/list node. BO teardown
 * releases only this handle; the boot owner frees the block after all users. */
struct mt_bo_vram_handle {
	struct mt_vram_block allocation;
	struct mt_vram_block *block;
	struct mt_system_memory *system;
	bool borrowed;
};

static int mt_bo_vram_alloc(void *opaque, u32 bytes, u32 alignment,
		struct mt_bo_backing *out)
{
	struct mt_bo_store *s = opaque;
	struct mt_bo_vram_handle *h;
	struct mt_vram_block *block;
	int ret;
	lockdep_assert_held(s->lock);
	if (!s->vram->region_owned)
		return -ENODEV;
	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return -ENOMEM;
	block = &h->allocation;
	h->block = block;
	ret = mt_vram_alloc(s->vram, MT_POOL_NORMAL, bytes, alignment, block);
	if (ret) {
		kfree(h);
		return ret;
	}
	/* Normal unload cannot tear down a store while objects still refer to it. */
	__module_get(THIS_MODULE);
	s->allocated_bytes += bytes;
	s->objects++;
	*out = (struct mt_bo_backing){.handle = h, .gpu_pa = block->gpu_pa,
		.bar_offset = block->bar_offset, .bytes = bytes};
	return 0;
}

static int mt_bo_vram_clear(void *opaque, const struct mt_bo_backing *backing)
{
	struct mt_bo_store *s = opaque;
	struct mt_bo_vram_handle *h = backing->handle;
	struct mt_vram_block *block = h->block;
	lockdep_assert_held(s->lock);
	if (h->borrowed)
		return -EPERM;
	memset_io(block->mapping, 0, block->size);
	mb();
	return 0;
}

static void mt_bo_vram_free(void *opaque, const struct mt_bo_backing *backing)
{
	struct mt_bo_store *s = opaque;
	struct mt_bo_vram_handle *h = backing->handle;
	struct mt_vram_block *block = h->block;
	lockdep_assert_held(s->lock);
	s->allocated_bytes -= backing->bytes;
	s->objects--;
	if (!h->borrowed)
		mt_vram_free(s->vram, block);
	kfree(h);
	module_put(THIS_MODULE);
}

static int mt_bo_vram_map(void *opaque, const struct mt_bo_backing *backing, void **address)
{
	struct mt_bo_store *s = opaque;
	struct mt_bo_vram_handle *h = backing->handle;
	struct mt_vram_block *block = h->block;
	lockdep_assert_held(s->lock);
	/* Kernel-internal cookie only. Users must cast back to __iomem and use
	 * memcpy_toio/fromio; this is neither ordinary RAM nor a user pointer. */
	*address = h->system ? h->system->cpu : (void __force *)block->mapping;
	return 0;
}

static void mt_bo_vram_unmap(void *opaque, const struct mt_bo_backing *backing)
{
	struct mt_bo_store *s = opaque;
	(void)backing;
	lockdep_assert_held(s->lock);
	mb();
	/* The BAR mapping lives until the last object reference, not each map. */
}

static const struct mt_bo_ops mt_bo_vram_ops = {
	.alloc = mt_bo_vram_alloc, .clear = mt_bo_vram_clear, .free = mt_bo_vram_free,
	.map = mt_bo_vram_map, .unmap = mt_bo_vram_unmap,
};

/* Internal borrowed view: no allocation, zeroing, transfer of block ownership
 * or I/O. The caller must prevent owner teardown until the final BO put. */
static inline int mt_bo_vram_borrow(struct mt_bo *bo, struct mt_bo_store *s,
		struct mt_vram_block *block)
{
	struct mt_bo_vram_handle *h;
	struct mt_vram_block *at;
	bool found = false;
	if (!bo || bo->refs || bo->backing.handle || !s || !s->vram ||
	    s->ops != &mt_bo_vram_ops || !block)
		return -EINVAL;
	lockdep_assert_held(s->lock);
	if (!s->vram->region_owned)
		return -ENODEV;
	list_for_each_entry(at, &s->vram->blocks, link)
		if (at == block) { found = true; break; }
	if (!found)
		return -EXDEV;
	if (!block->mapping || !block->size ||
	    ((block->gpu_pa | block->bar_offset | block->size) & 4095) ||
	    block->gpu_pa >= (1ULL << MT_GPU_VA_BITS) ||
	    block->size > (1ULL << MT_GPU_VA_BITS) - block->gpu_pa)
		return -ERANGE;
	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return -ENOMEM;
	h->block = block;
	h->borrowed = true;
	__module_get(THIS_MODULE);
	s->objects++;
	s->allocated_bytes += block->size;
	*bo = (struct mt_bo){.backing = {.handle = h, .gpu_pa = block->gpu_pa,
		.bar_offset = block->bar_offset, .bytes = block->size}, .ops = s->ops,
		.store = s, .requested_bytes = block->size, .refs = 1};
	return 0;
}

/* Same store and lifetime domain as BAR BOs, but ordinary system RAM. */
static inline int mt_bo_system_borrow(struct mt_bo *bo, struct mt_bo_store *s,
		struct mt_system_memory *m)
{
	struct mt_bo_vram_handle *h;
	u32 i;
	if (!bo || bo->refs || bo->backing.handle || !s || !s->lock ||
	    s->ops != &mt_bo_vram_ops || !m || !m->cpu || !m->page_pa ||
	    !m->bytes || (m->bytes & 4095))
		return -EINVAL;
	lockdep_assert_held(s->lock);
	for (i = 0; i < m->bytes / 4096; i++)
		if ((m->page_pa[i] & 4095) || m->page_pa[i] >= (1ULL << MT_GPU_VA_BITS))
			return -ERANGE;
	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return -ENOMEM;
	h->system = m;
	h->borrowed = true;
	__module_get(THIS_MODULE);
	s->objects++;
	s->allocated_bytes += m->bytes;
	*bo = (struct mt_bo){.backing = {.handle = h, .gpu_pa = m->page_pa[0],
		.bytes = m->bytes}, .ops = s->ops, .store = s,
		.requested_bytes = m->bytes, .refs = 1, .page_pa = m->page_pa};
	return 0;
}

static inline int mt_bo_vram_read(struct mt_bo *bo, u64 offset, void *dst, u64 bytes)
{
	struct mt_bo_store *s;
	void *mapping;
	int ret;
	if (!bo || bo->ops != &mt_bo_vram_ops || (!dst && bytes))
		return -EINVAL;
	s = bo->store;
	lockdep_assert_held(s->lock);
	ret = mt_bo_check_range(bo, offset, bytes);
	if (ret)
		return ret;
	ret = mt_bo_cpu_begin(bo, &mapping);
	if (ret)
		return ret;
	if (bytes) {
		struct mt_bo_vram_handle *h = bo->backing.handle;
		if (h->system)
			memcpy(dst, (u8 *)mapping + offset, bytes);
		else
			memcpy_fromio(dst, (void __iomem *)mapping + offset, bytes);
	}
	return mt_bo_cpu_end(bo);
}

static inline int mt_bo_vram_write(struct mt_bo *bo, u64 offset, const void *src, u64 bytes)
{
	struct mt_bo_store *s;
	void *mapping;
	int ret;
	if (!bo || bo->ops != &mt_bo_vram_ops || (!src && bytes))
		return -EINVAL;
	s = bo->store;
	lockdep_assert_held(s->lock);
	ret = mt_bo_check_range(bo, offset, bytes);
	if (ret)
		return ret;
	ret = mt_bo_cpu_begin(bo, &mapping);
	if (ret)
		return ret;
	if (bytes) {
		struct mt_bo_vram_handle *h = bo->backing.handle;
		if (h->system)
			memcpy((u8 *)mapping + offset, src, bytes);
		else
			memcpy_toio((void __iomem *)mapping + offset, src, bytes);
	}
	return mt_bo_cpu_end(bo);
}

static inline void mt_bo_store_init(struct mt_bo_store *s, struct mt_vram *vram,
		struct mutex *lock)
{
	*s = (struct mt_bo_store){.vram = vram, .lock = lock, .ops = &mt_bo_vram_ops};
}

static inline int mt_bo_store_fini(struct mt_bo_store *s)
{
	if (s->objects)
		return -EBUSY;
	memset(s, 0, sizeof(*s));
	return 0;
}
#endif
