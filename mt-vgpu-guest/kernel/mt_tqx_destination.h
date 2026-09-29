/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_TQX_DESTINATION_H
#define MT_GUEST_TQX_DESTINATION_H
#include "mt_tqx_texture.h"

#define MT_TQX_DESTINATION_BYTES 80U
struct mt_tqx_destination_input {
	u64 destination_va;
	u32 element_bytes, width, height;
	u32 pds_code, pds_execution_state, pds_constant_state;
};

/* Family-2 single-layer copy: 1400dc638 -> 1400d9eec/14011eb2c,
 * then 1400da4a8 and 1400d98c8. Three PDS values are heap-relative
 * addresses, NOT GPU VAs. Initial command state/finalization are separate. */
static inline int mt_tqx_destination_build(void *out, u32 capacity,
		const struct mt_tqx_destination_input *in)
{
	static const u32 swizzle[5] = {0x2da100,0x2da100,0x2da100,0x2d2100,0x1a2100};
	static const u32 format[5] = {0x140000,0x150000,0x168000,0x118000,0x48000};
	u8 block[MT_TQX_DESTINATION_BYTES] = {0};
	u64 bytes, limit = 1ULL << MT_GPU_VA_BITS;
	u32 index = 0, b;
	if (!out || !in || capacity < sizeof(block) || !in->width || !in->height ||
	    in->width > 32768 || in->height > 32768 || !in->element_bytes ||
	    in->element_bytes > 16 || (in->element_bytes & (in->element_bytes - 1)) ||
	    ((in->pds_code | in->pds_execution_state | in->pds_constant_state) & 15))
		return -EINVAL;
	bytes = (u64)in->width * in->height * in->element_bytes;
	if (in->destination_va >= limit || bytes > limit - in->destination_va ||
	    (u64)in->pds_execution_state + 36 > (1ULL << 32) ||
	    (u64)in->pds_constant_state + 16 > (1ULL << 32) ||
	    (u64)in->pds_code + 4 > (1ULL << 32))
		return -ERANGE;
	b = in->element_bytes;
	while (b > 1) { b >>= 1; index++; }
	mt_fw_put32(block, 0, in->element_bytes == 16 ? 0x42000005 : 0x40000005);
	mt_fw_put32(block, 4, swizzle[index]);
	mt_fw_put32(block, 12, in->height - 1);
	mt_fw_put32(block, 16, in->width - 1);
	mt_fw_put32(block, 24, in->width - 1);
	mt_fw_put32(block, 28, in->destination_va);
	mt_fw_put32(block, 32, format[index] | (in->destination_va >> 32));
	mt_fw_put32(block, 36, 2);
	mt_fw_put32(block, 40, 0xa8000000U);
	mt_fw_put32(block, 44, in->width - 1);
	mt_fw_put32(block, 48, in->height - 1);
	mt_fw_put32(block, 52, in->pds_code);
	mt_fw_put32(block, 56, in->pds_execution_state);
	mt_fw_put32(block, 64, in->pds_constant_state);
	mt_fw_put32(block, 68, 0x08000001); /* 5 constants, 0 temps, 4 PDS words. */
	mt_fw_put32(block, 72, 0x25);
	memcpy(out, block, sizeof(block));
	return 0;
}
#endif
