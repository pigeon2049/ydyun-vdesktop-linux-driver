/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_TQX_HEAP_H
#define MT_GUEST_TQX_HEAP_H
#include "mt_guest_heaps.h"
#include "mt_tqx_copy_stream.h"

enum mt_tqx_buffer_slot {
	MT_TQX_COMMAND, MT_TQX_SHADER, MT_TQX_PDS_CODE, MT_TQX_PDS_STATE,
	MT_TQX_TEXTURE, MT_TQX_SOURCE, MT_TQX_DESTINATION, MT_TQX_BUFFER_COUNT
};
/* Linux page allocation policy. Shader allocation covers the reference's
 * 0x4f00-byte family-2 allocation; only 0x4e80 bytes contain program code.
 * These are required mapped extents, not physical allocation addresses. */
static const u32 mt_tqx_buffer_bytes[5] = {4096, 20480, 4096, 4096, 4096};
static const u32 mt_tqx_buffer_heaps[5] = {0, 2, 1, 1, 10};
struct mt_tqx_heap_input {
	struct mt_tqx_copy_input copy;
	u64 va[5];
};

/* 093494 maps external heap 0/1/2/10 to internal 0/4/3/8.
 * 0a21f8 subtracts the internal heap base. Linux additionally validates the
 * complete span. Within the reserved tail only the three proven dynamic
 * pools and static PDS range are accepted, as returned by 041cc4.
 * Caller supplies a heap from mt_guest_plan_heaps, never an arbitrary table. */
static inline int mt_tqx_heap_relative(const struct mt_guest_heap *heap,
		u64 va, u32 bytes, u32 alignment, u64 *relative)
{
	struct mt_guest_pool_spec pools[MT_GUEST_POOL_COUNT];
	u64 available, offset;
	u32 i;
	bool reserved = false;
	if (!heap || !relative || !bytes || !alignment || (alignment & (alignment - 1)) ||
	    (va & (alignment - 1)))
		return -EINVAL;
	if (heap->reserved_size > heap->size || va < heap->base)
		return -ERANGE;
	available = heap->size - heap->reserved_size;
	offset = va - heap->base;
	if (offset >= heap->size || bytes > heap->size - offset ||
	    va >= (1ULL << MT_GPU_VA_BITS) || bytes > (1ULL << MT_GPU_VA_BITS) - va)
		return -ERANGE;
	if (offset >= available || bytes > available - offset) {
		mt_guest_plan_pools(pools);
		for (i = 0; i < MT_GUEST_POOL_COUNT; i++)
			if (va >= pools[i].va && va - pools[i].va < pools[i].bytes &&
			    bytes <= pools[i].bytes - (va - pools[i].va))
				reserved = true;
		/* Callback type 5 returns the fixed static-PDS descriptor. */
		if (va >= 0x81ffc00000ULL && va - 0x81ffc00000ULL < 0x100000 &&
		    bytes <= 0x100000 - (va - 0x81ffc00000ULL))
			reserved = true;
		if (!reserved)
			return -ERANGE;
	}
	*relative = offset;
	return 0;
}

/* Translate five mapped page ranges into the distinct address domains used
 * by the encoder. Page packing is our policy, not the Windows allocator's:
 * state +0 initial PDS, then 80 bytes per job: +0 execution, +36 constants,
 * +64 constant-load state. Texture descriptors occupy 64 bytes per job.
 * Command allocator vtable 141021938+28 -> 045374 selects pool 0, whereas
 * state type 5 selects texture pool 5; the constructor's engine 5 is unrelated.
 * CPU staging only: no allocation, VM publication or firmware submission. */
static inline int mt_tqx_heap_stream_input(void *out, u32 capacity,
		const struct mt_device_profile *profile, const struct mt_tqx_heap_input *in)
{
	struct mt_tqx_copy_stream_input next = {0};
	struct mt_guest_heap_plan heaps;
	struct mt_tqx_chunk_plan chunks;
	u64 relative[5];
	u32 i;
	int ret;
	if (!out || !in || capacity < sizeof(next))
		return -EINVAL;
	if (!profile || profile->family != 2 || profile->transfer_version != 1)
		return -EOPNOTSUPP;
	ret = mt_tqx_copy_chunks_build(&chunks, sizeof(chunks), &in->copy);
	if (ret)
		return ret;
	mt_guest_plan_heaps(&heaps);
	for (i = 0; i < 5; i++) {
		ret = mt_tqx_heap_relative(&heaps.heaps[mt_tqx_buffer_heaps[i]],
			in->va[i], mt_tqx_buffer_bytes[i], 4096, &relative[i]);
		if (ret)
			return ret;
	}
	if (in->va[MT_TQX_PDS_CODE] == in->va[MT_TQX_PDS_STATE])
		return -EINVAL;
	next.copy = in->copy;
	next.command_va = in->va[MT_TQX_COMMAND];
	next.shader_heap_base = relative[MT_TQX_SHADER];
	next.pds_code_heap_base = relative[MT_TQX_PDS_CODE];
	next.pds_initial_state = relative[MT_TQX_PDS_STATE];
	for (i = 0; i < chunks.count; i++) {
		u32 state = 16 + i * 80;
		next.addresses[i] = (struct mt_tqx_chunk_addresses){
			.constants_va = in->va[MT_TQX_PDS_STATE] + state + 36,
			.source_descriptor_index = (relative[MT_TQX_TEXTURE] + i * 64) >> 4,
			.pds_execution_state = relative[MT_TQX_PDS_STATE] + state,
			.pds_constant_state = relative[MT_TQX_PDS_STATE] + state + 64};
	}
	memcpy(out, &next, sizeof(next));
	return 0;
}

/* Called under the VM/BO lock. Each expected BO must cover its entire mapped
 * page range. Reject physical aliases, including shader/command/state aliases
 * at different VAs. The result owns no GPU references and cannot be submitted.
 */
static inline int mt_tqx_heap_stream_prepare(void *out, u32 capacity,
		const struct mt_device_profile *profile, const struct mt_gpu_vm *vm,
		const struct mt_bo *const bo[MT_TQX_BUFFER_COUNT], const struct mt_tqx_heap_input *in)
{
	struct mt_tqx_copy_stream_input request;
	u64 physical[MT_TQX_BUFFER_COUNT], va;
	u32 bytes[MT_TQX_BUFFER_COUNT], i, j;
	int ret;
	if (!out || capacity < sizeof(struct mt_tqx_copy_stream_image) || !bo)
		return -EINVAL;
	ret = mt_tqx_heap_stream_input(&request, sizeof(request), profile, in);
	if (ret)
		return ret;
	for (i = 0; i < MT_TQX_BUFFER_COUNT; i++) {
		bytes[i] = i < 5 ? mt_tqx_buffer_bytes[i] : in->copy.bytes;
		va = i < 5 ? in->va[i] : (i == MT_TQX_SOURCE ? in->copy.src : in->copy.dst);
		ret = mt_ce_copy_resolve(vm, bo[i], va, bytes[i], &physical[i]);
		if (ret)
			return ret;
		for (j = 0; j < i; j++)
			if (physical[i] < physical[j] + bytes[j] && physical[j] < physical[i] + bytes[i])
				return -EINVAL;
	}
	return mt_tqx_copy_stream_build(out, capacity, &request);
}
#endif
