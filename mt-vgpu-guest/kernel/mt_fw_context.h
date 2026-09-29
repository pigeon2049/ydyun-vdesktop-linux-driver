/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_FW_CONTEXT_H
#define MT_GUEST_FW_CONTEXT_H

#include "mt_fw_layout.h"

#define MT_FW_CONTEXT_BYTES 0x50U

/* CPU-side descriptor of 14001ecdc -> 1400231ec, for the current Guest
 * three-level MMU path. This descriptor is published only AFTER connection.
 * root_pa is GPU physical; *_gpa are Guest CPU physical, not GPU PA/IOVA.
 * The two referenced CPU buffers and descriptor must remain Host-accessible
 * throughout their published lifetime. This builder does not publish them.
 * mt_runtime_context.h pairs this with the initialized platform windows;
 * the main module owns persistent backing and guards publication lifetime.
 */
struct mt_fw_context_addresses {
	u64 root_pa, firmware_gpa, firmware_va;
	u64 info_gpa, aperture_gpa;
	u32 device_config;
};

static inline int mt_fw_context_build(void *out, u32 bytes,
		const struct mt_fw_context_addresses *a)
{
	if (!out || bytes < MT_FW_CONTEXT_BYTES || !a ||
	    !a->root_pa || (a->root_pa & 0xfff) || (a->root_pa >> 40) ||
	    !a->firmware_gpa || (a->firmware_gpa & 0xfff) ||
	    a->firmware_gpa > ~(u64)0 - MT_FW_MAP_SIZE ||
	    (a->firmware_va & 0xfff) ||
	    a->firmware_va > (1ULL << MT_GPU_VA_BITS) - MT_FW_MAP_SIZE ||
	    !a->info_gpa || (a->info_gpa & 7) ||
	    !a->aperture_gpa || (a->aperture_gpa & 7))
		return -EINVAL;
	memset(out, 0, MT_FW_CONTEXT_BYTES);
	mt_fw_put64(out, 0, a->root_pa);
	mt_fw_put64(out, 0x10, a->firmware_gpa + MT_FW_STATE_BYTES);
	mt_fw_put64(out, 0x18, a->firmware_va);
	mt_fw_put64(out, 0x28, a->firmware_va + MT_FW_STATE_BYTES);
	mt_fw_put32(out, 0x30, a->device_config);
	mt_fw_put64(out, 0x38, a->info_gpa);
	mt_fw_put64(out, 0x40, a->aperture_gpa);
	return 0;
}

/* The reference's cached-descriptor path refreshes only these two GPAs.
 * Do not zero Host-written/reserved fields when publishing an existing one.
 */
static inline int mt_fw_context_refresh(void *out, u32 bytes, u64 info_gpa, u64 aperture_gpa)
{
	if (!out || bytes < MT_FW_CONTEXT_BYTES || !info_gpa || (info_gpa & 7) ||
	    !aperture_gpa || (aperture_gpa & 7))
		return -EINVAL;
	mt_fw_put64(out, 0x38, info_gpa);
	mt_fw_put64(out, 0x40, aperture_gpa);
	return 0;
}
#endif
