/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_TQX_COPY_STREAM_H
#define MT_GUEST_TQX_COPY_STREAM_H
#include "mt_tqx_stream.h"
#include "mt_tqx_copy.h"

struct mt_tqx_chunk_addresses {
	u64 constants_va;
	u32 source_descriptor_index, pds_execution_state, pds_constant_state;
};
struct mt_tqx_copy_stream_input {
	struct mt_tqx_copy_input copy;
	u64 command_va;
	u32 shader_heap_base, pds_code_heap_base, pds_initial_state;
	struct mt_tqx_chunk_addresses addresses[MT_TQX_COPY_CHUNKS];
};
struct mt_tqx_copy_stream_image {
	u32 count, command_bytes;
	u8 commands[MT_TQX_COPY_CHUNKS * MT_TQX_DESTINATION_BYTES];
	u8 sources[MT_TQX_COPY_CHUNKS][MT_TQX_TEXTURE_BYTES];
	struct mt_tqx_program_state programs[MT_TQX_COPY_CHUNKS];
	u8 pds_initial[16], record[MT_TQX_RECORD_BYTES], page_record[16];
	u8 root_export[16]; /* 11d6bc: record count, then command root VA. */
};
/* A single region from a fresh command object: 10f034 -> 113c08 per chunk,
 * then 11d194. At most 320 command bytes fit in one 4096-byte page. This
 * stages CPU bytes only; supplied addresses still need heap/BO/VM ownership
 * checks and allocation lifetime. No firmware publication is performed. */
static inline int mt_tqx_copy_stream_build(void *out, u32 capacity,
		const struct mt_tqx_copy_stream_input *in)
{
	struct mt_tqx_copy_stream_image next = {0};
	struct mt_tqx_chunk_plan chunks;
	struct mt_tqx_stream_input request;
	u32 i;
	int ret;
	if (!out || !in || capacity < sizeof(next))
		return -EINVAL;
	ret = mt_tqx_copy_chunks_build(&chunks, sizeof(chunks), &in->copy);
	if (ret)
		return ret;
	next.count = chunks.count;
	next.command_bytes = chunks.count * MT_TQX_DESTINATION_BYTES;
	for (i = 0; i < chunks.count; i++) {
		const struct mt_tqx_chunk *c = &chunks.chunks[i];
		const struct mt_tqx_chunk_addresses *a = &in->addresses[i];
		request = (struct mt_tqx_stream_input){
			.job={.source_va=in->copy.src+c->offset,.destination_va=in->copy.dst+c->offset,
				.constants_va=a->constants_va,.element_bytes=c->element_bytes,
				.width=c->width,.height=c->height,.shader_heap_base=in->shader_heap_base,
				.source_descriptor_index=a->source_descriptor_index,
				.pds_code_heap_base=in->pds_code_heap_base,
				.pds_execution_state=a->pds_execution_state,.pds_constant_state=a->pds_constant_state},
			.command_va=in->command_va,.pds_initial_state=in->pds_initial_state};
		ret = mt_tqx_job_parts_build(next.sources[i], &next.programs[i],
			next.commands + i * MT_TQX_DESTINATION_BYTES, &request.job);
		if (ret)
			return ret;
		if (!i) {
			ret = mt_tqx_stream_finish(next.pds_initial, next.record, next.page_record,
				&request, next.command_bytes);
			if (ret)
				return ret;
		}
		mt_fw_put32(next.commands, i * MT_TQX_DESTINATION_BYTES + 72,
			i + 1 == chunks.count ? 0x2d : 0x25);
	}
	mt_fw_put64(next.root_export, 0, 1);
	mt_fw_put64(next.root_export, 8, in->command_va);
	memcpy(out, &next, sizeof(next));
	return 0;
}
#endif
