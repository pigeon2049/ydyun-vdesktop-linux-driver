/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_VRAM_H
#define MT_VRAM_H

#include <linux/genalloc.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/pci.h>
#include "mt_memory_layout.h"

/* Serialized by probe/remove for now. Runtime allocation callers will need
 * an outer lock for allocation-list/mapping lifetime, beyond gen_pool's lock.
 * Tokens are allocator indices, not CPU VAs, GPAs, GPU PAs or DMA IOVAs.
 */
#define MT_VRAM_TOKEN_BASE SZ_1G
struct mt_vram_block {
	struct list_head link;
	void __iomem *mapping;
	u64 bar_offset, gpu_pa;
	unsigned long token;
	u32 size, pool;
};

struct mt_vram {
	struct pci_dev *pdev;
	struct mt_memory_layout layout;
	struct gen_pool *pool[MT_POOL_COUNT];
	struct list_head blocks;
	bool region_owned;
};

static inline void mt_vram_free(struct mt_vram *vram, struct mt_vram_block *block)
{
	if (!block->size)
		return;
	pci_iounmap(vram->pdev, block->mapping);
	gen_pool_free(vram->pool[block->pool], block->token, block->size);
	list_del(&block->link);
	memset(block, 0, sizeof(*block));
}

static inline void mt_vram_fini(struct mt_vram *vram)
{
	struct mt_vram_block *block, *next;
	u32 i;
	if (!vram->pdev)
		return;
	list_for_each_entry_safe(block, next, &vram->blocks, link)
		mt_vram_free(vram, block);
	for (i = 0; i < MT_POOL_COUNT; i++) {
		if (vram->pool[i])
			gen_pool_destroy(vram->pool[i]);
		vram->pool[i] = NULL;
	}
	if (vram->region_owned)
		pci_release_region(vram->pdev, 2);
	vram->region_owned = false;
	vram->pdev = NULL;
}

static inline int mt_vram_init(struct mt_vram *vram, struct pci_dev *pdev,
			       const void *info, u32 length)
{
	u32 i;
	int ret;
	if (vram->pdev)
		return -EBUSY;
	if (!(pci_resource_flags(pdev, 2) & IORESOURCE_MEM))
		return -ENODEV;
	ret = mt_memory_parse(info, length, pci_resource_len(pdev, 2), &vram->layout);
	if (ret)
		return ret;
	vram->pdev = pdev;
	INIT_LIST_HEAD(&vram->blocks);
	ret = pci_request_region(pdev, 2, "mt_guest_vram");
	if (ret)
		goto fail;
	vram->region_owned = true;
	for (i = 0; i < MT_POOL_COUNT; i++) {
		vram->pool[i] = gen_pool_create(PAGE_SHIFT, -1);
		if (!vram->pool[i]) {
			ret = -ENOMEM;
			goto fail;
		}
		ret = gen_pool_add(vram->pool[i], MT_VRAM_TOKEN_BASE,
				   vram->layout.pool[i].size, -1);
		if (ret)
			goto fail;
	}
	return 0;
fail:
	mt_vram_fini(vram);
	return ret;
}

static inline int mt_vram_alloc(struct mt_vram *vram, u32 pool, u32 size,
				u32 alignment, struct mt_vram_block *out)
{
	const struct mt_memory_range *range;
	struct genpool_data_align data = { .align = alignment };
	unsigned long token;
	u64 offset;
	void __iomem *mapping;
	if (!vram->region_owned || pool >= MT_POOL_COUNT || !out || out->size ||
	    !size || (size & (PAGE_SIZE - 1)) || alignment < PAGE_SIZE ||
	    alignment > SZ_2M || !is_power_of_2(alignment))
		return -EINVAL;
	range = &vram->layout.pool[pool];
	if ((range->bar_offset & (alignment - 1)) || (range->gpu_pa & (alignment - 1)))
		return -EINVAL;
	token = gen_pool_alloc_algo(vram->pool[pool], size, gen_pool_first_fit_align, &data);
	if (!token)
		return -ENOSPC;
	offset = token - MT_VRAM_TOKEN_BASE;
	if (offset > range->size || size > range->size - offset) {
		gen_pool_free(vram->pool[pool], token, size);
		return -ERANGE;
	}
	mapping = pci_iomap_range(vram->pdev, 2, range->bar_offset + offset, size);
	if (!mapping) {
		gen_pool_free(vram->pool[pool], token, size);
		return -ENOMEM;
	}
	*out = (struct mt_vram_block){ .mapping = mapping, .token = token,
		.size = size, .pool = pool, .bar_offset = range->bar_offset + offset,
		.gpu_pa = range->gpu_pa + offset };
	list_add_tail(&out->link, &vram->blocks);
	return 0;
}

#endif
