// SPDX-License-Identifier: GPL-2.0
/* Unified 2D Copy, Native Clear & 3D Universal DRM Front End
 * Integrates TQX 2D transfer/fill with DM2 Universal 3D execution context.
 * Exposes standard DRM GEM handles, binary syncobj, sync_file export,
 * and DRM_IOCTL_MT_SUBMIT_3D for userspace graphics workloads.
 */
#include "../mt_guest_device.h"
#include "../mt_addr_plan.h"
#include "../mt_tqx_fill_work.h"
#include "../mt_gfx_context.h"
#include "../mt_gfx_context_data.h"
#include "../mt_gfx_packet.h"
#include "../mt_gfx_packet_template.h"
#include <linux/capability.h>
#include <linux/dma-resv.h>
#include <linux/ktime.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_syncobj.h>
#include "../../include/mt_drm_uapi.h"

static bool enable = true;
module_param(enable, bool, 0400);

static struct pci_dev *device;
static struct module *owner;
static struct mt_guest_device *d;
static struct drm_device *drm;
static struct mt_vm_vram *space_2d;
static struct mt_vm_vram *space_3d;
static struct mt_execution_process process_2d;
static struct mt_execution_process process_3d;
static struct mt_execution_context context_2d;
static struct mt_execution_context context_3d;
static struct mt_bo command_2d, dma_2d, state_2d;
static struct mt_tqx_work work_2d;
static struct mt_tqx_submission_workspace *workspace;
static struct mt_tqx_fill_workspace *fill_workspace;

/* 3D context BOs & Command buffer */
static struct mt_bo context_bos[MT_GFX_CONTEXT_BO_COUNT];
static u64 context_vas[MT_GFX_CONTEXT_BO_COUNT];
static struct mt_bo command_3d;
static u64 command_3d_va = MT_TRANSLATE_CMD_VA;
static u8 csw_3d[MT_GFX_CONTEXT_CSW_BYTES];
static bool ready_3d;

static DEFINE_MUTEX(submit_lock);
static DEFINE_MUTEX(slot_lock);
static bool ready, retained, faulted;
static u64 submitted, completed, last_sequence;
static u32 cores, leased;

struct slot { struct mt_bo bo; u64 va; u64 va_3d; bool leased; };
static struct slot slots[MT_DRM_SLOT_COUNT];

#ifdef MT_LIVE_LARGE_SURFACES
#define MT_LIVE_SLOT_MAX (8U * 1024U * 1024U)
static u32 slot_bytes(u32 i)
{
	return i == 0 ? MT_LIVE_SLOT_MAX : i == 1 ? 4U * 1024U * 1024U : MT_DRM_SLOT_BYTES;
}
static u64 slot_va(u32 i) { return 0x41000000ULL + i * 0x1000000ULL; }
#else
#define MT_LIVE_SLOT_MAX MT_DRM_SLOT_BYTES
static u32 slot_bytes(u32 i) { return MT_DRM_SLOT_BYTES; }
static u64 slot_va(u32 i) { return MT_TQX_STREAM_SRC_VA + i * MT_CTX_BO_STRIDE; }
#endif

struct lease { struct drm_gem_object base; struct slot *slot; };
static_assert(sizeof(struct mt_guest_device) == 30784);
static_assert(offsetof(struct mt_guest_device, markers) == 29888);
static_assert(sizeof(struct drm_mt_query) == 80);
static_assert(sizeof(struct drm_mt_create) == 16);
static_assert(sizeof(struct drm_mt_rw) == 4120);
static_assert(sizeof(struct drm_mt_copy) == 48);
static_assert(sizeof(struct drm_mt_fill) == 56);
static_assert(sizeof(struct drm_mt_submit_3d) == 32);


static int transfer(struct mt_bo *bo, u64 off, void *data, u64 bytes, bool write)
{
	struct mt_bo_vram_handle *handle;
	void *mapping;
	int ret;
	if (bo->store != &d->buffers || bo->ops != d->buffers.ops)
		return -EXDEV;
	lockdep_assert_held(&d->state.trial_lock);
	ret = mt_bo_check_range(bo, off, bytes);
	if (ret)
		return ret;
	ret = mt_bo_cpu_begin(bo, &mapping);
	if (ret)
		return ret;
	handle = bo->backing.handle;
	if (handle->system) {
		if (write)
			memcpy((u8 *)mapping + off, data, bytes);
		else
			memcpy(data, (u8 *)mapping + off, bytes);
	} else if (write) {
		memcpy_toio((void __iomem *)mapping + off, data, bytes);
	} else {
		memcpy_fromio(data, (void __iomem *)mapping + off, bytes);
	}
	return mt_bo_cpu_end(bo);
}

