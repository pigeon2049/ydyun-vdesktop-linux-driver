/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_TQX_PROGRAM_H
#define MT_GUEST_TQX_PROGRAM_H
#include "mt_device_profile.h"
#include "mt_fw_layout.h"
#include "mt_tqx_program_data.h"

/* The embedded bytes use the family < 4 reference table, currently enabled
 * only for the traced local family 2. The concatenated CPU image contains two
 * banks: shader bytes, then PDS bytes. The reference allocates these in separate
 * GPU heaps (12369c), so this image must not be uploaded as a single GPU bank.
 * This function does not allocate or upload a BO. */
static inline int mt_tqx_program_bank_build(void *out, u32 capacity,
		const struct mt_device_profile *profile)
{
	if (!out || capacity < MT_TQX_PROGRAM_BANK_BYTES)
		return -EINVAL;
	if (!profile || profile->family != 2 || profile->transfer_version != 1)
		return -EOPNOTSUPP;
	memcpy(out, mt_tqx_program_bytes, MT_TQX_PROGRAM_BANK_BYTES);
	return 0;
}

/* Caller supplies 32-bit heap-relative shader address, encoded source texture
 * descriptor index, and GPU VA of the constant allocation. These are different
 * address domains. No guessed VA -> heap offset conversion is performed here. */
struct mt_tqx_program_input {
	u64 constants_va;
	u32 operation, shader_heap_base, source_descriptor_index;
};
struct mt_tqx_program_state {
	u8 constants[20];
	u8 pds_constants[16];
	u8 pds_execution[36];
	u32 shader_offset, shader_bytes;
	u32 pds_constants_offset, pds_execution_offset;
	u32 constants_dwords, pds_constants_dwords;
};

/* Restricted 1401169fc / 140114290 (equal rectangles, no transform),
 * 14011e5bc / 1400fbab8, and 14011ccdc / 14011b7f0 CPU staging.
 * Source texture descriptor encoding and destination command emission are
 * intentionally outside this structure: it is not executable by itself. */
static inline int mt_tqx_program_state_build(void *out, u32 capacity,
		const struct mt_tqx_program_input *in)
{
	struct mt_tqx_program_state next = {0};
	const struct mt_tqx_shader_meta *shader;
	const struct mt_tqx_pds_meta *constants = &mt_tqx_pds[4], *execution = &mt_tqx_pds[9];
	u32 raw[5], i;
	u64 shader_address, execution_word;
	if (!out || !in || capacity < sizeof(next))
		return -EINVAL;
	if (in->operation != 8 && in->operation != 0x10 && in->operation != 0x15)
		return -EOPNOTSUPP;
	if ((in->constants_va & 3) || (in->shader_heap_base & 0x7f))
		return -EINVAL;
	if (in->constants_va >= (1ULL << MT_GPU_VA_BITS) ||
	    20 > (1ULL << MT_GPU_VA_BITS) - in->constants_va)
		return -ERANGE;
	shader = &mt_tqx_shaders[in->operation];
	shader_address = (u64)in->shader_heap_base + shader->offset;
	if (shader_address > 0xffffffffULL ||
	    shader->code_dwords * 4 > (1ULL << 32) - shader_address)
		return -ERANGE;
	if (shader->parameters != 5 || constants->parameters != 4 || execution->parameters != 9)
		return -EINVAL;
	raw[0] = in->source_descriptor_index;
	raw[1] = raw[2] = 0x3f800000U; /* IEEE-754 1.0; no kernel floating point. */
	raw[3] = raw[4] = 0;
	for (i = 0; i < 5; i++) {
		u64 map = mt_tqx_parameter_map[shader->map_start + i];
		u32 value;
		if (!(map >> 32)) {
			if (map >= 5)
				return -EINVAL;
			value = raw[map];
		} else {
			value = map;
		}
		mt_fw_put32(next.constants, i * 4, value);
	}
	memcpy(next.pds_constants, &mt_tqx_pds_state[constants->state_start], 16);
	memcpy(next.pds_execution, &mt_tqx_pds_state[execution->state_start], 36);
	mt_fw_put64(next.pds_constants, constants->patch_dword * 4, in->constants_va);
	mt_fw_put32(next.pds_constants, constants->patch_dword * 4 + 8, 0x98000005U);
	execution_word = shader_address | ((u64)((shader->outputs + 1) / 2) << 35);
	mt_fw_put64(next.pds_execution, execution->patch_dword * 4, execution_word);
	next.shader_offset = shader->offset;
	next.shader_bytes = shader->code_dwords * 4;
	next.pds_constants_offset = constants->offset;
	next.pds_execution_offset = execution->offset;
	next.constants_dwords = 5;
	next.pds_constants_dwords = 4;
	memcpy(out, &next, sizeof(next));
	return 0;
}
#endif
