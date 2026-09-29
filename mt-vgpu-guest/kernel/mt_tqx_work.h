/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_TQX_WORK_H
#define MT_GUEST_TQX_WORK_H
#include "mt_tqx_submission.h"
#include "mt_work_job.h"
#include "mt_execution_context.h"
#include "mt_boot_bo.h"

/* Caller-owned until cancellation or transfer to a pending fence. Never copy.
 * pool_slices are borrowed from the execution-context cache, which keeps their
 * addresses and allocations stable across repeated jobs. */
struct mt_tqx_work {
	struct mt_work_job job;
	struct mt_execution_context *context;
	struct mt_pool_slice *pool_slices[3];
};

static inline int mt_tqx_work_prepare(struct mt_tqx_work *work,
		struct mt_tqx_submission_workspace *workspace, const struct mt_tqx_upload_ops *io,
		const struct mt_device_profile *profile, u32 cores,
		struct mt_execution_context *context,
		struct mt_bo *const bo[MT_TQX_SUBMISSION_BUFFERS],
		const struct mt_tqx_submission_input *input);

static inline void mt_pool_slice_array_release(struct mt_pool_slice **slices, u32 count)
{
	u32 i;
	for (i = 0; i < count; i++) {
		int ret;
		if (!slices[i])
			continue;
		if (!slices[i]->state) {
			kfree(slices[i]);
			slices[i] = NULL;
			continue;
		}
		ret = mt_pool_slice_free(slices[i]);
		if (WARN_ON(ret))
			continue;
		kfree(slices[i]);
		slices[i] = NULL;
	}
}

static inline void mt_tqx_work_drop_pool_slice_refs(struct mt_tqx_work *work)
{
	/* These are borrowed pointers. The execution context owns the allocations
	 * across cancellation, submission and repeated jobs. */
	memset(work->pool_slices, 0, sizeof(work->pool_slices));
}

static inline int mt_tqx_context_pool_slices_release(struct mt_execution_context *context)
{
	u32 i, present = 0;
	if (!context || !context->process || !context->process->vm)
		return -EINVAL;
	if (context->active_jobs)
		return -EBUSY;
	for (i = 0; i < ARRAY_SIZE(context->tqx_pool_slices); i++) {
		struct mt_pool_slice *slice = context->tqx_pool_slices[i];
		u32 j;
		if (!slice)
			continue;
		present++;
		for (j = 0; j < i; j++)
			if (context->tqx_pool_slices[j] == slice)
				return -EUCLEAN;
		if (!slice->state)
			continue; /* Allocation record exists, but the pool request failed. */
		if (slice->context != context || !slice->bo)
			return -EUCLEAN;
		if (slice->bo->cpu_users || slice->bo->gpu_users)
			return -EBUSY;
	}
	/* These are suballocations of pool BOs which remain mapped for the whole
	 * VM lifetime. Reclaim them on a sealed root after all VM users drain. */
	if (present && context->process->vm->active_uses)
		return -EBUSY;
	mt_pool_slice_array_release(context->tqx_pool_slices,
		ARRAY_SIZE(context->tqx_pool_slices));
	for (i = 0; i < ARRAY_SIZE(context->tqx_pool_slices); i++)
		if (context->tqx_pool_slices[i])
			return -EBUSY;
	return 0;
}

/* Production adapter: allocate shader, PDS-state and texture storage from the
 * original dynamic pools. The fixed PDS code remains the shared resource.
 * Five ordinary BOs are supplied in order: command, source, destination, DMA,
 * and engine state. Their VA fields stay caller-selected and are validated by
 * the normal submission path. */