static int write_bo(struct mt_bo *bo, u64 off, const void *p, u64 bytes)
{
	return transfer(bo, off, (void *)p, bytes, true);
}
static int read_bo(struct mt_bo *bo, u64 off, void *p, u64 bytes)
{
	return transfer(bo, off, p, bytes, false);
}
static const struct mt_tqx_upload_ops upload = { .write = write_bo, .read = read_bo };

static int idle(void)
{
	if (!d->runtime.published || d->runtime.event_result ||
	    !d->state.trial.pinned || !d->state.trial.connected ||
	    !d->service.running || readl(d->state.regs + 0x890) != 2 ||
	    mt_trial_fw_state(&d->state.trial) != 2 || !mt_trial_started(&d->state.trial))
		return -EHOSTDOWN;
	return 0;
}

static int allowed(void)
{
	return capable(CAP_SYS_ADMIN) ? 0 : -EPERM;
}

static void lease_free(struct drm_gem_object *obj)
{
	struct lease *l = container_of(obj, struct lease, base);
	struct drm_device *dev = obj->dev;
	mutex_lock(&slot_lock);
	mutex_lock(&d->state.trial_lock);
	if (l->slot) {
		l->slot->leased = false;
		leased--;
		WARN_ON(mt_bo_put(&l->slot->bo));
	}
	mutex_unlock(&d->state.trial_lock);
	mutex_unlock(&slot_lock);
	/* Custom GEM free must run the core release itself (mirrors
	 * mt_live_drm.c): without dma_resv_fini the last submit fence in the
	 * reservation stays referenced (+1 probe ref per submitting file,
	 * r140 Δ1), and the create-time drm_dev_get leaks with it. */
	drm_gem_object_release(obj);
	kfree(l);
	drm_dev_put(dev);
}

static struct dma_buf *lease_export(struct drm_gem_object *obj, int flags)
{
	return ERR_PTR(-EOPNOTSUPP);
}
static int lease_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	return -EOPNOTSUPP;
}
static const struct drm_gem_object_funcs lease_funcs = {
	.free = lease_free, .export = lease_export, .mmap = lease_mmap,
};

static struct drm_gem_object *deny_import(struct drm_device *dev, struct dma_buf *buf)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static struct drm_gem_object *lookup(struct drm_file *file, u32 handle)
{
	struct drm_gem_object *obj = drm_gem_object_lookup(file, handle);
	if (obj && (obj->dev != drm || obj->funcs != &lease_funcs)) {
		drm_gem_object_put(obj);
		return NULL;
	}
	return obj;
}

static int query_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_mt_query *q = data;
	int ret = allowed();
	if (ret)
		return ret;
	mutex_lock(&slot_lock);
	mutex_lock(&d->state.trial_lock);
	*q = (struct drm_mt_query){.abi = MT_DRM_ABI, .slot_count = MT_DRM_SLOT_COUNT,
		.slot_bytes = MT_LIVE_SLOT_MAX, .leased = leased, .faulted = faulted,
		.retained = retained, .submitted = submitted, .completed = completed,
		.capabilities = MT_DRM_CAP_COPY | MT_DRM_CAP_FILL | (ready_3d ? MT_DRM_CAP_3D : 0),
		.last_sequence = last_sequence,
		.vm2d_mappings = space_2d ? space_2d->vm.count : 0,
		.vm3d_mappings = space_3d ? space_3d->vm.count : 0,
		.vm3d_max_mappings = space_3d ? space_3d->vm.max_ranges : 0};
	mutex_unlock(&d->state.trial_lock);
	mutex_unlock(&slot_lock);
	return 0;
}

static int create_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_mt_create *r = data;
	struct lease *l;
	u32 i, best = MT_DRM_SLOT_COUNT;
	int ret = allowed();
	if (ret)
		return ret;
	if (!r->bytes || r->bytes > MT_LIVE_SLOT_MAX || r->flags || r->handle || r->reserved)
		return -EINVAL;
	l = kzalloc(sizeof(*l), GFP_KERNEL);
	if (!l)
		return -ENOMEM;
	mutex_lock(&slot_lock);
	mutex_lock(&d->state.trial_lock);
	ret = faulted ? -EIO : idle();
	if (ret)
		goto unlock;
	for (i = 0; i < MT_DRM_SLOT_COUNT; i++)
		if (!slots[i].leased && !slots[i].bo.gpu_users && !slots[i].bo.cpu_users &&
		    slot_bytes(i) >= r->bytes &&
		    (best == MT_DRM_SLOT_COUNT || slot_bytes(i) < slot_bytes(best)))
			best = i;
	if (best == MT_DRM_SLOT_COUNT) {
		ret = -ENOSPC;
		goto unlock;
	}
	i = best;
	ret = slots[i].bo.ops->clear(slots[i].bo.store, &slots[i].bo.backing);
	if (!ret)
		ret = mt_bo_get(&slots[i].bo);
	if (!ret) {
		slots[i].leased = true;
		leased++;
		l->slot = &slots[i];
	}
