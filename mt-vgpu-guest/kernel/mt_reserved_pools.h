/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_RESERVED_POOLS_H
#define MT_GUEST_RESERVED_POOLS_H
#include "mt_vram.h"
#include "mt_guest_heaps.h"
#include "mt_device_profile.h"
#include "mt_pool_slice.h"

/* Owner lives outside the ABI-stable mt_guest. Borrowed BOs must be gone
 * before fini, under the same session lock as all view/cache operations. */
struct mt_reserved_pools {
	struct mt_vram_block blocks[MT_GUEST_POOL_COUNT];
	struct mt_pool_state slices[MT_GUEST_POOL_COUNT];
	bool prepared;
};

static inline void mt_reserved_pools_fini(struct mt_vram *vram, struct mt_reserved_pools *pools)
{
	u32 i;
	for (i = 0; i < MT_GUEST_POOL_COUNT; i++)
		if (pools->blocks[i].size)
			mt_vram_free(vram, &pools->blocks[i]);
	memset(pools, 0, sizeof(*pools));
}

/* Reserve only: no clears, uploads, page-table publication or suballocation. */
static inline int mt_reserved_pools_prepare(struct mt_vram *vram,
		struct mt_reserved_pools *pools, const struct mt_device_profile *profile)
{
	struct mt_guest_pool_spec specs[MT_GUEST_POOL_COUNT];
	u32 i;
	int ret;
	if (!vram || !pools || !vram->region_owned || pools->prepared)
		return -EINVAL;
	for (i = 0; i < MT_GUEST_POOL_COUNT; i++)
		if (pools->blocks[i].size)
			return -EINVAL;
	if (!profile || profile->family != 2 || profile->transfer_version != 1)
		return -EOPNOTSUPP;
	mt_guest_plan_pools(specs);
	for (i = 0; i < MT_GUEST_POOL_COUNT; i++) {
		ret = mt_vram_alloc(vram, MT_POOL_NORMAL, specs[i].bytes, PAGE_SIZE, &pools->blocks[i]);
		if (ret) {
			mt_reserved_pools_fini(vram, pools);
			return ret;
		}
	}
	pools->prepared = true;
	return 0;
}
#endif
