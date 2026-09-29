/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_WORK_JOB_H
#define MT_GUEST_WORK_JOB_H
#include "mt_work_command.h"

/* All transitions hold the common VM/BO/session lock. The owner embeds this
 * object in pending work; only explicit ownership moves may copy it. Pin the complete
 * VM conservatively because resource references inside command streams have
 * not been decoded. This does not validate command-stream safety. */
enum mt_work_job_state { MT_JOB_EMPTY, MT_JOB_HELD, MT_JOB_PUBLISHED };
struct mt_work_job {
	struct mt_gpu_vm *vm;
	struct mt_bo *bos[MT_BOOT_MAX_RANGES + 1];
	u32 count;
	enum mt_work_job_state state;
	u8 packet[MT_FW_COMMAND_BYTES];
};

/* Move an unpublished owner; the source becomes empty, never duplicated. */
static inline int mt_work_job_move(struct mt_work_job *to, struct mt_work_job *from)
{
	if (!to || !from || to == from || to->state != MT_JOB_EMPTY || from->state != MT_JOB_HELD)
		return -EINVAL;
	*to = *from;
	memset(from, 0, sizeof(*from));
	return 0;
}

static inline int mt_work_job_prepare(struct mt_work_job *job,
		struct mt_gpu_vm *vm, struct mt_bo *command,
		const struct mt_work_command_inputs *request)
{
	struct mt_work_job next = {0};
	u32 i, j;
	int ret;
	if (!job || job->state != MT_JOB_EMPTY)
		return -EINVAL;
	ret = mt_work_command_prepare(next.packet, sizeof(next.packet), vm, command, request);
	if (ret)
		return ret;
	if (!vm->uploaded)
		return -EINVAL;
	if (vm->active_uses)
		return -EBUSY;
	/* Aliases own mapping refs, but require only one exclusive GPU pin. */
	next.bos[next.count++] = vm->tables;
	for (i = 0; i < vm->count; i++) {
		for (j = 0; j < next.count; j++)
			if (next.bos[j] == vm->bindings[i].bo)
				break;
		if (j == next.count)
			next.bos[next.count++] = vm->bindings[i].bo;
	}
	for (i = 0; i < next.count; i++) {
		ret = mt_bo_gpu_begin(next.bos[i]);
		if (ret) {
			while (i)
				mt_bo_gpu_end(next.bos[--i]);
			return ret;
		}
	}
	vm->active_uses++;
	next.vm = vm;
	next.state = MT_JOB_HELD;
	*job = next;
	return 0;
}

/* Internal release: caller must have established non-publication or a
 * matching completion. A completed job does not withdraw the sealed root. */
static inline void mt_work_job_release(struct mt_work_job *job)
{
	while (job->count)
		mt_bo_gpu_end(job->bos[--job->count]);
	job->vm->active_uses--;
	memset(job, 0, sizeof(*job));
}
static inline int mt_work_job_cancel(struct mt_work_job *job)
{
	if (!job || job->state == MT_JOB_EMPTY)
		return -EINVAL;
	if (job->state != MT_JOB_HELD)
		return -EBUSY;
	mt_work_job_release(job);
	return 0;
}
/* Called only after queue publication, with the session lock still held. */
static inline void mt_work_job_published(struct mt_work_job *job)
{
	job->state = MT_JOB_PUBLISHED;
}
/* Only the pending-fence engine may call this after matching DM and wire ID. */
static inline int mt_work_job_complete(struct mt_work_job *job)
{
	if (!job || job->state != MT_JOB_PUBLISHED)
		return -EINVAL;
	mt_work_job_release(job);
	return 0;
}
#endif