unlock:
	mutex_unlock(&d->state.trial_lock);
	mutex_unlock(&slot_lock);
	if (ret) {
		kfree(l);
		return ret;
	}
	drm_dev_get(dev);
	r->bytes = PAGE_ALIGN(r->bytes);
	drm_gem_private_object_init(dev, &l->base, r->bytes);
	l->base.funcs = &lease_funcs;
	ret = drm_gem_handle_create(file, &l->base, &r->handle);
	drm_gem_object_put(&l->base);
	return ret;
}

static int rw_ioctl(void *data, struct drm_file *file, bool write)
{
	struct drm_mt_rw *r = data;
	struct drm_gem_object *obj;
	struct lease *l;
	int ret = allowed();
	if (ret)
		return ret;
	if (r->flags || r->reserved || !r->bytes || r->bytes > sizeof(r->data))
		return -EINVAL;
	obj = lookup(file, r->handle);
	if (!obj)
		return -ENOENT;
	if (r->offset > obj->size || r->bytes > obj->size - r->offset) {
		ret = -EINVAL;
		goto put;
	}
	l = container_of(obj, struct lease, base);
	mutex_lock(&slot_lock);
	mutex_lock(&d->state.trial_lock);
	ret = transfer(&l->slot->bo, r->offset, r->data, r->bytes, write);
	mutex_unlock(&d->state.trial_lock);
	mutex_unlock(&slot_lock);
put:
	drm_gem_object_put(obj);
	return ret;
}

static int read_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	return rw_ioctl(data, file, false);
}
static int write_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	return rw_ioctl(data, file, true);
}

static int copy_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_mt_copy *r = data;
	struct drm_gem_object *objects[2];
	struct lease *src, *dst;
	struct drm_syncobj *sync = NULL;
	struct dma_fence *fence = NULL;
	struct ww_acquire_ctx acquire;
	struct mt_tqx_submission_input input;
	struct mt_bo *bos[5];
	long waited;
	bool published = false;
	int ret = allowed();
	if (ret)
		return ret;
	if (r->flags || r->reserved || r->sequence || !r->bytes || r->bytes > MT_LIVE_SLOT_MAX)
		return -EINVAL;
	if (r->source == r->destination)
		return -EINVAL;
	objects[0] = lookup(file, r->source);
	objects[1] = lookup(file, r->destination);
	if (!objects[0] || !objects[1]) {
		ret = -ENOENT;
		goto put;
	}
	if (r->source_offset > objects[0]->size || r->bytes > objects[0]->size - r->source_offset ||
	    r->destination_offset > objects[1]->size || r->bytes > objects[1]->size - r->destination_offset) {
		ret = -EINVAL;
		goto put;
	}
	if (r->out_syncobj) {
		sync = drm_syncobj_find(file, r->out_syncobj);
		if (!sync) {
			ret = -ENOENT;
			goto put;
		}
	}
	src = container_of(objects[0], struct lease, base);
	dst = container_of(objects[1], struct lease, base);
	ret = drm_gem_lock_reservations(objects, 2, &acquire);
	if (ret)
		goto put;
	ret = dma_resv_reserve_fences(objects[0]->resv, 1);
	if (!ret)
		ret = dma_resv_reserve_fences(objects[1]->resv, 1);
	if (ret) {
		drm_gem_unlock_reservations(objects, 2, &acquire);
		goto put;
	}
	mutex_lock(&submit_lock);
	mutex_lock(&d->state.trial_lock);
	ret = faulted ? -EIO : idle();
	if (ret)
		goto unlock_session;
	input = (struct mt_tqx_submission_input){
		.stream = {.copy = {src->slot->va + r->source_offset,
				    dst->slot->va + r->destination_offset, r->bytes},
			   .va = {MT_TQX_CMD_VA}},
		.dma_va = MT_TQX_DMA_VA, .state_va = MT_TQX_STATE_VA};
	bos[0] = &command_2d; bos[1] = &src->slot->bo; bos[2] = &dst->slot->bo;
	bos[3] = &dma_2d; bos[4] = &state_2d;
	ret = mt_tqx_work_prepare_from_pools(&work_2d, &d->shared_boot, workspace,
		&upload, &d->gem.profile, cores, &context_2d, bos, &input);
	if (ret)
		goto unlock_session;
	d->markers.ready = true;
	d->markers.work_ready = true;
	ret = d->markers.ops->submit_tqx_work(&d->markers, &work_2d, &fence);
	d->markers.work_ready = false;
	d->markers.ready = false;
	if (ret)
		goto cancel;
	published = true;
	submitted++;
	last_sequence = fence->seqno;
	dma_resv_add_fence(objects[0]->resv, fence, DMA_RESV_USAGE_READ);
	dma_resv_add_fence(objects[1]->resv, fence, DMA_RESV_USAGE_WRITE);
	if (sync)
		drm_syncobj_replace_fence(sync, fence);
	goto unlock_session;
