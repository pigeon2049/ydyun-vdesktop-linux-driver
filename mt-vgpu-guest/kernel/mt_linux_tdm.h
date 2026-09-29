/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_LINUX_TDM_H
#define MT_GUEST_LINUX_TDM_H
#include "mt_tqx_dma.h"

#define MT_LINUX_TDM_BYTES 0x11b8U
#define MT_LINUX_TDM_REG_OFFSET 0x1190U

/* Offline migration boundary for the pinned QY1 Linux DDK2 single TDM kick.
 * No ioctl accepts this packet. The caller must already have a validated fresh
 * TQX record and ownership of every referenced BO. Only the traced zeroed
 * header/CSW/kick subset is supported: no jobs, fences, instrumentation or
 * nonzero context-save contents are silently discarded.
 *
 * Linux has a 0x58 header, CSW before regions, and a 0xe0 kick; Windows vGPU
 * has a 0x48 header and a 0xc0 kick. Their 40-byte QY1 register tails match.
 * Rebuild the envelope and per-core length array, never memcpy the Linux DMA
 * allocation into a Guest queue. native_size_array_va is the separately
 * validated Linux CCB length-array address, not the translated array address.
 */
static inline int mt_linux_tdm_normalize(void *out, u32 capacity,
		const struct mt_device_profile *profile, const void *packet, u32 bytes,
		u64 native_dma_va, u64 native_size_array_va,
		const struct mt_tqx_upload_result *source, const struct mt_tqx_dma_input *input)
{
	const u8 *p = packet;
	u8 header[0x60] = {0}, regs[40] = {0};
	u32 i;
	if (!out || capacity < sizeof(struct mt_tqx_dma_image) || !p || !source || !input)
		return -EINVAL;
	if (bytes != MT_LINUX_TDM_BYTES || (native_dma_va & 7) ||
	    native_dma_va >= (1ULL << 40) - MT_LINUX_TDM_BYTES ||
	    (native_size_array_va & 127) || native_size_array_va >= (1ULL << 40))
		return -EINVAL;
	mt_fw_put64(header, 0x10, native_dma_va + 0x58);
	mt_fw_put32(header, 0x1c, 0x67);
	mt_fw_put32(header, 0x28, 0x1078);
	mt_fw_put32(header, 0x2c, 1);
	mt_fw_put32(header, 0x58, 2);
	if (memcmp(p, header, sizeof(header)))
		return -EOPNOTSUPP;
	/* Original RegionDesc size 0x100 excludes the 40-byte register suffix.
	 * The kick's own size 0x108 includes that suffix. Both are checked. */
	for (i = sizeof(header); i < MT_LINUX_TDM_REG_OFFSET; i++) {
		u8 expected = 0;
		switch (i) {
		case 0x1060: expected = 0x20; break;
		case 0x1061: expected = 0x10; break;
		case 0x1078: expected = 3; break;
		case 0x1080: expected = 0x90; break;
		case 0x1081: expected = 0x10; break;
		case 0x1089: expected = 1; break;
		case 0x10a4: expected = 0x20; break;
		case 0x10a8: expected = 1; break;
		case 0x10b8: expected = 8; break;
		case 0x10b9: expected = 1; break;
		}
		if (p[i] != expected)
			return -EOPNOTSUPP;
	}
	memcpy(regs, source->record + 0x18, 8);
	mt_fw_put64(regs, 8, native_size_array_va);
	mt_fw_put64(regs, 16, input->state_va);
	memcpy(regs + 24, source->record + 0x30, 16);
	if (memcmp(p + MT_LINUX_TDM_REG_OFFSET, regs, sizeof(regs)))
		return -EINVAL;
	/* This also validates record fields, addresses and core count before
	 * touching out, so malformed input never leaves a partial descriptor. */
	return mt_tqx_dma_encode(out, capacity, profile, source, input);
}
#endif
