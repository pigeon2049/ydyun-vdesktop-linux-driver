/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_TQX_JOB_H
#define MT_GUEST_TQX_JOB_H
#include "mt_tqx_destination.h"
#include "mt_tqx_program.h"

struct mt_tqx_job_input {
	u64 source_va, destination_va, constants_va;
	u32 element_bytes, width, height;
	u32 shader_heap_base, source_descriptor_index, pds_code_heap_base;
	u32 pds_execution_state, pds_constant_state;
};
struct mt_tqx_job_image {
	u8 source[MT_TQX_TEXTURE_BYTES];
	struct mt_tqx_program_state program;
	u8 destination[MT_TQX_DESTINATION_BYTES];
};

/* Internal encoder into an unpublished staging image. Parts may be partially
 * written on failure; transactional public builders keep them in local storage.
 * Sharing this avoids nesting full job images on the kernel stack. */
static inline int mt_tqx_job_parts_build(u8 *source_out, struct mt_tqx_program_state *program_out,
		u8 *destination_out, const struct mt_tqx_job_input *in)
{
	struct mt_tqx_texture_input source;
	struct mt_tqx_program_input program;
	struct mt_tqx_destination_input destination;
	u64 code;
	int ret;
	source = (struct mt_tqx_texture_input){in->source_va,in->element_bytes,in->width,in->height};
	ret = mt_tqx_texture_build(source_out, MT_TQX_TEXTURE_BYTES, &source);
	if (ret)
		return ret;
	program = (struct mt_tqx_program_input){
		.constants_va=in->constants_va, .shader_heap_base=in->shader_heap_base,
		.source_descriptor_index=in->source_descriptor_index,
		.operation=in->element_bytes <= 4 ? 8 : (in->element_bytes == 8 ? 16 : 21)};
	ret = mt_tqx_program_state_build(program_out, sizeof(*program_out), &program);
	if (ret)
		return ret;
	code = (u64)in->pds_code_heap_base + program_out->pds_constants_offset;
	if (code > 0xffffffffULL)
		return -ERANGE;
	destination = (struct mt_tqx_destination_input){
		.destination_va=in->destination_va, .element_bytes=in->element_bytes,
		.width=in->width, .height=in->height, .pds_code=code,
		.pds_execution_state=in->pds_execution_state, .pds_constant_state=in->pds_constant_state};
	return mt_tqx_destination_build(destination_out, MT_TQX_DESTINATION_BYTES, &destination);
}

/* One CPU-side linear-copy job; heap/VM ownership is checked by its caller.
 * This isolated image lacks stream finalization and is never published here. */
static inline int mt_tqx_job_build(void *out, u32 capacity, const struct mt_tqx_job_input *in)
{
	struct mt_tqx_job_image next;
	int ret;
	if (!out || !in || capacity < sizeof(next))
		return -EINVAL;
	ret = mt_tqx_job_parts_build(next.source, &next.program, next.destination, in);
	if (ret)
		return ret;
	memcpy(out, &next, sizeof(next));
	return 0;
}
#endif