cancel:
	if (work_2d.context)
		WARN_ON(mt_tqx_work_cancel(&work_2d));
unlock_session:
	mutex_unlock(&d->state.trial_lock);
	drm_gem_unlock_reservations(objects, 2, &acquire);
	if (published) {
		waited = dma_fence_wait_timeout(fence, false, msecs_to_jiffies(5000));
		ret = waited > 0 ? dma_fence_get_status(fence) :
			(waited < 0 ? (int)waited : -ETIMEDOUT);
		if (ret == 1) {
			ret = 0;
			completed++;
			r->sequence = fence->seqno;
		} else {
			if (!ret) ret = -EIO;
			faulted = true;
		}
	}
	mutex_unlock(&submit_lock);
put:
	if (fence) dma_fence_put(fence);
	if (sync) drm_syncobj_put(sync);
	if (objects[0]) drm_gem_object_put(objects[0]);
	if (objects[1]) drm_gem_object_put(objects[1]);
	return ret;
}

static int fill_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_mt_fill *r = data;
	struct drm_gem_object *objects[1];
	struct lease *dst;
	struct drm_syncobj *sync = NULL;
	struct dma_fence *fence = NULL;
	struct ww_acquire_ctx acquire;
	struct mt_tqx_fill_input input;
	struct mt_bo *bos[4];
	long waited;
	bool published = false;
	int ret = allowed();
	if (ret)
		return ret;
	if (r->flags || r->sequence || !r->width || !r->height || !r->rect_width || !r->rect_height ||
	    r->x >= r->width || r->y >= r->height ||
	    r->rect_width > r->width - r->x || r->rect_height > r->height - r->y ||
	    (r->offset & 3))
		return -EINVAL;
	if ((u64)r->width * (u64)r->height > (MT_LIVE_SLOT_MAX - r->offset) / 4ULL)
		return -EINVAL;
	objects[0] = lookup(file, r->destination);
	if (!objects[0])
		return -ENOENT;
	if (r->offset > objects[0]->size ||
	    (u64)r->width * (u64)r->height * 4ULL > objects[0]->size - r->offset) {
		ret = -EINVAL;
		goto put;
	}
	if (r->out_syncobj) {
		sync = drm_syncobj_find(file, r->out_syncobj);
		if (!sync) {
			ret = -ENOENT;
			goto put;
		}
	}
	dst = container_of(objects[0], struct lease, base);
	ret = drm_gem_lock_reservations(objects, 1, &acquire);
	if (ret)
		goto put;
	ret = dma_resv_reserve_fences(objects[0]->resv, 1);
	if (ret) {
		drm_gem_unlock_reservations(objects, 1, &acquire);
		goto put;
	}
	mutex_lock(&submit_lock);
	mutex_lock(&d->state.trial_lock);
	ret = faulted ? -EIO : idle();
	if (ret)
		goto unlock_session;
	bos[0] = &command_2d; bos[1] = &dst->slot->bo; bos[2] = &dma_2d; bos[3] = &state_2d;
	input = (struct mt_tqx_fill_input){
		.destination_va = dst->slot->va + r->offset, .command_va = MT_TQX_CMD_VA,
		.element_bytes = 4, .width = r->width, .height = r->height,
		.x = r->x, .y = r->y, .rect_width = r->rect_width, .rect_height = r->rect_height,
		.color = {r->color, 0, 0, 0}};
	ret = mt_tqx_fill_work_prepare(&work_2d, fill_workspace, &upload, &d->shared_boot,
		&d->gem.profile, cores, &context_2d, bos, &input, MT_TQX_DMA_VA, MT_TQX_STATE_VA);
	if (ret)
		goto unlock_session;
	d->markers.ready = true;
	d->markers.work_ready = true;
	ret = d->markers.ops->submit_tqx_work(&d->markers, &work_2d, &fence);
	d->markers.work_ready = false;
	d->markers.ready = false;
	if (ret)
		goto cancel;
	published = true;
	submitted++;
	last_sequence = fence->seqno;
	dma_resv_add_fence(objects[0]->resv, fence, DMA_RESV_USAGE_WRITE);
	if (sync)
		drm_syncobj_replace_fence(sync, fence);
	goto unlock_session;
