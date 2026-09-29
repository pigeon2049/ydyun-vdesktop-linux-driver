/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_FW_STATE_H
#define MT_GUEST_FW_STATE_H

#include "mt_fw_layout.h"

/* Initial Guest state for the S3000 host-PB path (device-info flag 0x10).
 * 140015f78 -> 1400227e4 -> 14002db90. These are GPU addresses, never CPU
 * pointers. The caller must reserve and map every referenced resource before
 * publishing the image. Constructing these bytes alone does not do that.
 */
struct mt_fw_guest_resources {
	u64 pb_va[2];
	u64 fence_gpu_pa;
	u64 yuv_va;
	u64 heap1_va, heap2_va, heap7_va, heap8_va, heap10_va, heap9_va;
	u32 dm_kill_offset;
};

static inline int mt_fw_apply_guest_state(void *image, u32 size,
					const struct mt_fw_guest_resources *r)
{
	if (!image || !r || size != MT_FW_MAP_SIZE ||
	    !r->pb_va[0] || !r->pb_va[1] || (r->pb_va[0] & 255) || (r->pb_va[1] & 255) ||
	    (r->pb_va[0] >> 40) || (r->pb_va[1] >> 40) ||
	    (r->fence_gpu_pa >> 40) || (r->yuv_va >> 40) ||
	    (r->heap1_va >> 40) || (r->heap2_va >> 40) ||
	    (r->heap7_va >> 40) || (r->heap8_va >> 40) ||
	    (r->heap10_va >> 40) || (r->heap9_va >> 40))
		return -EINVAL;
	mt_fw_put32(image, 4, 0);
	mt_fw_put64(image, 0x30, 0x90000000U | MT_FW_STATE_BYTES);
	mt_fw_put32(image, 0x40, 6);
	mt_fw_put64(image, 0x25660, 0x90000000U | MT_FW_STATE_BYTES);
	mt_fw_put64(image, 0x25670, 0);
	mt_fw_put32(image, 0x25680, 1);
	mt_fw_put64(image, 0x25690, 0x90000000U | MT_FW_STATE_BYTES);
	mt_fw_put64(image, 0x25698, 0x90000000U | MT_FW_STATE_BYTES);
	memcpy((u8 *)image + 0x256a0, r->pb_va, 16);
	mt_fw_put64(image, 0x270f0, r->fence_gpu_pa);
	mt_fw_put64(image, 0x270f8, r->yuv_va);
	mt_fw_put64(image, 0x27100, r->heap1_va);
	mt_fw_put64(image, 0x27108, r->heap2_va);
	mt_fw_put64(image, 0x27110, r->heap7_va);
	mt_fw_put64(image, 0x27118, r->heap8_va);
	mt_fw_put64(image, 0x27120, r->heap10_va);
	mt_fw_put64(image, 0x27128, r->heap9_va);
	mt_fw_put32(image, 0x27130, r->dm_kill_offset);
	/* The platform scratch buffer is zero on initial construction. The Guest
	 * PB callback copies just the 16-byte pair at scratch+8 (no PMVA array).
	 */
	memset((u8 *)image + 0x27148, 0, 0x2000);
	memcpy((u8 *)image + 0x27150, r->pb_va, 16);
	return 0;
}

#endif
