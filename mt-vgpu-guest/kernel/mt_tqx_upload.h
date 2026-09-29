/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_TQX_UPLOAD_H
#define MT_GUEST_TQX_UPLOAD_H
#include "mt_tqx_heap.h"

/* Software records are retained separately, not uploaded as GPU programs.
 * mt_tqx_dma_encode converts this result into a separate DMA image. */
struct mt_tqx_upload_result {
	u8 record[MT_TQX_RECORD_BYTES], page_record[16], root_export[16];
};
struct mt_tqx_upload_workspace {
	struct mt_tqx_copy_stream_image stream;
	u8 write_page[4096], read_page[4096];
};
struct mt_tqx_upload_ops {
	int (*write)(struct mt_bo *, u64, const void *, u64);
	int (*read)(struct mt_bo *, u64, void *, u64);
};

/* Internal page packing from an already validated stream. All padding,
 * including shader allocation slack, is initialized by Linux policy. */
static inline void mt_tqx_upload_pack_page(u8 page[4096], u32 slot, u32 offset,
		const struct mt_tqx_copy_stream_image *stream)
{
	u32 i, bytes;
	memset(page, 0, 4096);
	switch (slot) {
	case MT_TQX_COMMAND:
		memcpy(page, stream->commands, stream->command_bytes);
		break;
	case MT_TQX_SHADER:
		if (offset < MT_TQX_SHADER_BANK_BYTES) {
			bytes = MT_TQX_SHADER_BANK_BYTES - offset;
			if (bytes > 4096)
				bytes = 4096;
			memcpy(page, mt_tqx_program_bytes + offset, bytes);
		}
		break;
	case MT_TQX_PDS_CODE:
		memcpy(page, mt_tqx_program_bytes + MT_TQX_SHADER_BANK_BYTES,
			MT_TQX_PROGRAM_BANK_BYTES - MT_TQX_SHADER_BANK_BYTES);
		break;
	case MT_TQX_PDS_STATE:
		memcpy(page, stream->pds_initial, 16);
		for (i = 0; i < stream->count; i++) {
			u32 state = 16 + i * 80;
			memcpy(page + state, stream->programs[i].pds_execution, 36);
			memcpy(page + state + 36, stream->programs[i].constants, 20);
			memcpy(page + state + 64, stream->programs[i].pds_constants, 16);
		}
		break;
	case MT_TQX_TEXTURE:
		memcpy(page, stream->sources, stream->count * MT_TQX_TEXTURE_BYTES);
		break;
	}
}

/* Hold the common session lock throughout validation, all I/O, and reference
 * release. Workspace must be private heap storage (9504 bytes), not stack.
 * The GEM bridge owns references to all seven objects during this operation.
 * No mapping or data may change concurrently. Existing CPU/GPU users are
 * rejected before any write. A sealed VM is accepted while idle because this
 * only updates contents inside existing mappings; its page tables stay fixed.
 * On I/O error the output records are unchanged, but BO contents may be partial:
 * retry the full upload or discard the buffers. Success neither pins a future
 * job nor publishes it.
 */
static inline int mt_tqx_upload(void *out, u32 capacity,
		struct mt_tqx_upload_workspace *workspace, const struct mt_tqx_upload_ops *ops,
		const struct mt_device_profile *profile, struct mt_gpu_vm *vm,
		struct mt_bo *const bo[MT_TQX_BUFFER_COUNT], const struct mt_tqx_heap_input *in)
{
	const struct mt_bo *resources[MT_TQX_BUFFER_COUNT];
	u64 offsets[5], physical;
	u32 i, offset;
	int ret;
	if (!out || capacity < sizeof(struct mt_tqx_upload_result) || !workspace ||
	    !ops || !ops->write || !ops->read || !vm || !bo)
		return -EINVAL;
	if (vm->active_uses)
		return -EBUSY;
	for (i = 0; i < MT_TQX_BUFFER_COUNT; i++) {
		if (!bo[i] || !bo[i]->refs)
			return -EINVAL;
		if (bo[i]->cpu_users || bo[i]->gpu_users)
			return -EBUSY;
		resources[i] = bo[i];
	}
	ret = mt_tqx_heap_stream_prepare(&workspace->stream, sizeof(workspace->stream),
		profile, vm, resources, in);
	if (ret)
		return ret;
	/* Resolve each VA to the offset inside its BO; bindings may start at
 * nonzero BO offsets or contain multiple disjoint regions of one object. */
	for (i = 0; i < 5; i++) {
		ret = mt_ce_copy_resolve(vm, bo[i], in->va[i], mt_tqx_buffer_bytes[i], &physical);
		if (ret)
			return ret;
		offsets[i] = physical - bo[i]->backing.gpu_pa;
	}
	for (i = 0; i < 5; i++) {
		for (offset = 0; offset < mt_tqx_buffer_bytes[i]; offset += 4096) {
			mt_tqx_upload_pack_page(workspace->write_page, i, offset, &workspace->stream);
			ret = ops->write(bo[i], offsets[i] + offset, workspace->write_page, 4096);
			if (ret)
				return ret;
			ret = ops->read(bo[i], offsets[i] + offset, workspace->read_page, 4096);
			if (ret)
				return ret;
			if (memcmp(workspace->write_page, workspace->read_page, 4096))
				return -EIO;
		}
	}
	memcpy(out, workspace->stream.record, MT_TQX_RECORD_BYTES);
	memcpy((u8 *)out + MT_TQX_RECORD_BYTES, workspace->stream.page_record, 16);
	memcpy((u8 *)out + MT_TQX_RECORD_BYTES + 16, workspace->stream.root_export, 16);
	return 0;
}
#endif