cancel:
	if (work_2d.context)
		WARN_ON(mt_tqx_work_cancel(&work_2d));
unlock_session:
	mutex_unlock(&d->state.trial_lock);
	drm_gem_unlock_reservations(objects, 1, &acquire);
	if (published) {
		waited = dma_fence_wait_timeout(fence, false, msecs_to_jiffies(5000));
		ret = waited > 0 ? dma_fence_get_status(fence) :
			(waited < 0 ? (int)waited : -ETIMEDOUT);
		if (ret == 1) {
			ret = 0;
			completed++;
			r->sequence = fence->seqno;
		} else {
			if (!ret) ret = -EIO;
			faulted = true;
		}
	}
	mutex_unlock(&submit_lock);
put:
	if (fence) dma_fence_put(fence);
	if (sync) drm_syncobj_put(sync);
	if (objects[0]) drm_gem_object_put(objects[0]);
	return ret;
}

/* 3D Workload Execution on DM2 via DRM ioctl */
static int submit_3d_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_mt_submit_3d *r = data;
	struct drm_syncobj *sync = NULL;
	struct dma_fence *fence = NULL;
	struct drm_gem_object *target_obj = NULL;
	struct lease *target_lease = NULL;
	struct ww_acquire_ctx acquire;
	struct mt_execution_request req;
	ktime_t t_start, t_end;
	long waited;
	bool locked_resv = false;
	int ret = allowed();
	if (ret)
		return ret;
	if (r->flags || r->sequence)
		return -EINVAL;
	if (!ready_3d)
		return -ENODEV;

	if (r->target_handle) {
		target_obj = lookup(file, r->target_handle);
		if (!target_obj)
			return -ENOENT;
		target_lease = container_of(target_obj, struct lease, base);
		if (!target_lease->slot || !target_lease->slot->va_3d) {
			ret = -EINVAL;
			goto put_target;
		}
		ret = drm_gem_lock_reservations(&target_obj, 1, &acquire);
		if (ret)
			goto put_target;
		ret = dma_resv_reserve_fences(target_obj->resv, 1);
		if (ret) {
			drm_gem_unlock_reservations(&target_obj, 1, &acquire);
			goto put_target;
		}
		locked_resv = true;
	}

	if (r->out_syncobj) {
		sync = drm_syncobj_find(file, r->out_syncobj);
		if (!sync) {
			ret = -ENOENT;
			goto unlock_resv;
		}
	}

	mutex_lock(&submit_lock);
	mutex_lock(&d->state.trial_lock);
	ret = faulted ? -EIO : idle();
	if (ret) {
		mutex_unlock(&d->state.trial_lock);
		mutex_unlock(&submit_lock);
		goto put_sync;
	}

	/* Dynamic variation: write frame_tag into Envelope header +0x08 */
	if (r->frame_tag) {
		u64 tag = r->frame_tag;
		write_bo(&command_3d, 0x08, &tag, 8);
	}

	/* Dynamic Render Target binding if target GEM handle provided */
	if (target_lease) {
		u64 rt_va = target_lease->slot->va_3d;
		u64 rt_stride = 128ULL * 4;
		u64 rt_extent = (128ULL << 16) | 128ULL;
		write_bo(&command_3d, 0x45a0, &rt_va, 8);
		write_bo(&command_3d, 0x45a8, &rt_stride, 8);
		write_bo(&command_3d, 0x45b0, &rt_extent, 8);
		write_bo(&command_3d, 0x4668, &rt_va, 8);
	}

	req.command_va = command_3d_va;
	req.bytes = MT_GFX_LINUX_PACKET_BYTES;
	req.type = 3;
	req.submit_flags = 0;

	d->markers.ready = true;
	d->markers.work_ready = true;
	ret = d->markers.ops->submit_context(&d->markers, &context_3d, &command_3d, &req, &fence);
	d->markers.work_ready = false;
	d->markers.ready = false;

	if (ret) {
		mutex_unlock(&d->state.trial_lock);
		mutex_unlock(&submit_lock);
		goto put_sync;
	}

	submitted++;
	last_sequence = fence->seqno;
	r->sequence = fence->seqno;

	if (target_obj)
		dma_resv_add_fence(target_obj->resv, fence, DMA_RESV_USAGE_WRITE);

	if (sync)
		drm_syncobj_replace_fence(sync, fence);

	mutex_unlock(&d->state.trial_lock);

	/* Wait and measure latency */
	t_start = ktime_get();
	waited = dma_fence_wait_timeout(fence, false, msecs_to_jiffies(3000));
	t_end = ktime_get();
	r->latency_us = (u32)ktime_to_us(ktime_sub(t_end, t_start));

	ret = waited > 0 ? dma_fence_get_status(fence) :
		(waited < 0 ? (int)waited : -ETIMEDOUT);
	if (ret == 1) {
		ret = 0;
		completed++;
	} else {
		if (!ret) ret = -EIO;
		faulted = true;
	}

	mutex_unlock(&submit_lock);

