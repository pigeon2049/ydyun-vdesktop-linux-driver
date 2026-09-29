/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_FW_LAYOUT_H
#define MT_GUEST_FW_LAYOUT_H

#include "mt_mmu.h"

/* Guest branch of mtkm64.sys 14001728c. This is the CPU-side layout
 * descriptor, NOT the firmware state structure or an uploadable image.
 * Device allocations, state fields and resource addresses are separate.
 */
#define MT_FW_LAYOUT_BYTES 0x1610U
#define MT_FW_STATE_BYTES 0x6ddd0U
#define MT_FW_QUEUE_BYTES 0x11520U

static inline void mt_fw_put32(void *out, u32 offset, u32 value)
{
	memcpy((u8 *)out + offset, &value, sizeof(value));
}

static inline void mt_fw_put64(void *out, u32 offset, u64 value)
{
	memcpy((u8 *)out + offset, &value, sizeof(value));
}

static inline int mt_fw_build_guest_layout(void *out, u64 firmware_va)
{
	if (!out || (firmware_va & 0xfff) ||
	    firmware_va > (1ULL << MT_GPU_VA_BITS) - MT_FW_MAP_SIZE)
		return -EINVAL;
	memset(out, 0, MT_FW_LAYOUT_BYTES);
	mt_fw_put64(out, 0x08, firmware_va);
	mt_fw_put32(out, 0x10, 0x90000000U);
	mt_fw_put32(out, 0x24, MT_FW_STATE_BYTES);
	mt_fw_put32(out, 0x28, 0x90000000U | MT_FW_STATE_BYTES);
	mt_fw_put64(out, 0x30, firmware_va + MT_FW_STATE_BYTES);
	mt_fw_put32(out, 0x3c, MT_FW_STATE_BYTES);
	mt_fw_put32(out, 0x40, 0x90000000U | MT_FW_STATE_BYTES);
	mt_fw_put32(out, 0x48, MT_FW_STATE_BYTES);
	mt_fw_put32(out, 0x4c, 0x90000000U | MT_FW_STATE_BYTES);
	mt_fw_put32(out, 0x50, MT_FW_MAP_SIZE);
	mt_fw_put64(out, 0x58, firmware_va);
	mt_fw_put32(out, 0x8c, MT_FW_STATE_BYTES);
	mt_fw_put32(out, 0x90, 0x90000000U | MT_FW_STATE_BYTES);
	mt_fw_put32(out, 0xb0, 0x1000);
	mt_fw_put32(out, 0xb4, MT_FW_MAP_SIZE);
	mt_fw_put64(out, 0xb8, firmware_va);
	return 0;
}

#endif
