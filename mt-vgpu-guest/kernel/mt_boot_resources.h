/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_BOOT_RESOURCES_H
#define MT_GUEST_BOOT_RESOURCES_H

#include <linux/slab.h>
#include "mt_vram.h"
#include "mt_guest_heaps.h"
#include "mt_mmu_bootstrap.h"
#include "mt_static_resources.h"
#include "mt_static_programs.h"

enum mt_boot_allocation {
	MT_BOOT_DUMMY, MT_BOOT_PDS, MT_BOOT_USC, MT_BOOT_YUV, MT_BOOT_KILL,
	MT_BOOT_FENCE, MT_BOOT_PAGING_CONTEXT, MT_BOOT_PB, MT_BOOT_ALLOCATION_COUNT
};

#define MT_BOOT_DUMMY_OFFSET MT_FW_TABLE_BYTES
#define MT_BOOT_YUV_OFFSET (MT_BOOT_DUMMY_OFFSET + MT_MMU_DUMMY_BYTES)
#define MT_BOOT_KILL_OFFSET (MT_BOOT_YUV_OFFSET + MT_STATIC_RESOURCE_BYTES)
#define MT_BOOT_PDS_OFFSET (MT_BOOT_KILL_OFFSET + MT_STATIC_RESOURCE_BYTES)
#define MT_BOOT_USC_OFFSET (MT_BOOT_PDS_OFFSET + MT_STATIC_PDS_BYTES)
#define MT_BOOT_STAGE_BYTES (MT_BOOT_USC_OFFSET + MT_STATIC_USC_BYTES)

struct mt_boot_resources {
	struct mt_vram_block blocks[MT_BOOT_ALLOCATION_COUNT];
	u64 va[MT_BOOT_ALLOCATION_COUNT];
	void *stage;
	u32 table_pages;
	bool prepared;
};

static inline void mt_boot_resources_fini(struct mt_vram *vram, struct mt_boot_resources *boot)
{
	u32 i;
	for (i = 0; i < MT_BOOT_ALLOCATION_COUNT; i++)
		mt_vram_free(vram, &boot->blocks[i]);
	kvfree(boot->stage);
	memset(boot, 0, sizeof(*boot));
}

/* Reserve backing memory and build CPU staging data. No writes through any
 * I/O mapping and no Host/FW/root publication. Resource virtual addresses
 * come from the Guest plan; USC remains deferred. Only the proven firmware
 * range goes into its own tree. Per-context resource mappings are separate.
 */
static inline int mt_boot_resources_prepare(struct mt_vram *vram,
		struct mt_boot_resources *boot, const struct mt_vram_block *firmware,
		const struct mt_vram_block *tables, const struct mt_device_profile *profile)
{
	static const u32 sizes[MT_BOOT_ALLOCATION_COUNT] = {
		MT_MMU_DUMMY_BYTES, SZ_1M, SZ_1M, SZ_512K, SZ_512K,
		PAGE_SIZE, 2 * PAGE_SIZE, SZ_2M
	};
	static const u32 resource_indices[MT_BOOT_ALLOCATION_COUNT] = {0, 4, 9, 10, 11, 6, 7, 0};
	struct mt_guest_heap_plan *plan;
	struct mt_mmu_range range;
	u32 i, pool;
	int ret;
	if (boot->prepared || boot->stage || firmware->size != MT_FW_MAP_SIZE ||
	    tables->size < MT_FW_TABLE_BYTES || !vram->region_owned)
		return -EINVAL;
	plan = kzalloc(sizeof(*plan), GFP_KERNEL);
	if (!plan)
		return -ENOMEM;
	mt_guest_plan_heaps(plan);
	for (i = 0; i < MT_BOOT_ALLOCATION_COUNT; i++) {
		pool = i == MT_BOOT_PB ? MT_POOL_PB : MT_POOL_NORMAL;
		ret = mt_vram_alloc(vram, pool, sizes[i], PAGE_SIZE, &boot->blocks[i]);
		if (ret)
			goto fail;
		if (i != MT_BOOT_DUMMY)
			boot->va[i] = plan->resources[resource_indices[i]].va;
	}
	boot->stage = kvzalloc(MT_BOOT_STAGE_BYTES, GFP_KERNEL);
	if (!boot->stage) {
		ret = -ENOMEM;
		goto fail;
	}
	range = (struct mt_mmu_range){ .va = plan->heaps[6].base, .pa = firmware->gpu_pa,
		.size = MT_FW_MAP_SIZE, .flags = 0x1c };
	ret = mt_mmu_build_bootstrap(boot->stage, MT_FW_TABLE_BYTES,
			tables->gpu_pa, &range, 1, &boot->table_pages);
	if (ret)
		goto fail;
	ret = mt_mmu_build_dummy((u8 *)boot->stage + MT_BOOT_DUMMY_OFFSET,
			MT_MMU_DUMMY_BYTES, boot->blocks[MT_BOOT_DUMMY].gpu_pa);
	if (ret)
		goto fail;
	ret = mt_init_static_resources((u8 *)boot->stage + MT_BOOT_YUV_OFFSET,
		MT_STATIC_RESOURCE_BYTES, (u8 *)boot->stage + MT_BOOT_KILL_OFFSET,
		MT_STATIC_RESOURCE_BYTES);
	if (ret)
		goto fail;
	ret = mt_static_programs_build((u8 *)boot->stage + MT_BOOT_PDS_OFFSET,
		MT_STATIC_PDS_BYTES, (u8 *)boot->stage + MT_BOOT_USC_OFFSET,
		MT_STATIC_USC_BYTES, profile);
	if (ret)
		goto fail;
	boot->prepared = true;
	kfree(plan);
	return 0;
fail:
	mt_boot_resources_fini(vram, boot);
	kfree(plan);
	return ret;
}

#endif