put_sync:
	if (fence)
		dma_fence_put(fence);
	if (sync)
		drm_syncobj_put(sync);
unlock_resv:
	if (locked_resv)
		drm_gem_unlock_reservations(&target_obj, 1, &acquire);
put_target:
	if (target_obj)
		drm_gem_object_put(target_obj);
	return ret;
}

static int mt_open(struct drm_device *dev, struct drm_file *file)
{
	return allowed();
}

static const struct drm_ioctl_desc ioctls[] = {
	DRM_IOCTL_DEF_DRV(MT_QUERY, query_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MT_CREATE, create_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MT_READ, read_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MT_WRITE, write_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MT_COPY, copy_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MT_FILL, fill_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MT_SUBMIT_3D, submit_3d_ioctl, DRM_RENDER_ALLOW),
};

DEFINE_DRM_GEM_FOPS(mt_drm_fops);
static const struct drm_driver mt_driver = {
	.driver_features = DRIVER_GEM | DRIVER_RENDER | DRIVER_SYNCOBJ,
	.open = mt_open, .gem_prime_import = deny_import,
	.ioctls = ioctls, .num_ioctls = ARRAY_SIZE(ioctls), .fops = &mt_drm_fops,
	.name = "mtvgpu", .desc = "MT vGPU Unified 2D Copy, Native Clear & 3D Universal DRM Front End",
	.major = 0, .minor = 3, .patchlevel = 0,
};

static void release_unpublished(void)
{
	u32 i;
	if (work_2d.context)
		WARN_ON(mt_tqx_work_cancel(&work_2d));
	if (context_2d.process) {
		WARN_ON(mt_tqx_context_pool_slices_release(&context_2d));
		WARN_ON(mt_execution_context_destroy(&context_2d));
	}
	if (context_3d.process) {
		WARN_ON(mt_execution_context_destroy(&context_3d));
	}
	for (i = 0; i < MT_GFX_CONTEXT_BO_COUNT; i++) {
		if (context_bos[i].refs)
			WARN_ON(mt_bo_put(&context_bos[i]));
	}
	if (command_3d.refs) WARN_ON(mt_bo_put(&command_3d));
	if (process_3d.store)
		WARN_ON(mt_execution_process_destroy(&process_3d));
	if (space_3d)
		WARN_ON(d->address_spaces.ops->destroy(space_3d));

	if (process_2d.store)
		WARN_ON(mt_execution_process_destroy(&process_2d));
	if (space_2d)
		WARN_ON(d->address_spaces.ops->destroy(space_2d));

	for (i = 0; i < MT_DRM_SLOT_COUNT; i++)
		if (slots[i].bo.refs)
			WARN_ON(mt_bo_put(&slots[i].bo));
	if (command_2d.refs) WARN_ON(mt_bo_put(&command_2d));
	if (dma_2d.refs) WARN_ON(mt_bo_put(&dma_2d));
	if (state_2d.refs) WARN_ON(mt_bo_put(&state_2d));
	ready_3d = false;
}

