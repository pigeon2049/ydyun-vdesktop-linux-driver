/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_TQX_SUBMISSION_H
#define MT_GUEST_TQX_SUBMISSION_H
#include "mt_tqx_dma.h"

enum mt_tqx_submission_slot {
	MT_TQX_DMA = MT_TQX_BUFFER_COUNT, MT_TQX_ENGINE_STATE, MT_TQX_SUBMISSION_BUFFERS
};
struct mt_tqx_submission_input {
	struct mt_tqx_heap_input stream;
	u64 dma_va, state_va;
};
struct mt_tqx_submission_result { u8 view[24]; };
/* Private heap storage; never place this on a kernel stack. */
struct mt_tqx_submission_workspace {
	struct mt_tqx_upload_workspace upload;
	struct mt_tqx_upload_result records;
	struct mt_tqx_dma_image dma;
};

/* All nine objects must be referenced by the caller under the shared BO/VM
 * lock. Validate the complete set before any write, including DMA/state
 * physical aliases against all seven existing resources. Engine state is
 * context-owned storage and is NEVER overwritten here; a newly created BO's
 * zeroing policy is not a claim about hardware context initialization.
 * Success uploads eleven pages, returns the actual DMA submission view, but
 * does not publish a VM, pin a future job, or submit to firmware. An I/O error
 * leaves the result unchanged but may leave partial BO writes; retry fully.
 */
static inline int mt_tqx_submission_upload(struct mt_tqx_submission_result *out,
		u32 capacity, struct mt_tqx_submission_workspace *w,
		const struct mt_tqx_upload_ops *ops, const struct mt_device_profile *profile,
		u32 cores, struct mt_gpu_vm *vm,
		struct mt_bo *const bo[MT_TQX_SUBMISSION_BUFFERS],
		const struct mt_tqx_submission_input *in)
{
	const struct mt_bo *resources[MT_TQX_BUFFER_COUNT];
	struct mt_tqx_dma_input dma;
	u64 va[MT_TQX_SUBMISSION_BUFFERS], physical[MT_TQX_SUBMISSION_BUFFERS], offset;
	u32 bytes[MT_TQX_SUBMISSION_BUFFERS], i, j;
	int ret;
	if (!out || capacity < sizeof(*out) || !w || !ops || !ops->write || !ops->read ||
	    !vm || !bo || !in)
		return -EINVAL;
	/* Sealed roots remain immutable, but mapped BO contents may be prepared
	 * for another job once all prior GPU uses have completed. */
	if (vm->active_uses)
		return -EBUSY;
	for (i = 0; i < MT_TQX_SUBMISSION_BUFFERS; i++) {
		if (!bo[i] || !bo[i]->refs)
			return -EINVAL;
		if (bo[i]->cpu_users || bo[i]->gpu_users)
			return -EBUSY;
		if (i < MT_TQX_BUFFER_COUNT)
			resources[i] = bo[i];
	}
	ret = mt_tqx_heap_stream_prepare(&w->upload.stream, sizeof(w->upload.stream),
		profile, vm, resources, &in->stream);
	if (ret)
		return ret;
	memcpy(w->records.record, w->upload.stream.record, MT_TQX_RECORD_BYTES);
	memcpy(w->records.page_record, w->upload.stream.page_record, 16);
	memcpy(w->records.root_export, w->upload.stream.root_export, 16);
	dma = (struct mt_tqx_dma_input){in->dma_va, in->state_va, cores};
	ret = mt_tqx_dma_encode(&w->dma, sizeof(w->dma), profile, &w->records, &dma);
	if (ret)
		return ret;
	for (i = 0; i < MT_TQX_SUBMISSION_BUFFERS; i++) {
		if (i < 5) {
			va[i] = in->stream.va[i];
			bytes[i] = mt_tqx_buffer_bytes[i];
		} else if (i < MT_TQX_BUFFER_COUNT) {
			va[i] = i == MT_TQX_SOURCE ? in->stream.copy.src : in->stream.copy.dst;
			bytes[i] = in->stream.copy.bytes;
		} else {
			va[i] = i == MT_TQX_DMA ? in->dma_va : in->state_va;
			bytes[i] = i == MT_TQX_DMA ? MT_TQX_DMA_ALLOCATION : MT_TQX_ENGINE_STATE_ALLOCATION;
		}
		/* Output and context/DMA workspace must permit device writes. CPU
		 * upload success alone cannot establish GPU PTE permissions. */
		ret = mt_ce_copy_resolve_access(vm, bo[i], va[i], bytes[i],
			i == MT_TQX_DESTINATION || i == MT_TQX_DMA || i == MT_TQX_ENGINE_STATE,
			&physical[i]);
		if (ret)
			return ret;
		for (j = 0; j < i; j++)
			if (physical[i] < physical[j] + bytes[j] && physical[j] < physical[i] + bytes[i])
				return -EINVAL;
	}
	ret = mt_tqx_upload(&w->records, sizeof(w->records), &w->upload, ops,
		profile, vm, bo, &in->stream);
	if (ret)
		return ret;
	offset = physical[MT_TQX_DMA] - bo[MT_TQX_DMA]->backing.gpu_pa;
	for (i = 0; i < MT_TQX_DMA_ALLOCATION; i += 4096) {
		ret = ops->write(bo[MT_TQX_DMA], offset + i, w->dma.descriptor + i, 4096);
		if (ret)
			return ret;
		ret = ops->read(bo[MT_TQX_DMA], offset + i, w->upload.read_page, 4096);
		if (ret)
			return ret;
		if (memcmp(w->dma.descriptor + i, w->upload.read_page, 4096))
			return -EIO;
	}
	memcpy(out->view, w->dma.submission_view, sizeof(out->view));
	return 0;
}
#endif
