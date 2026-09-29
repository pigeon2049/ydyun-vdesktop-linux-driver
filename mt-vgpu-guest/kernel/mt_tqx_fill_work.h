/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_TQX_FILL_WORK_H
#define MT_GUEST_TQX_FILL_WORK_H
#include "mt_tqx_fill.h"
#include "mt_tqx_work.h"
#define MT_TQX_FILL_MAX_SURFACE_BYTES (8U * 1024U * 1024U)

struct mt_tqx_fill_workspace {
	struct mt_tqx_fill_image fill;
	struct mt_tqx_upload_result records;
	struct mt_tqx_dma_image dma;
	u8 write_page[4096], read_page[4096];
};

/* A context must already own the three cache slices. The front end prepares
 * these before exposing any GEM handle. This function neither allocates slices
 * nor changes mappings. ordinary: command, destination, DMA, engine state.
 * Engine state is never reinitialized. All buffers are referenced and the
 * caller holds the shared session lock throughout upload and job pinning. */
static inline int mt_tqx_fill_work_prepare(struct mt_tqx_work *work,
		struct mt_tqx_fill_workspace *w, const struct mt_tqx_upload_ops *io,
		struct mt_boot_bo_store *shared, const struct mt_device_profile *profile,
		u32 cores, struct mt_execution_context *context, struct mt_bo *const ordinary[4],
		const struct mt_tqx_fill_input *input, u64 dma_va, u64 state_va)
{
	struct mt_tqx_fill_input in;
	struct mt_tqx_dma_input din = {dma_va, state_va, cores};
	struct mt_guest_heap_plan heaps;
	struct mt_gpu_vm *vm;
	struct mt_bo *bo[7];
	u64 va[7], physical[7], relative[7], offsets[7], frame;
	u32 bytes[7] = {4096, 20480, 4096, 4096, 0, 8192, 4096};
	const u32 heap[7] = {0, 2, 1, 1, 0, 0, 0};
	const u32 pool_order[3] = {1, 0, 2};
	u32 i, j, offset, count;
	int ret;
	if (!work || work->context || work->job.state != MT_JOB_EMPTY || !w || !io ||
	    !io->write || !io->read || !shared || !shared->buffers || !shared->pools ||
	    !context || !context->process || !context->process->store || !ordinary || !input)
		return -EINVAL;
	lockdep_assert_held(shared->buffers->lock);
	if (!profile || profile->family != 2 || profile->transfer_version != 1 ||
	    context->route.type != 1 || context->route.dm != 1)
		return -EOPNOTSUPP;
	if (memcmp(profile, &context->process->store->profile, sizeof(*profile)) ||
	    context->process->store->buffers != shared->buffers)
		return -EXDEV;
	vm = context->process->vm;
	if (!vm || !vm->tables || vm->tables->store != shared->buffers ||
	    !mt_boot_bo_bound(shared, vm) || (vm->sealed && !vm->uploaded))
		return -EXDEV;
	if (context->active_jobs || vm->active_uses || vm->tables->cpu_users || vm->tables->gpu_users)
		return -EBUSY;
	for (i = 0; i < 3; i++) {
		struct mt_pool_slice *s = context->tqx_pool_slices[i];
		if (work->pool_slices[i] || !s || s->context != context ||
		    s->state != &shared->pools->slices[pool_order[i]] ||
		    s->bo != shared->slots[MT_PROCESS_SHARED_COUNT + pool_order[i]] ||
		    s->bytes < (i == 0 ? 20480 : 4096))
			return -EXDEV;
	}
	in = *input;
	/* Public native-fill subset: one 32-bit pixel per element. */
	if (in.element_bytes != 4 || !in.width || !in.height ||
	    in.width > 32768 || in.height > 32768 || (in.destination_va & 3))
		return -EINVAL;
	frame = (u64)in.width * in.height * 4;
	if (frame > MT_TQX_FILL_MAX_SURFACE_BYTES)
		return -E2BIG;
	bytes[4] = frame;
	bo[0] = ordinary[0]; bo[1] = context->tqx_pool_slices[0]->bo;
	bo[2] = shared->slots[MT_SHARED_PDS]; bo[3] = context->tqx_pool_slices[1]->bo;
	bo[4] = ordinary[1]; bo[5] = ordinary[2]; bo[6] = ordinary[3];
	va[0] = in.command_va; va[1] = context->tqx_pool_slices[0]->va;
	va[2] = 0x81ffc00000ULL; va[3] = context->tqx_pool_slices[1]->va;
	va[4] = in.destination_va; va[5] = dma_va; va[6] = state_va;
	mt_guest_plan_heaps(&heaps);
	for (i = 0; i < 7; i++) {
		if (!bo[i] || bo[i]->store != shared->buffers || !bo[i]->refs)
			return -EXDEV;
		if (bo[i]->cpu_users || bo[i]->gpu_users)
			return -EBUSY;
		ret = mt_tqx_heap_relative(&heaps.heaps[heap[i]], va[i], bytes[i],
			i == 4 ? 4 : 4096, &relative[i]);
		if (ret)
			return ret;
		ret = mt_ce_copy_resolve_access(vm, bo[i], va[i], bytes[i], i >= 4, &physical[i]);
		if (ret)
			return ret;
		offsets[i] = physical[i] - bo[i]->backing.gpu_pa;
		for (j = 0; j < i; j++)
			if (physical[i] < physical[j] + bytes[j] && physical[j] < physical[i] + bytes[i])
				return -EINVAL;
	}
	in.shader_heap_base = relative[1]; in.pds_code_heap_base = relative[2];
	in.pds_initial_state = relative[3];
	ret = mt_tqx_fill_build(&w->fill, sizeof(w->fill), &in);
	if (ret)
		return ret;
	memcpy(w->records.record, w->fill.record, MT_TQX_RECORD_BYTES);
	memcpy(w->records.page_record, w->fill.page_record, 16);
	mt_fw_put64(w->records.root_export, 0, 1);
	mt_fw_put64(w->records.root_export, 8, in.command_va);
	ret = mt_tqx_dma_encode(&w->dma, sizeof(w->dma), profile, &w->records, &din);
	if (ret)
		return ret;
	/* Upload only command, static programs, initial PDS state and DMA.
	 * The destination receives no CPU write anywhere in this path. */
	for (i = 0; i < 6; i++) {
		if (i == 4)
			continue;
		for (offset = 0; offset < bytes[i]; offset += 4096) {
			memset(w->write_page, 0, 4096);
			if (i == 0)
				memcpy(w->write_page, w->fill.command, MT_TQX_FILL_BYTES);
			else if (i == 1 && offset < MT_TQX_SHADER_BANK_BYTES) {
				count = min_t(u32, 4096, MT_TQX_SHADER_BANK_BYTES - offset);
				memcpy(w->write_page, mt_tqx_program_bytes + offset, count);
			} else if (i == 2)
				memcpy(w->write_page, mt_tqx_program_bytes + MT_TQX_SHADER_BANK_BYTES,
					MT_TQX_PROGRAM_BANK_BYTES - MT_TQX_SHADER_BANK_BYTES);
			else if (i == 3)
				memcpy(w->write_page, w->fill.pds_initial, 16);
			else if (i == 5)
				memcpy(w->write_page, w->dma.descriptor + offset, 4096);
			ret = io->write(bo[i], offsets[i] + offset, w->write_page, 4096);
			if (!ret)
				ret = io->read(bo[i], offsets[i] + offset, w->read_page, 4096);
			if (!ret && memcmp(w->write_page, w->read_page, 4096))
				ret = -EIO;
			if (ret)
				return ret;
		}
	}
	ret = mt_tqx_work_hold_submission(work, io, context, ordinary[2], w->dma.submission_view);
	if (!ret)
		memcpy(work->pool_slices, context->tqx_pool_slices, sizeof(work->pool_slices));
	return ret;
}
#endif
