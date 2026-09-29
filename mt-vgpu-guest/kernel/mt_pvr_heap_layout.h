/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_PVR_HEAP_LAYOUT_H
#define MT_PVR_HEAP_LAYOUT_H
#include "mt_memory_layout.h"

enum mt_pvr_range_id { MT_PVR_FW, MT_PVR_MMU, MT_PVR_VIDEO, MT_PVR_GPU, MT_PVR_COUNT };
struct mt_pvr_range { u64 cpu, device, size; };
struct mt_pvr_heap_layout {
	struct mt_pvr_range range[MT_PVR_COUNT];
	u64 mmu_host_base;
};

/* Separate allocators must never advertise overlapping backing. Page tables
 * use the Windows normal private pool; GPU allocations use its general pool.
 * The firmware pool is split at the fixed 8 MiB Linux FW_MAIN boundary.
 * Video use remains unvalidated; the context audit stops before VPU binding. */
static inline int mt_pvr_heap_parse(const void *raw, u32 length, u64 cpu_base,
				    u64 bar_size, struct mt_pvr_heap_layout *out)
{
	struct mt_memory_layout memory;
	struct mt_pvr_heap_layout plan = {0};
	u64 general_start, fw_start, fw_size;
	u32 i, j;
	int err;
	if (!out || !cpu_base || (cpu_base & 4095) || cpu_base > ~(u64)0 - bar_size)
		return -EINVAL;
	err = mt_memory_parse(raw, length, bar_size, &memory);
	if (err)
		return err;
	general_start = memory.pool[MT_POOL_NORMAL].bar_offset + memory.pool[MT_POOL_NORMAL].size;
	fw_start = memory.pool[MT_POOL_FIRMWARE].bar_offset;
	fw_size = memory.pool[MT_POOL_FIRMWARE].size;
	if (general_start >= fw_start || fw_size <= MT_FW_MAP_SIZE)
		return -ERANGE;
	plan.range[MT_PVR_FW] = (struct mt_pvr_range){0, fw_start, MT_FW_MAP_SIZE};
	plan.range[MT_PVR_MMU] = (struct mt_pvr_range){0,
		memory.pool[MT_POOL_NORMAL].bar_offset, memory.pool[MT_POOL_NORMAL].size};
	plan.range[MT_PVR_VIDEO] = (struct mt_pvr_range){0,
		fw_start + MT_FW_MAP_SIZE, fw_size - MT_FW_MAP_SIZE};
	plan.range[MT_PVR_GPU] = (struct mt_pvr_range){0, general_start, fw_start - general_start};
	plan.mmu_host_base = memory.pool[MT_POOL_NORMAL].gpu_pa;
	for (i = 0; i < MT_PVR_COUNT; i++) {
		struct mt_pvr_range *r = &plan.range[i];
		if (!r->size || ((r->device | r->size) & 4095) ||
		    r->device >= memory.actual_size || r->size > memory.actual_size - r->device)
			return -ERANGE;
		r->cpu = cpu_base + r->device;
		for (j = 0; j < i; j++)
			if (r->device < plan.range[j].device + plan.range[j].size &&
			    plan.range[j].device < r->device + r->size)
				return -EINVAL;
	}
	*out = plan;
	return 0;
}
#endif
