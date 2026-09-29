/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_TQX_STREAM_H
#define MT_GUEST_TQX_STREAM_H
#include "mt_tqx_job.h"

#define MT_TQX_RECORD_BYTES 296U
struct mt_tqx_stream_input {
	struct mt_tqx_job_input job;
	u64 command_va;
	u32 pds_initial_state;
};
struct mt_tqx_stream_image {
	struct mt_tqx_job_image job;
	u8 pds_initial[16];
	/* Software library records, not firmware queue or DMA descriptors. */
	u8 record[MT_TQX_RECORD_BYTES];
	u8 page_record[16];
};

/* Family-2 fresh single-page, single-job copy, no external sync/saved state.
 * 113c08 includes 11e6dc/1168e4 initialization and 0dad24 completion;
 * 11d194 flushes 11e768, records the page, and backpatches record size.
 * Heap/VM ownership and allocator lifetime are caller responsibilities.
 * Output pieces require separate GPU allocations: never upload as one blob. */
/* Internal record encoder; all output arrays belong to an unpublished local
 * image. Keeping this separate avoids stacking two full images for batching. */
static inline int mt_tqx_stream_finish(u8 *pds_initial, u8 *record, u8 *page_record,
		const struct mt_tqx_stream_input *in, u32 command_bytes)
{
	const struct mt_tqx_pds_meta *initial = &mt_tqx_pds[1];
	const struct mt_tqx_shader_meta *shader = &mt_tqx_shaders[1];
	u64 initial_shader, initial_code;
	if (!in->command_va || (in->command_va & 4095) || (in->pds_initial_state & 15) ||
	    !command_bytes || command_bytes > 320 ||
	    (command_bytes != 76 && command_bytes % MT_TQX_DESTINATION_BYTES))
		return -EINVAL;
	if (in->command_va >= (1ULL << MT_GPU_VA_BITS) ||
	    (u64)in->pds_initial_state + 16 > (1ULL << 32))
		return -ERANGE;
	initial_shader = (u64)in->job.shader_heap_base + shader->offset;
	initial_code = (u64)in->job.pds_code_heap_base + initial->offset;
	if (initial_shader + shader->code_dwords * 4 > (1ULL << 32) ||
	    initial_code + initial->code_dwords * 4 > (1ULL << 32))
		return -ERANGE;
	if (initial->parameters != 4 || initial->patch_dword != 2)
		return -EINVAL;
	memcpy(pds_initial, &mt_tqx_pds_state[initial->state_start], 16);
	mt_fw_put32(pds_initial, initial->patch_dword * 4, initial_shader);
	memset(record, 0, MT_TQX_RECORD_BYTES);
	memset(page_record, 0, 16);
	mt_fw_put64(record, 0x18, in->command_va);
	/* 0db2c8: ceil(page_bytes/32) << 5, distinct from exact job bytes. */
	mt_fw_put32(record, 0x30, (command_bytes + 31) & ~31U);
	mt_fw_put32(record, 0x34, initial_code);
	mt_fw_put32(record, 0x38, in->pds_initial_state);
	mt_fw_put32(record, 0x3c, 0x08040001);
	mt_fw_put64(record, 0xa0, 1); /* Fresh constructor sequence number. */
	mt_fw_put32(record, 0x124, command_bytes);
	mt_fw_put64(page_record, 0, in->command_va);
	mt_fw_put64(page_record, 8, command_bytes);
	return 0;
}

static inline int mt_tqx_stream_build(void *out, u32 capacity,
		const struct mt_tqx_stream_input *in)
{
	struct mt_tqx_stream_image next = {0};
	int ret;
	if (!out || !in || capacity < sizeof(next))
		return -EINVAL;
	ret = mt_tqx_job_build(&next.job, sizeof(next.job), &in->job);
	if (ret)
		return ret;
	ret = mt_tqx_stream_finish(next.pds_initial, next.record, next.page_record,
		in, MT_TQX_DESTINATION_BYTES);
	if (ret)
		return ret;
	/* 0dad64 replaces the last control word instead of appending an end. */
	mt_fw_put32(next.job.destination, 72, 0x2d);
	memcpy(out, &next, sizeof(next));
	return 0;
}
#endif
