/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_MARKER_FENCE_H
#define MT_GUEST_MARKER_FENCE_H
#include <linux/dma-fence.h>
#include <linux/jiffies.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include "mt_fw_event_io.h"
#include "mt_work_job.h"
#include "mt_execution_context.h"
#include "mt_tqx_work.h"
#include "mt_ta_submit.h"

/* Pending work owns its VM/BO resources independently of external fence
 * references. Timeout or file closure cannot cancel published ownership. */
struct mt_marker_fence {
	struct dma_fence fence;
	spinlock_t lock;
	struct list_head link;
	u32 wire_id;
	struct mt_work_job job;
	struct mt_execution_context *context;
	struct mt_pool_slice *pool_slices[3];
	/* r366: TA submit params, meaningful iff submitted via submit_ta_work.
	 * Appended; pre-r366 code never touches it. */
	struct mt_ta_submit_params ta_params;
};
/* TA work owner (r366, r364 D1). Mirrors struct mt_tqx_work; params carries
 * the decoded 0x82:0xC IN subset. Caller-owned until the submit op accepts
 * it; never copied. */
struct mt_ta_work {
	struct mt_work_job job;
	struct mt_execution_context *context;
	struct mt_pool_slice *pool_slices[3];
	struct mt_ta_submit_params params;
};
struct mt_marker_store;
struct mt_marker_ops {
	int (*submit_tqx_work)(struct mt_marker_store *, struct mt_tqx_work *, struct dma_fence **);
	int (*submit_context)(struct mt_marker_store *, struct mt_execution_context *,
		struct mt_bo *, const struct mt_execution_request *, struct dma_fence **);
	int (*submit)(struct mt_marker_store *, u32 dm, struct dma_fence **);
	int (*submit_work)(struct mt_marker_store *, u32 dm, struct mt_gpu_vm *,
		struct mt_bo *, const struct mt_work_command_inputs *, struct dma_fence **);
	/* r366: TA submission, 5th op (r364 D1). ABI WARNING: marker stores
	 * initialized by probe builds predating r366 contain a 4-entry table;
	 * dereferencing this member on such a store reads out of bounds. Use
	 * the bridge's exported mt_bridge_submit_ta_work() until the probe
	 * is rebuilt. */
	int (*submit_ta_work)(struct mt_marker_store *, struct mt_ta_work *,
		struct dma_fence **);
};
struct mt_marker_store {
	struct mutex *lock;
	struct mt_fw_queue_io *queue;
	const struct mt_marker_ops *ops;
	struct list_head pending[MT_FW_DM_COUNT];
	u64 context[MT_FW_DM_COUNT], next[MT_FW_DM_COUNT], completed;
	u32 count[MT_FW_DM_COUNT], total;
	int (*can_submit)(void *);
	void *opaque;
	void *buffers; /* Owning BO store for context submission. */
	struct mt_device_profile profile;
	/* Set only by a caller that has verified the live connection and retained
	 * all queue/context resources. No public API sets this in this stage. */
	bool ready;
	/* Independent gate: only RAM selftests enable workloads at this stage.
	 * Live enablement requires context/root association and stream validation. */
	bool work_ready;
};
static const char *mt_marker_driver_name(struct dma_fence *f) { return "mt-vgpu-guest"; }
static const char *mt_marker_timeline_name(struct dma_fence *f) { return "firmware-submit"; }
static void mt_marker_release(struct dma_fence *f)
{
	struct mt_marker_fence *m = container_of(f, struct mt_marker_fence, fence);
	kfree_rcu(m, fence.rcu);
	module_put(THIS_MODULE);
}
static const struct dma_fence_ops mt_marker_fence_ops = {
	.get_driver_name = mt_marker_driver_name, .get_timeline_name = mt_marker_timeline_name,
	.release = mt_marker_release,
};
static int mt_marker_submit_common(struct mt_marker_store *s, u32 dm,
		struct mt_gpu_vm *vm, struct mt_bo *bo,
		const struct mt_work_command_inputs *request, struct dma_fence **out)
{
	struct mt_marker_fence *m;
	u8 command[MT_FW_COMMAND_BYTES];
	int ret;
	lockdep_assert_held(s->lock);
	if (!out || dm < 1 || dm > 3)
		return -EOPNOTSUPP;
	if (!s->ready || !s->can_submit)
		return -EHOSTDOWN;
	if (request && (!s->work_ready || !vm || !vm->sealed))
		return -EOPNOTSUPP;
	if (request) {
		ret = mt_device_profile_work(&s->profile, request->type);
		if (ret)
			return ret;
	}
	ret = s->can_submit(s->opaque);
	if (ret)
		return ret;
	if (s->count[dm] >= 63)
		return -EAGAIN;
	/* Never reuse a 32-bit wire ID in a retained session, including failure
	 * gaps; a delayed completion cannot match a newly allocated fence. */
	if (s->next[dm] > 0xffffffffULL)
		return -EOVERFLOW;
	m = kzalloc(sizeof(*m), GFP_KERNEL);
	if (!m)
		return -ENOMEM;
	__module_get(THIS_MODULE);
	spin_lock_init(&m->lock);
	m->wire_id = s->next[dm]++;
	dma_fence_init(&m->fence, &mt_marker_fence_ops, &m->lock, s->context[dm], m->wire_id);
	if (request) {
		struct mt_work_command_inputs in = *request;
		in.fence = m->wire_id; /* Wire IDs are never caller-selected. */
		ret = mt_work_job_prepare(&m->job, vm, bo, &in);
		if (ret) {
			dma_fence_put(&m->fence);
			return ret;
		}
		memcpy(command, m->job.packet, sizeof(command));
	} else {
		mt_fw_marker_command(command, m->wire_id);
	}
	/* Publish the software owner before any queue head/doorbell write.
	 * The session lock prevents a concurrent completion from racing this. */
	list_add_tail(&m->link, &s->pending[dm]);
	s->count[dm]++;
	s->total++;
	ret = mt_fw_queue_try_submit(s->queue, dm, 0, command);
	if (ret) {
		/* try_submit errors happen before all hardware writes. */
		list_del(&m->link);
		s->count[dm]--;
		s->total--;
		if (request)
			mt_work_job_cancel(&m->job);
		dma_fence_put(&m->fence);
		return ret;
	}
	if (request)
		mt_work_job_published(&m->job);
	*out = dma_fence_get(&m->fence);
	return 0;
}
static int mt_marker_submit(struct mt_marker_store *s, u32 dm, struct dma_fence **out)
{
	return mt_marker_submit_common(s, dm, NULL, NULL, NULL, out);
}
static int mt_marker_submit_work(struct mt_marker_store *s, u32 dm,
		struct mt_gpu_vm *vm, struct mt_bo *bo,
		const struct mt_work_command_inputs *request, struct dma_fence **out)
{
	if (!vm || !bo || !request)
		return -EINVAL;
	return mt_marker_submit_common(s, dm, vm, bo, request, out);
}
static int mt_marker_submit_context(struct mt_marker_store *s,
		struct mt_execution_context *c, struct mt_bo *bo,
		const struct mt_execution_request *request, struct dma_fence **out)
{
	struct mt_work_command_inputs in;
	struct mt_marker_fence *m;
	int ret;
	lockdep_assert_held(s->lock);
	ret = mt_execution_context_inputs(&in, c, request);
	if (ret)
		return ret;
	if (!s->buffers || c->process->store->buffers != s->buffers)
		return -EXDEV;
	if (c->active_jobs == ~(u32)0)
		return -EOVERFLOW;
	ret = mt_marker_submit_work(s, c->route.dm, c->process->vm, bo, &in, out);
	if (ret)
		return ret;
	/* The session lock excludes completion until context ownership is set. */
	m = container_of(*out, struct mt_marker_fence, fence);
	m->context = c;
	c->active_jobs++;
	return 0;
}
static inline int mt_marker_complete(struct mt_marker_store *s, u32 dm,
		const struct mt_fw_event *event)
{
	struct mt_marker_fence *m;
	int ret;
	lockdep_assert_held(s->lock);
	if (dm >= MT_FW_DM_COUNT || !event)
		return -EINVAL;
	if (list_empty(&s->pending[dm]))
		return -ENOENT;
	m = list_first_entry(&s->pending[dm], struct mt_marker_fence, link);
	ret = mt_fw_event_matches(event, s->count[dm], m->wire_id);
	if (ret)
		return ret;
	list_del(&m->link);
	s->count[dm]--;
	s->total--;
	s->completed++;
	/* Release under the session mutex, before signalling wakes consumers. */
	if (m->job.state == MT_JOB_PUBLISHED)
		mt_work_job_complete(&m->job);
	if (m->context) {
		m->context->active_jobs--;
		/* Drop this fence's borrowed references. The execution context keeps
		 * its pool allocations mapped for the next job on the same root. */
		memset(m->pool_slices, 0, sizeof(m->pool_slices));
		m->context = NULL;
	}
	/* Callback code runs with the dma_fence lock; callbacks must not sleep
	 * or acquire the session mutex. The fence owns its own lock/lifetime. */
	dma_fence_signal(&m->fence);
	dma_fence_put(&m->fence);
	return 0;
}
/* Consume a prepared TQX owner without dropping any pins. All rejection and
 * queue-full paths preserve the prepared owner for retry/cancellation. */