static inline int mt_tqx_work_prepare_from_pools(struct mt_tqx_work *work,
		struct mt_boot_bo_store *shared, struct mt_tqx_submission_workspace *workspace,
		const struct mt_tqx_upload_ops *io, const struct mt_device_profile *profile,
		u32 cores, struct mt_execution_context *context, struct mt_bo *const ordinary[5],
		const struct mt_tqx_submission_input *input)
{
	struct mt_tqx_submission_input request;
	struct mt_tqx_chunk_plan chunks;
	struct mt_guest_pool_spec specs[MT_GUEST_POOL_COUNT];
	struct mt_bo *bo[MT_TQX_SUBMISSION_BUFFERS];
	struct mt_pool_slice *cached[3];
	u32 state_bytes, texture_bytes, i, present = 0;
	int ret;
	if (!work || work->context || work->job.state != MT_JOB_EMPTY ||
	    work->pool_slices[0] || work->pool_slices[1] || work->pool_slices[2] ||
	    !shared || !shared->pools || !shared->buffers || !context || !context->process ||
	    !context->process->store || !ordinary || !input || !profile || !workspace ||
	    !io || !io->write || !io->read)
		return -EINVAL;
	lockdep_assert_held(shared->buffers->lock);
	if (memcmp(profile, &context->process->store->profile, sizeof(*profile)))
		return -EXDEV;
	if (context->route.type != 1 || context->route.dm != 1 ||
	    context->process->store->profile.family != 2 ||
	    context->process->store->profile.transfer_version != 1 ||
	    profile->family != 2 || profile->transfer_version != 1)
		return -EOPNOTSUPP;
	if (context->process->store->buffers != shared->buffers || !context->process->vm ||
	    !context->process->vm->tables || context->process->vm->tables->store != shared->buffers)
		return -EXDEV;
	if (context->active_jobs || context->process->vm->active_uses)
		return -EBUSY;
	for (i = 0; i < 5; i++)
		if (!ordinary[i] || ordinary[i]->store != shared->buffers)
			return -EXDEV;
		else if (!ordinary[i]->refs || ordinary[i]->cpu_users || ordinary[i]->gpu_users)
			return -EBUSY;
	ret = mt_tqx_copy_chunks_build(&chunks, sizeof(chunks), &input->stream.copy);
	if (ret)
		return ret;
	state_bytes = 16 + chunks.count * 80;
	texture_bytes = chunks.count * MT_TQX_TEXTURE_BYTES;
	if (state_bytes > 4096 || texture_bytes > 4096)
		return -E2BIG;
	for (i = 0; i < ARRAY_SIZE(context->tqx_pool_slices); i++)
		if (context->tqx_pool_slices[i])
			present++;
	if (present && present != ARRAY_SIZE(context->tqx_pool_slices))
		return -EUCLEAN;
	ret = mt_boot_bo_bind(shared, context->process->vm, profile);
	if (ret)
		return ret;
	if (!shared->slots[MT_SHARED_PDS] || !shared->slots[MT_PROCESS_SHARED_COUNT + 1] ||
	    !shared->slots[MT_PROCESS_SHARED_COUNT + 2] ||
	    !shared->slots[MT_PROCESS_SHARED_COUNT + 3])
		return -ENOENT;
	mt_guest_plan_pools(specs);
	if (!present) {
		for (i = 0; i < ARRAY_SIZE(cached); i++) {
			context->tqx_pool_slices[i] = kzalloc(sizeof(*context->tqx_pool_slices[i]),
				GFP_KERNEL);
			if (!context->tqx_pool_slices[i]) {
				ret = -ENOMEM;
				goto fail_cache;
			}
		}
		ret = mt_boot_pool_alloc(shared, context, 6,
			mt_tqx_buffer_bytes[MT_TQX_SHADER], context->tqx_pool_slices[0]);
		if (!ret)
			ret = mt_boot_pool_alloc(shared, context, 4, state_bytes,
				context->tqx_pool_slices[1]);
		if (!ret)
			ret = mt_boot_pool_alloc(shared, context, 3, texture_bytes,
				context->tqx_pool_slices[2]);
		if (ret)
			goto fail_cache;
	}
	cached[0] = context->tqx_pool_slices[0];
	cached[1] = context->tqx_pool_slices[1];
	cached[2] = context->tqx_pool_slices[2];
	if (cached[0]->state != &shared->pools->slices[1] ||
	    cached[1]->state != &shared->pools->slices[0] ||
	    cached[2]->state != &shared->pools->slices[2] ||
	    cached[0]->context != context || cached[1]->context != context ||
	    cached[2]->context != context ||
	    cached[0]->bo != shared->slots[MT_PROCESS_SHARED_COUNT + 1] ||
	    cached[1]->bo != shared->slots[MT_PROCESS_SHARED_COUNT] ||
	    cached[2]->bo != shared->slots[MT_PROCESS_SHARED_COUNT + 2] ||
	    cached[0]->bytes < mt_tqx_buffer_bytes[MT_TQX_SHADER] ||
	    cached[1]->bytes < state_bytes || cached[2]->bytes < texture_bytes) {
		ret = -EXDEV;
		goto fail_cache;
	}
	memcpy(work->pool_slices, cached, sizeof(cached));
	/* VA fields come from context-owned slices, never from the caller. */
	request = *input;
	request.stream.va[1] = cached[0]->va;
	request.stream.va[2] = 0x81ffc00000ULL;
	request.stream.va[3] = cached[1]->va;
	request.stream.va[4] = cached[2]->va;
	bo[MT_TQX_COMMAND] = ordinary[0];
	bo[MT_TQX_SHADER] = cached[0]->bo;
	bo[MT_TQX_PDS_CODE] = shared->slots[MT_SHARED_PDS];
	bo[MT_TQX_PDS_STATE] = cached[1]->bo;
	bo[MT_TQX_TEXTURE] = cached[2]->bo;
	bo[MT_TQX_SOURCE] = ordinary[1];
	bo[MT_TQX_DESTINATION] = ordinary[2];
	bo[MT_TQX_DMA] = ordinary[3];
	bo[MT_TQX_ENGINE_STATE] = ordinary[4];
	ret = mt_tqx_work_prepare(work, workspace, io, profile, cores, context, bo, &request);
	if (!ret)
		return 0;
	mt_tqx_work_drop_pool_slice_refs(work);
	return ret;
fail_cache:
	mt_tqx_context_pool_slices_release(context);
	return ret;
}