static int prepare_context(void)
{
	struct mt_bo *private[3] = {&command_2d, &dma_2d, &state_2d};
	const u64 va[3] = {MT_TQX_CMD_VA, MT_TQX_DMA_VA, MT_TQX_STATE_VA};
	const u32 size[3] = {MT_TQX_CMD_BO_BYTES, MT_TQX_DMA_BO_BYTES, MT_TQX_STATE_BO_BYTES};
	struct mt_bo *bos[5];
	struct mt_tqx_submission_input input = {
		.stream = {.copy = {MT_TQX_STREAM_SRC_VA, MT_TQX_STREAM_DST_VA, 256}, .va = {MT_TQX_CMD_VA}},
		.dma_va = MT_TQX_DMA_VA, .state_va = MT_TQX_STATE_VA};
	struct mt_gfx_context_bo_addresses addrs;
	u32 i;
	int ret = idle();
	if (ret)
		return ret;

	ret = mt_tqx_topology_from_info(&cores, &d->gem.profile, (void *)d->state.info, PAGE_SIZE);
	if (ret || cores != d->gem.tqx_cores)
		return ret ? ret : -EINVAL;

	/* --- 1. Prepare 2D Graphics Context --- */
	ret = d->address_spaces.ops->create(&d->address_spaces, 32, &space_2d);
	if (ret)
		return ret;

	for (i = 0; i < 3; i++) {
		ret = mt_bo_create(private[i], d->buffers.ops, &d->buffers, size[i], PAGE_SIZE);
		if (!ret)
			ret = d->address_spaces.ops->bind(space_2d, private[i], va[i], 0, size[i], MT_GPU_MAP_DEFAULT);
		if (ret)
			return ret;
	}

	for (i = 0; i < MT_DRM_SLOT_COUNT; i++) {
		slots[i].va = slot_va(i);
		ret = mt_bo_create(&slots[i].bo, d->buffers.ops, &d->buffers, slot_bytes(i), PAGE_SIZE);
		if (!ret)
			ret = d->address_spaces.ops->bind(space_2d, &slots[i].bo,
				slots[i].va, 0, slot_bytes(i), MT_GPU_MAP_DEFAULT);
		if (ret)
			return ret;
	}

	ret = d->address_spaces.ops->bind_boot_shared(space_2d, &d->gem.profile);
	if (ret)
		return ret;

	ret = mt_execution_process_create(&d->execution, &process_2d, &space_2d->vm, task_tgid_nr(current));
	if (ret)
		return ret;

	ret = mt_execution_context_create(&context_2d, &process_2d, 1, 0);
	if (ret)
		return ret;

	ret = d->address_spaces.ops->upload(space_2d);
	if (ret)
		return ret;
	ret = d->address_spaces.ops->seal(space_2d);
	if (ret)
		return ret;

	/* Test-prepare 2D work */
	bos[0] = &command_2d; bos[1] = &slots[0].bo; bos[2] = &slots[1].bo;
	bos[3] = &dma_2d; bos[4] = &state_2d;
	input.stream.copy.src = slots[0].va;
	input.stream.copy.dst = slots[1].va;
	ret = mt_tqx_work_prepare_from_pools(&work_2d, &d->shared_boot, workspace,
		&upload, &d->gem.profile, cores, &context_2d, bos, &input);
	if (!ret)
		ret = mt_tqx_work_cancel(&work_2d);
	if (ret)
		return ret;

	/* --- 2. Prepare 3D Universal Context --- */
	ret = d->address_spaces.ops->create(&d->address_spaces, 32, &space_3d);
	if (ret)
		return ret;

	for (i = 0; i < MT_GFX_CONTEXT_BO_COUNT; i++) {
		u32 bytes = mt_gfx_context_bo_specs[i].bytes;
		u32 alloc_size = PAGE_ALIGN(bytes);
		u64 bva = MT_CTX_BO_BASE_VA + i * MT_CTX_BO_STRIDE;

		context_vas[i] = bva;
		addrs.va[i] = bva;

		ret = mt_bo_create(&context_bos[i], d->buffers.ops, &d->buffers, alloc_size, PAGE_SIZE);
		if (ret)
			return ret;
		ret = write_bo(&context_bos[i], 0, mt_gfx_bo_init_metas[i].data, bytes);
		if (ret)
			return ret;
		ret = d->address_spaces.ops->bind(space_3d, &context_bos[i], bva, 0, alloc_size, MT_GPU_MAP_DEFAULT);
		if (ret)
			return ret;
	}

	ret = mt_gfx_context_build_csw(csw_3d, sizeof(csw_3d), &addrs);
	if (ret)
		return ret;

	ret = mt_bo_create(&command_3d, d->buffers.ops, &d->buffers,
			   MT_TRANSLATE_CMD_BYTES, PAGE_SIZE);
	if (ret)
		return ret;

	ret = write_bo(&command_3d, 0, mt_gfx_linux_packet_template, MT_GFX_LINUX_PACKET_BYTES);
	if (ret)
		return ret;

	{
		u64 csw_va = command_3d_va + 0x58ULL;
		ret = write_bo(&command_3d, 0x10, &csw_va, 8);
		if (ret)
			return ret;
	}
	ret = write_bo(&command_3d, 0x58, csw_3d, sizeof(csw_3d));
	if (ret)
		return ret;

	ret = d->address_spaces.ops->bind(space_3d, &command_3d, command_3d_va, 0,
					  MT_TRANSLATE_CMD_BYTES,
					  MT_GPU_MAP_DEFAULT);
	if (ret)
		return ret;

	/* Bind slots 0 & 1 as potential 3D Render Targets into space_3d */
	for (i = 0; i < 2; i++) {
		u64 rt_va = 0x48100000ULL + i * 0x100000ULL;
		slots[i].va_3d = rt_va;
		ret = d->address_spaces.ops->bind(space_3d, &slots[i].bo, rt_va, 0, slot_bytes(i), MT_GPU_MAP_DEFAULT);
		if (ret) {
			pr_err("mt_live_3d_drm: failed to bind slot %u to 3D space: %d\n", i, ret);
			return ret;
		}
	}

	/* Overwrite packet template's unmapped remote address with safe default RT0 */
	{
		u64 def_rt_va = slots[0].va_3d;
		u64 def_rt_stride = 128ULL * 4;
		u64 def_rt_extent = (128ULL << 16) | 128ULL;
		write_bo(&command_3d, 0x45a0, &def_rt_va, 8);
		write_bo(&command_3d, 0x45a8, &def_rt_stride, 8);
		write_bo(&command_3d, 0x45b0, &def_rt_extent, 8);
		write_bo(&command_3d, 0x4668, &def_rt_va, 8);
	}

	ret = d->address_spaces.ops->bind_boot_shared(space_3d, &d->gem.profile);
	if (ret)
		return ret;

	ret = mt_execution_process_create(&d->execution, &process_3d, &space_3d->vm, task_tgid_nr(current));
	if (ret)
		return ret;

	ret = mt_execution_context_create(&context_3d, &process_3d, 5, 0);
	if (ret)
		return ret;

	ret = d->address_spaces.ops->upload(space_3d);
	if (ret)
		return ret;
	ret = d->address_spaces.ops->seal(space_3d);
	if (ret)
		return ret;

	ready_3d = true;
	/* Mapping headroom is derived from the page-table budget, not a fixed
	 * constant. Report it so the next mapping stage is measurable. */
	pr_info("mt_live_3d_drm: prepared unified 2D VM (pages=%u, maps=%u/%u) & 3D VM (pages=%u, maps=%u/%u)\n",
		space_2d->vm.used_pages, space_2d->vm.count, space_2d->vm.max_ranges,
		space_3d->vm.used_pages, space_3d->vm.count, space_3d->vm.max_ranges);

	return 0;
}