static int mt_marker_submit_tqx_work(struct mt_marker_store *s,
		struct mt_tqx_work *work, struct dma_fence **out)
{
	struct mt_marker_fence *m;
	struct mt_execution_context *c;
	struct mt_gpu_vm *vm;
	u32 dm = 1;
	int ret;
	if (!s || !work || !out || !work->context || work->job.state != MT_JOB_HELD)
		return -EINVAL;
	lockdep_assert_held(s->lock);
	c = work->context;
	if (!c->process || !c->process->store || c->route.type != 1 || c->route.dm != dm ||
	    s->profile.family != 2 || s->profile.transfer_version != 1)
		return -EOPNOTSUPP;
	vm = c->process->vm;
	if (!s->buffers || c->process->store->buffers != s->buffers || work->job.vm != vm)
		return -EXDEV;
	if (!s->ready || !s->can_submit)
		return -EHOSTDOWN;
	if (!s->work_ready || !vm->sealed || !vm->uploaded)
		return -EOPNOTSUPP;
	ret = s->can_submit(s->opaque);
	if (ret)
		return ret;
	if (s->count[dm] >= 63)
		return -EAGAIN;
	if (s->next[dm] > 0xffffffffULL)
		return -EOVERFLOW;
	m = kzalloc(sizeof(*m), GFP_KERNEL);
	if (!m)
		return -ENOMEM;
	__module_get(THIS_MODULE);
	spin_lock_init(&m->lock);
	m->wire_id = s->next[dm]++;
	dma_fence_init(&m->fence, &mt_marker_fence_ops, &m->lock, s->context[dm], m->wire_id);
	ret = mt_work_job_move(&m->job, &work->job);
	if (ret) {
		dma_fence_put(&m->fence);
		return ret;
	}
	memcpy(m->pool_slices, work->pool_slices, sizeof(m->pool_slices));
	memset(work->pool_slices, 0, sizeof(work->pool_slices));
	m->context = c; /* active_jobs already belongs to this prepared owner. */
	mt_fw_put32(m->job.packet, 0x48, m->wire_id);
	list_add_tail(&m->link, &s->pending[dm]);
	s->count[dm]++;
	s->total++;
	ret = mt_fw_queue_try_submit(s->queue, dm, 0, m->job.packet);
	if (ret) {
		list_del(&m->link);
		s->count[dm]--;
		s->total--;
		/* Queue errors precede hardware writes; restore exclusive ownership. */
		mt_fw_put32(m->job.packet, 0x48, 0);
		mt_work_job_move(&work->job, &m->job);
		memcpy(work->pool_slices, m->pool_slices, sizeof(work->pool_slices));
		memset(m->pool_slices, 0, sizeof(m->pool_slices));
		m->context = NULL;
		dma_fence_put(&m->fence);
		return ret;
	}
	mt_work_job_published(&m->job);
	work->context = NULL;
	*out = dma_fence_get(&m->fence);
	return 0;
}

