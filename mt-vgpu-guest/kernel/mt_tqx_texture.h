/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_TQX_TEXTURE_H
#define MT_GUEST_TQX_TEXTURE_H
#include "mt_mmu.h"
#include "mt_fw_layout.h"

#define MT_TQX_TEXTURE_BYTES 64U
struct mt_tqx_texture_input { u64 source_va; u32 element_bytes, width, height; };

/* Family-2 linear byte-copy view only: 14011bbe4 -> 14008deb8 ->
 * 1400ae5f8/1400acd90, then default sampler 1400949fc/1400aec68.
 * 32 bytes of texture state + 16 of sampler + 16 zero allocation padding.
 * No descriptor heap address or descriptor index is encoded in this block. */
static inline int mt_tqx_texture_build(void *out, u32 capacity,
		const struct mt_tqx_texture_input *in)
{
	static const u64 format0[5] = {0xb64,0x48b64,0xc0b64,0x118364,0x18029c};
	static const u64 format2[5] = {0x8100,0x8100,0x8100,0x8200,0};
	static const u64 format3[5] = {0x18000200000058ULL,0x18000200000057ULL,
		0x18000232100000ULL,0x18000232100000ULL,0x18000232100000ULL};
	u8 block[MT_TQX_TEXTURE_BYTES] = {0};
	u64 bytes, limit = 1ULL << MT_GPU_VA_BITS;
	u32 b, index = 0;
	if (!out || !in || capacity < sizeof(block) || !in->width || !in->height ||
	    in->width > 32768 || in->height > 32768 || !in->element_bytes ||
	    in->element_bytes > 16 || (in->element_bytes & (in->element_bytes - 1)))
		return -EINVAL;
	bytes = (u64)in->width * in->height * in->element_bytes;
	if (in->source_va >= limit || bytes > limit - in->source_va)
		return -ERANGE;
	b = in->element_bytes;
	while (b > 1) { b >>= 1; index++; }
	mt_fw_put64(block, 0, format0[index] | ((u64)(in->width - 1) << 27) |
			((u64)(in->height - 1) << 42));
	mt_fw_put64(block, 8, in->source_va | 0x4000000000000000ULL |
			((u64)(in->width - 1) << 46));
	mt_fw_put64(block, 16, format2[index]);
	mt_fw_put64(block, 24, format3[index]);
	mt_fw_put64(block, 32, 0x8012400032000fffULL);
	memcpy(out, block, sizeof(block));
	return 0;
}
#endif
