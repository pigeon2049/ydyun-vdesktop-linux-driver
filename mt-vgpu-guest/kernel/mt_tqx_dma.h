/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_TQX_DMA_H
#define MT_GUEST_TQX_DMA_H
#include "mt_tqx_upload.h"
#include "mt_tqx_engine_state.h"

#define MT_TQX_DMA_ALLOCATION 8192U
struct mt_tqx_dma_input { u64 dma_va, state_va; u32 cores; };
struct mt_tqx_dma_image {
	u32 bytes, reserved;
	u8 descriptor[MT_TQX_DMA_ALLOCATION];
	u8 submission_view[24];
};

/* 04cc80 -> 09c3e8 -> 04e924 -> 11d7b8, family2, one freshly
 * finalized record/page, header offset 0, no optional instrumentation.
 * cores is a bounded layout parameter (1..8), NOT inferred from PCI revision.
 * The output needs its own ordinary-heap BO. state_va names separate engine
 * state, not PDS state. No mapping/ownership, state initialization, DMA upload
 * or firmware publication is performed by this CPU encoder.
 * Padding and all unwritten bytes are zero by Linux allocation policy.
 */
static inline int mt_tqx_dma_encode(void *out, u32 capacity,
		const struct mt_device_profile *profile, const struct mt_tqx_upload_result *source,
		const struct mt_tqx_dma_input *input)
{
	struct mt_tqx_dma_image *image = out;
	struct mt_tqx_dma_input in;
	struct mt_tqx_engine_state_plan state;
	u8 record[MT_TQX_RECORD_BYTES];
	u64 command, page_command, page_bytes, roots, root_command, sequence, sync_offset;
	u32 exact, padded, flags, initial, code, total, i;
	int ret;
	if (!out || capacity < sizeof(*image) || !source || !input)
		return -EINVAL;
	if (!profile || profile->family != 2 || profile->transfer_version != 1)
		return -EOPNOTSUPP;
	in = *input;
	ret = mt_tqx_engine_state_plan(&state, profile, in.cores, in.state_va);
	if (ret)
		return ret;
	if (in.dma_va & 4095)
		return -EINVAL;
	if (in.dma_va < 0x40000000ULL || in.dma_va > 0x8040000000ULL - MT_TQX_DMA_ALLOCATION)
		return -ERANGE;
	memcpy(record, source->record, sizeof(record));
	memcpy(&command, record + 0x18, 8);
	memcpy(&sync_offset, record + 0x20, 8);
	memcpy(&sequence, record + 0xa0, 8);
	memcpy(&padded, record + 0x30, 4);
	memcpy(&code, record + 0x34, 4);
	memcpy(&initial, record + 0x38, 4);
	memcpy(&flags, record + 0x3c, 4);
	memcpy(&exact, record + 0x124, 4);
	memcpy(&page_command, source->page_record, 8);
	memcpy(&page_bytes, source->page_record + 8, 8);
	memcpy(&roots, source->root_export, 8);
	memcpy(&root_command, source->root_export + 8, 8);
	if (roots != 1 || command != page_command || command != root_command || page_bytes != exact ||
	    !exact || exact > 320 || (exact != 76 && exact % 80) || padded != ((exact + 31) & ~31U) ||
	    flags != 0x08040001 || sequence != 1 || sync_offset || ((code | initial) & 15))
		return -EINVAL;
	/* Reject records outside the traced fresh-copy/single-clear subset, rather than silently
 * discarding optional synchronization or state carried by unknown fields. */
	for (i = 0; i < sizeof(record); i++)
		if (!((i >= 0x18 && i < 0x20) || (i >= 0x30 && i < 0x40) ||
		      (i >= 0xa0 && i < 0xa8) || i >= 0x124) && record[i])
			return -EOPNOTSUPP;
	if ((command & 4095) || command < 0x40000000ULL || command > 0x8040000000ULL - 4096)
		return -ERANGE;
	if ((in.dma_va < in.state_va + 4096 && in.state_va < in.dma_va + MT_TQX_DMA_ALLOCATION) ||
	    (in.dma_va < command + 4096 && command < in.dma_va + MT_TQX_DMA_ALLOCATION) ||
	    (in.state_va < command + 4096 && command < in.state_va + 4096))
		return -EINVAL;
	/* The original layout reserves three section slots, one 0xe8-byte record,
 * per-core 128-byte page slots, and a 0x1020-byte auxiliary area. */
	total = (0x200 + in.cores * 128 + 0x1020 + 127) & ~127U;
	memset(image, 0, sizeof(*image));
	image->bytes = total;
	mt_fw_put32(image->descriptor, 0x1c, 0x67);
	mt_fw_put32(image->descriptor, 0x28, 0x48);
	mt_fw_put32(image->descriptor, 0x2c, 1);
	mt_fw_put32(image->descriptor, 0x48, 3);
	mt_fw_put32(image->descriptor, 0x50, 0x90);
	mt_fw_put32(image->descriptor, 0x58, 0xd0);
	mt_fw_put32(image->descriptor, 0x94, 0x08000000);
	mt_fw_put32(image->descriptor, 0x98, 0x10);
	mt_fw_put32(image->descriptor, 0x9c, 1);
	mt_fw_put32(image->descriptor, 0xa8, 0xe8);
	memcpy(image->descriptor + 0x148, record + 0x120, 8);
	mt_fw_put64(image->descriptor, 0x160, command);
	mt_fw_put64(image->descriptor, 0x168, in.dma_va + 0x200);
	mt_fw_put64(image->descriptor, 0x170, state.encoded_va);
	memcpy(image->descriptor + 0x178, record + 0x30, 16);
	for (i = 0; i < in.cores; i++)
		mt_fw_put32(image->descriptor, 0x200 + i * 128, exact);
	mt_fw_put64(image->submission_view, 0, in.dma_va);
	mt_fw_put64(image->submission_view, 8, total);
	return 0;
}
#endif