/* ---- TA submission (r366, R2b phase 1) ---- */

/* TA marker packet (r365 proven on live hardware): opcode MT_FW_TA_OPCODE at
 * +0x0c, wire_id at +0x48, pid at +0x4c; rest zero. Marker-level only. */
static inline void mt_fw_ta_marker_command(void *command, u32 fence, u32 pid)
{
	memset(command, 0, MT_FW_COMMAND_BYTES);
	mt_fw_put32(command, 0x0c, MT_FW_TA_OPCODE);
	mt_fw_put32(command, 0x48, fence);
	mt_fw_put32(command, 0x4c, pid);
}

/* TA completion matching (r365): 0x66-class commands complete with
 * words[1]==MT_FW_TA_COMPLETE_CODE, not the standard COMPLETE (0). */
static inline int mt_fw_event_matches_ta(const struct mt_fw_event *e,
					 u32 pending, u32 oldest)
{
	if (!e)
		return -EINVAL;
	if (e->words[1] != MT_FW_TA_COMPLETE_CODE &&
	    mt_fw_event_kind(e) != MT_EVENT_COMPLETE)
		return -EOPNOTSUPP;
	if (!pending)
		return -ENOENT;
	return e->words[2] == oldest ? 0 : -ESTALE;
}

/* TA sync-prim writeback (r364 D6). Marker-level (r366): no sync-prim
 * infrastructure (R5); submit rejects any requested update, so this only
 * ever sees empty counts. The check is real for R2b. */