/* Complete an already validated/uploaded TQX DMA image under the session
 * lock. Shared by copy and native clear. No root is sealed/published here. */
static inline int mt_tqx_work_hold_submission(struct mt_tqx_work *work,
		const struct mt_tqx_upload_ops *io, struct mt_execution_context *context,
		struct mt_bo *dma_bo, const u8 view[24])
{
	struct mt_execution_request request = {.type = 1};
	struct mt_work_command_inputs command;
	struct mt_gpu_vm *vm;
	bool sealed_root;
	u64 bytes;
	int ret;
	if (!work || work->context || work->job.state != MT_JOB_EMPTY || !context ||
	    !context->process || !context->process->store || !io || !io->write || !io->read ||
	    !dma_bo || !view)
		return -EINVAL;
	if (context->route.type != 1 || context->route.dm != 1 ||
	    context->process->store->profile.family != 2 ||
	    context->process->store->profile.transfer_version != 1)
		return -EOPNOTSUPP;
	vm = context->process->vm;
	if (!vm || !vm->tables || context->process->store->buffers != vm->tables->store)
		return -EXDEV;
	sealed_root = vm->sealed;
	if (sealed_root && !vm->uploaded)
		return -EINVAL;
	if (context->active_jobs == ~(u32)0)
		return -EOVERFLOW;
	if (vm->tables->cpu_users || vm->tables->gpu_users)
		return -EBUSY;
	/* Initial preparation writes and verifies the page tables. Later jobs may
	 * reuse a sealed root only while mappings stay fixed; do not rewrite it. */
	if (!sealed_root) {
		vm->uploaded = false;
		ret = io->write(vm->tables, 0, vm->image, vm->capacity);
		if (!ret)
			ret = io->read(vm->tables, 0, vm->scratch, vm->capacity);
		if (!ret && memcmp(vm->image, vm->scratch, vm->capacity))
			ret = -EIO;
		if (ret)
			return ret;
		vm->uploaded = true;
	}
	memcpy(&request.command_va, view, 8);
	memcpy(&bytes, view + 8, 8);
	if (!bytes || bytes > MT_TQX_DMA_ALLOCATION)
		return -EINVAL;
	request.bytes = bytes;
	ret = mt_execution_context_inputs(&command, context, &request);
	if (ret)
		return ret;
	ret = mt_work_job_prepare(&work->job, vm, dma_bo, &command);
	if (ret)
		return ret;
	/* Pinning happens before releasing the lock: no mutation gap between
	 * uploaded bytes and ownership. Pin the whole VM, including all nine BOs. */
	context->active_jobs++;
	work->context = context;
	return 0;
}

static inline int mt_tqx_work_prepare(struct mt_tqx_work *work,
		struct mt_tqx_submission_workspace *workspace, const struct mt_tqx_upload_ops *io,
		const struct mt_device_profile *profile, u32 cores,
		struct mt_execution_context *context,
		struct mt_bo *const bo[MT_TQX_SUBMISSION_BUFFERS],
		const struct mt_tqx_submission_input *input)
{
	struct mt_tqx_submission_result submitted;
	struct mt_gpu_vm *vm;
	int ret;
	if (!work || work->context || work->job.state != MT_JOB_EMPTY || !context ||
	    !context->process || !context->process->store || !io || !io->write || !io->read)
		return -EINVAL;
	if (context->route.type != 1 || context->route.dm != 1 ||
	    context->process->store->profile.family != 2 ||
	    context->process->store->profile.transfer_version != 1)
		return -EOPNOTSUPP;
	vm = context->process->vm;
	if (!vm || !vm->tables || context->process->store->buffers != vm->tables->store)
		return -EXDEV;
	if (vm->sealed && !vm->uploaded)
		return -EINVAL;
	if (context->active_jobs == ~(u32)0)
		return -EOVERFLOW;
	if (vm->tables->cpu_users || vm->tables->gpu_users)
		return -EBUSY;
	ret = mt_tqx_submission_upload(&submitted, sizeof(submitted), workspace, io,
		profile, cores, vm, bo, input);
	if (ret)
		return ret;
	return mt_tqx_work_hold_submission(work, io, context, bo[MT_TQX_DMA], submitted.view);
}

static inline int mt_tqx_work_cancel(struct mt_tqx_work *work)
{
	int ret;
	if (!work || !work->context)
		return -EINVAL;
	ret = mt_work_job_cancel(&work->job);
	if (ret)
		return ret;
	work->context->active_jobs--;
	mt_tqx_work_drop_pool_slice_refs(work);
	work->context = NULL;
	return 0;
}
#endif