static int __init start(void)
{
	int ret = -ENODEV;
	(void)mt_fw_event_io_ops;
	if (!enable)
		return -EPERM;
	workspace = kvzalloc(sizeof(*workspace), GFP_KERNEL);
	fill_workspace = kvzalloc(sizeof(*fill_workspace), GFP_KERNEL);
	if (!workspace || !fill_workspace) {
		ret = -ENOMEM;
		goto free_workspace;
	}
	device = mt_guest_find_s3000();
	if (!device)
		goto free_workspace;
	device_lock(&device->dev);
	if (!mt_guest_match_s3000(device->vendor, device->device, device->subsystem_vendor, device->subsystem_device) ||
	    !device->driver || strcmp(device->driver->name, MT_GUEST_DRIVER_NAME))
		goto unlock_device;
	owner = device->driver->driver.owner;
	if (!owner || !try_module_get(owner))
		goto unlock_device;
	d = pci_get_drvdata(device);
	if (!d || d->markers.lock != &d->state.trial_lock ||
	    d->markers.opaque != &d->state || !d->markers.can_submit ||
	    !d->markers.ops || !d->markers.ops->submit_tqx_work ||
	    !d->markers.ops->submit_context)
		goto put_owner;

	mutex_lock(&d->state.trial_lock);
	ret = prepare_context();
	mutex_unlock(&d->state.trial_lock);
	if (ret)
		goto release_context;

	drm = drm_dev_alloc(&mt_driver, &device->dev);
	if (IS_ERR(drm)) {
		ret = PTR_ERR(drm);
		goto release_context;
	}

	ret = drm_dev_register(drm, 0);
	if (!ret) {
		WRITE_ONCE(ready, true);
		device_unlock(&device->dev);
		pr_info("mt_live_3d_drm: registered unified mtvgpu DRM node: 2D Copy, Native Fill & 3D Universal Render\n");
		return 0;
	}

	WRITE_ONCE(ready, false);
	drm_dev_put(drm);

release_context:
	mutex_lock(&d->state.trial_lock);
	release_unpublished();
	mutex_unlock(&d->state.trial_lock);
put_owner:
	module_put(owner);
unlock_device:
	device_unlock(&device->dev);
	pci_dev_put(device);
free_workspace:
	kvfree(workspace);
	kvfree(fill_workspace);
	return ret;
}

static void __exit stop(void)
{
	WRITE_ONCE(ready, false);
	drm_dev_unregister(drm);
	drm_dev_put(drm);
	mutex_lock(&d->state.trial_lock);
	WARN_ON(retained);
	release_unpublished();
	mutex_unlock(&d->state.trial_lock);
	module_put(owner);
	pci_dev_put(device);
	kvfree(workspace);
	kvfree(fill_workspace);
	pr_info("mt_live_3d_drm: unloaded cleanly\n");
}

module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Unified 2D Copy, Native Fill & 3D Universal DRM Front End for MT vGPU");