static inline int mt_ta_sync_update_apply(const struct mt_ta_submit_params *p)
{
	if (!p)
		return -EINVAL;
	if (p->ta_upd_count == 0 && p->ta_fence_count == 0)
		return 0;
	/* No resolvable sync-prim targets at marker level; fail honestly
	 * rather than fake writes. R2b/R5 will resolve (block, offset). */
	return -EOPNOTSUPP;
}

/* Resolve a check_fence wire_id to a referenced dma_fence. 0 means no
 * dependency. wire_ids allocated but no longer pending are treated as
 * satisfied (their fence completed). Unknown ids are -EINVAL.
 * Takes s->lock briefly; caller must NOT hold it (the wait below would
 * wedge against completion, which needs s->lock). */
static int mt_marker_ta_lookup_fence(struct mt_marker_store *s, s32 check_fence,
				     struct dma_fence **out)
{
	u32 dm;
	struct mt_marker_fence *m;
	bool allocated = false;
	if (!s || !out)
		return -EINVAL;
	*out = NULL;
	if (check_fence == 0)
		return 0;
	if (check_fence < 0)
		return -EINVAL;
	mutex_lock(s->lock);
	for (dm = 0; dm < MT_FW_DM_COUNT; dm++) {
		if ((u64)check_fence < s->next[dm])
			allocated = true;
	}
	if (allocated) {
		for (dm = 0; dm < MT_FW_DM_COUNT && !*out; dm++) {
			list_for_each_entry(m, &s->pending[dm], link) {
				if (m->wire_id == (u32)check_fence) {
					*out = dma_fence_get(&m->fence);
					break;
				}
			}
		}
	}
	mutex_unlock(s->lock);
	if (*out)
		return 0;
	return allocated ? 0 : -EINVAL;
}

/* Submit TA work as an independent marker op (r364 D1, r366 R2b phase 1).
 *
 * Lock contract: caller must NOT hold s->lock. The check_fence input wait
 * runs without the session lock (completions need it); the submit phase
 * takes it internally. This differs from submit_tqx_work (lock held).
 *
 * Marker-level (r366, r364 D8): payload is a 0x66 marker only; any real TA
 * payload (updates/fences/PR) is rejected with -EOPNOTSUPP until R2b/R5.
 */
static int mt_marker_submit_ta_work(struct mt_marker_store *s,
				    struct mt_ta_work *work,
				    struct dma_fence **out)
{
	struct mt_marker_fence *m;
	struct mt_execution_context *c;
	struct dma_fence *dep = NULL;
	const u32 dm = MT_FW_DM_TA;
	long waited;
	int ret;

	/* D7: validate everything before any hardware write. */
	if (!s || !work || !out)
		return -EINVAL;
	if (work->job.state != MT_JOB_HELD)
		return -EINVAL;
	c = work->context;
	if (!c || c->route.dm != dm)
		return -EOPNOTSUPP;
	if (s->profile.family != 2)
		return -EOPNOTSUPP;
	/* D5: kick_pr semantics are TO-VALIDATE (r364 V3); do not fabricate. */
	if (work->params.kick_flags & MT_TA_KICK_PR)
		return -EOPNOTSUPP;
	/* D8: R5 gate — marker level only. */
	if (work->params.ta_upd_count > 0 || work->params.ta_fence_count > 0)
		return -EOPNOTSUPP;
	if (!s->ready || !s->can_submit)
		return -EHOSTDOWN;
	ret = s->can_submit(s->opaque);
	if (ret)
		return ret;

	/* D6: input dependency wait, without s->lock (see contract above). */
	ret = mt_marker_ta_lookup_fence(s, work->params.check_fence, &dep);
	if (ret)
		return ret;
	if (dep) {
		waited = dma_fence_wait_timeout(dep, false, msecs_to_jiffies(5000));
		dma_fence_put(dep);
		if (waited < 0)
			return (int)waited;
		if (waited == 0)
			return -ETIMEDOUT;
	}

	mutex_lock(s->lock);
	if (s->count[dm] >= 63) {
		ret = -EAGAIN;
		goto out_unlock;
	}
	/* Never reuse a 32-bit wire ID, including failure gaps (cf. TQX). */
	if (s->next[dm] > 0xffffffffULL) {
		ret = -EOVERFLOW;
		goto out_unlock;
	}
	m = kzalloc(sizeof(*m), GFP_KERNEL);
	if (!m) {
		ret = -ENOMEM;
		goto out_unlock;
	}
	__module_get(THIS_MODULE);
	spin_lock_init(&m->lock);
	m->wire_id = s->next[dm]++;
	dma_fence_init(&m->fence, &mt_marker_fence_ops, &m->lock,
		       s->context[dm], m->wire_id);
	/* D2/D4: DM3, opcode 0x66 marker; wire_id at +0x48 (r365 proven). */
	mt_fw_ta_marker_command(m->job.packet, m->wire_id, (u32)current->pid);
	m->ta_params = work->params;
	/* Marker-level: no context ownership (R2b binds the real context). */
	list_add_tail(&m->link, &s->pending[dm]);
	s->count[dm]++;
	s->total++;
	ret = mt_fw_queue_try_submit(s->queue, dm, 0, m->job.packet);
	if (ret) {
		/* Queue errors precede hardware writes; unwind fully. */
		list_del(&m->link);
		s->count[dm]--;
		s->total--;
		/* wire_id is never reused. */
		dma_fence_put(&m->fence);
		goto out_unlock;
	}
	/* Success: consume the caller's prepared work. */
	work->job.state = MT_JOB_EMPTY;
	work->context = NULL;
	memset(work->pool_slices, 0, sizeof(work->pool_slices));
	*out = dma_fence_get(&m->fence);
	ret = 0;
out_unlock:
	mutex_unlock(s->lock);
	return ret;
}

/* TA-aware completion (r366). Accepts the 0x66-class completion code
 * (MT_FW_TA_COMPLETE_CODE, r365) in addition to standard COMPLETE.
 * Caller must hold s->lock. The frozen probe's mt_marker_complete()
 * rejects 0x100, so TA fences complete through this path only. */
static inline int mt_marker_complete_ta(struct mt_marker_store *s, u32 dm,
					const struct mt_fw_event *event)
{
	struct mt_marker_fence *m;
	int ret;
	if (!s || !event || dm != MT_FW_DM_TA)
		return -EINVAL;
	lockdep_assert_held(s->lock);
	if (list_empty(&s->pending[dm]))
		return -ENOENT;
	m = list_first_entry(&s->pending[dm], struct mt_marker_fence, link);
	ret = mt_fw_event_matches_ta(event, s->count[dm], m->wire_id);
	if (ret)
		return ret;
	ret = mt_ta_sync_update_apply(&m->ta_params);
	if (ret)
		return ret;
	list_del(&m->link);
	s->count[dm]--;
	s->total--;
	s->completed++;
	/* Marker-level: job never published, no context bound. */
	if (m->job.state == MT_JOB_PUBLISHED)
		mt_work_job_complete(&m->job);
	if (m->context) {
		m->context->active_jobs--;
		memset(m->pool_slices, 0, sizeof(m->pool_slices));
		m->context = NULL;
	}
	dma_fence_signal(&m->fence);
	dma_fence_put(&m->fence);
	return 0;
}

static const struct mt_marker_ops mt_marker_operations = {
	.submit_tqx_work = mt_marker_submit_tqx_work,
	.submit = mt_marker_submit, .submit_work = mt_marker_submit_work,
	.submit_context = mt_marker_submit_context,
	.submit_ta_work = mt_marker_submit_ta_work,
};
/* Bridge-exported TA submit entry (r366). Implemented in mt_pvr_bridge.c;
 * runs in the bridge's context so pending TA fences pin the bridge module,
 * never a short-lived verifier. See the ABI WARNING on struct mt_marker_ops. */
int mt_bridge_submit_ta_work(struct mt_marker_store *s, struct mt_ta_work *work,
			     struct dma_fence **out);

static inline void mt_marker_store_init(struct mt_marker_store *s, struct mutex *lock,
		struct mt_fw_queue_io *queue, const struct mt_device_profile *profile)
{
	u32 dm;
	u64 context = dma_fence_context_alloc(MT_FW_DM_COUNT);
	memset(s, 0, sizeof(*s));
	s->lock = lock;
	s->queue = queue;
	s->ops = &mt_marker_operations;
	if (profile)
		s->profile = *profile;
	for (dm = 0; dm < MT_FW_DM_COUNT; dm++) {
		INIT_LIST_HEAD(&s->pending[dm]);
		s->context[dm] = context + dm;
		s->next[dm] = 1;
	}
}
#endif
