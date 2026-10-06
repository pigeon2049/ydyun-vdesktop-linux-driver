// SPDX-License-Identifier: GPL-2.0
/* Direct-VRAM DRM front end with bounded immutable mappings. Slots are leased
 * through GEM handles; closing a handle does not unmap a published root.
 * First root seal retains this module/context until a real withdrawal protocol
 * is implemented. No raw commands, user GPU VA, mmap or PRIME are exposed. */
#include "../mt_guest_device.h"
#include "../mt_addr_plan.h"
#include "../mt_tqx_fill_work.h"
#include <linux/capability.h>
#include <linux/dma-resv.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_syncobj.h>
#include "../../include/mt_drm_uapi.h"

static bool enable;
module_param(enable, bool, 0400);
static struct pci_dev *device;
static struct module *owner;
static struct mt_guest_device *d;
static struct drm_device *drm;
static struct mt_vm_vram *space;
static struct mt_execution_process process;
static struct mt_execution_context context;
static struct mt_bo command, dma, state;
static struct mt_tqx_work work;
static struct mt_tqx_submission_workspace *workspace;
static struct mt_tqx_fill_workspace *fill_workspace;
static DEFINE_MUTEX(submit_lock);
static bool ready, retained, faulted;
static u64 submitted, completed, last_sequence;
static u32 cores, leased;
struct slot { struct mt_bo bo; u64 va; bool leased; };
static struct slot slots[MT_DRM_SLOT_COUNT];
#ifdef MT_LIVE_LARGE_SURFACES
/* Fit the current Guest's remaining ordinary heap without resizing a live
 * allocator: one 8 MiB frame, one 4 MiB transfer tile, six 64 KiB objects. */
#define MT_LIVE_SLOT_MAX (8U * 1024U * 1024U)
static u32 slot_bytes(u32 i)
{
	return i == 0 ? MT_LIVE_SLOT_MAX : i == 1 ? 4U * 1024U * 1024U : MT_DRM_SLOT_BYTES;
}
static u64 slot_va(u32 i) { return 0x41000000ULL + i * 0x1000000ULL; }
#else
#define MT_LIVE_SLOT_MAX MT_DRM_SLOT_BYTES
static u32 slot_bytes(u32 i) { return MT_DRM_SLOT_BYTES; }
static u64 slot_va(u32 i) { return 0x40100000ULL + i * 0x100000ULL; }
#endif
struct lease { struct drm_gem_object base; struct slot *slot; };
static_assert(sizeof(struct mt_guest_device) == 30784);
static_assert(offsetof(struct mt_guest_device, markers) == 29888);
static_assert(sizeof(struct drm_mt_query) == 80);
static_assert(sizeof(struct drm_mt_create) == 16);
static_assert(sizeof(struct drm_mt_rw) == 4120);
static_assert(sizeof(struct drm_mt_copy) == 48);
static_assert(sizeof(struct drm_mt_fill) == 56);

/* Dispatch through original BO ops. Header-local mt_bo_vram_* helpers check
 * their own static ops address and cannot operate on another module's BOs. */
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
	    !d->service.running || d->markers.total ||
	    d->markers.ready || d->markers.work_ready ||
	    (space && space->vm.active_uses) || context.active_jobs)
		return -EBUSY;
	return d->markers.can_submit(&d->state);
}
static int allowed(void)
{
	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;
	if (!READ_ONCE(ready))
		return -ENODEV;
	return 0;
}
static void lease_free(struct drm_gem_object *obj)
{
	struct lease *l = container_of(obj, struct lease, base);
	struct drm_device *dev = obj->dev;
	mutex_lock(&d->state.trial_lock);
	WARN_ON(!l->slot->leased || !leased);
	l->slot->leased = false;
	leased--;
	WARN_ON(mt_bo_put(&l->slot->bo));
	mutex_unlock(&d->state.trial_lock);
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
	mutex_lock(&submit_lock);
	mutex_lock(&d->state.trial_lock);
	*q = (struct drm_mt_query){.abi = MT_DRM_ABI, .slot_count = MT_DRM_SLOT_COUNT,
		.slot_bytes = MT_LIVE_SLOT_MAX, .leased = leased, .faulted = faulted,
		.retained = retained, .submitted = submitted, .completed = completed,
		.capabilities = MT_DRM_CAP_COPY | MT_DRM_CAP_FILL,
		.last_sequence = last_sequence};
	mutex_unlock(&d->state.trial_lock);
	mutex_unlock(&submit_lock);
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
	mutex_lock(&submit_lock);
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
	/* Clearing the entire physical slot prevents cross-file content reuse. */
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
	mutex_unlock(&submit_lock);
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
	mutex_lock(&submit_lock);
	mutex_lock(&d->state.trial_lock);
	ret = transfer(&l->slot->bo, r->offset, r->data, r->bytes, write);
	mutex_unlock(&d->state.trial_lock);
	mutex_unlock(&submit_lock);
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
	struct drm_gem_object *objects[2] = {NULL, NULL};
	struct lease *src, *dst;
	struct drm_syncobj *sync = NULL;
	struct dma_fence *fence = NULL;
	struct ww_acquire_ctx acquire;
	struct mt_bo *bos[5];
	struct mt_tqx_submission_input input;
	bool published = false;
	long waited;
	int ret = allowed();
	if (ret)
		return ret;
	if (r->flags || r->reserved || r->sequence || !r->bytes ||
	    r->bytes > MT_LIVE_SLOT_MAX || !r->out_syncobj || r->source == r->destination)
		return -EINVAL;
	objects[0] = lookup(file, r->source);
	objects[1] = lookup(file, r->destination);
	if (!objects[0] || !objects[1]) {
		ret = -ENOENT;
		goto put;
	}
	if (objects[0] == objects[1] || r->source_offset > objects[0]->size ||
	    r->bytes > objects[0]->size - r->source_offset ||
	    r->destination_offset > objects[1]->size ||
	    r->bytes > objects[1]->size - r->destination_offset) {
		ret = -EINVAL;
		goto put;
	}
	sync = drm_syncobj_find(file, r->out_syncobj);
	if (!sync) {
		ret = -ENOENT;
		goto put;
	}
	ret = mutex_lock_interruptible(&submit_lock);
	if (ret)
		goto put;
	ret = drm_gem_lock_reservations(objects, 2, &acquire);
	if (ret)
		goto unlock_submit;
	ret = dma_resv_reserve_fences(objects[0]->resv, 1);
	if (!ret)
		ret = dma_resv_reserve_fences(objects[1]->resv, 1);
	if (ret)
		goto unlock_reservations;
	src = container_of(objects[0], struct lease, base);
	dst = container_of(objects[1], struct lease, base);
	bos[0] = &command; bos[1] = &src->slot->bo; bos[2] = &dst->slot->bo;
	bos[3] = &dma; bos[4] = &state;
	input = (struct mt_tqx_submission_input){
		.stream = {.copy = {src->slot->va + r->source_offset,
			dst->slot->va + r->destination_offset, r->bytes}, .va = {MT_TQX_CMD_VA}},
		.dma_va = MT_TQX_DMA_VA, .state_va = MT_TQX_STATE_VA};
	mutex_lock(&d->state.trial_lock);
	ret = faulted ? -EIO : idle();
	if (ret)
		goto unlock_session;
	ret = mt_tqx_work_prepare_from_pools(&work, &d->shared_boot, workspace,
		&upload, &d->gem.profile, cores, &context, bos, &input);
	if (!ret && !retained) {
		ret = d->address_spaces.ops->seal(space);
		if (!ret) {
			/* Root withdrawal is not implemented. Stable owners must remain. */
			__module_get(THIS_MODULE);
			retained = true;
		}
	}
	if (ret)
		goto cancel;
	d->markers.ready = true;
	d->markers.work_ready = true;
	ret = d->markers.ops->submit_tqx_work(&d->markers, &work, &fence);
	d->markers.work_ready = false;
	d->markers.ready = false;
	if (ret)
		goto cancel;
	published = true;
	submitted++;
	last_sequence = fence->seqno;
	dma_resv_add_fence(objects[0]->resv, fence, DMA_RESV_USAGE_READ);
	dma_resv_add_fence(objects[1]->resv, fence, DMA_RESV_USAGE_WRITE);
	drm_syncobj_replace_fence(sync, fence);
	goto unlock_session;
cancel:
	if (work.context) {
		int cancelled = mt_tqx_work_cancel(&work);
		if (WARN_ON(cancelled)) {
			faulted = true;
			__module_get(THIS_MODULE);
		}
	}
unlock_session:
	mutex_unlock(&d->state.trial_lock);
unlock_reservations:
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
			if (!ret)
				ret = -EIO;
			faulted = true;
		}
	}
unlock_submit:
	mutex_unlock(&submit_lock);
put:
	dma_fence_put(fence);
	if (sync)
		drm_syncobj_put(sync);
	drm_gem_object_put(objects[1]);
	drm_gem_object_put(objects[0]);
	return ret;
}

static int fill_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_mt_fill *r = data;
	struct drm_gem_object *objects[1] = {NULL};
	struct lease *dst;
	struct drm_syncobj *sync = NULL;
	struct dma_fence *fence = NULL;
	struct ww_acquire_ctx acquire;
	struct mt_bo *bos[4];
	struct mt_tqx_fill_input input;
	bool published = false;
	long waited;
	int ret = allowed();
	if (ret)
		return ret;
	if (r->flags || r->sequence || !r->out_syncobj || !r->width || !r->height ||
	    r->width > 32768 || r->height > 32768 || (r->offset & 3) ||
	    !r->rect_width || !r->rect_height || r->x >= r->width || r->y >= r->height ||
	    r->rect_width > r->width - r->x || r->rect_height > r->height - r->y)
		return -EINVAL;
	objects[0] = lookup(file, r->destination);
	if (!objects[0]) {
		ret = -ENOENT;
		goto put;
	}
	if (r->offset > objects[0]->size ||
	    (u64)r->width * r->height * 4 > objects[0]->size - r->offset) {
		ret = -EINVAL;
		goto put;
	}
	sync = drm_syncobj_find(file, r->out_syncobj);
	if (!sync) {
		ret = -ENOENT;
		goto put;
	}
	ret = mutex_lock_interruptible(&submit_lock);
	if (ret)
		goto put;
	ret = drm_gem_lock_reservations(objects, 1, &acquire);
	if (ret)
		goto unlock_submit;
	ret = dma_resv_reserve_fences(objects[0]->resv, 1);
	if (ret)
		goto unlock_reservations;
	dst = container_of(objects[0], struct lease, base);
	bos[0] = &command; bos[1] = &dst->slot->bo; bos[2] = &dma; bos[3] = &state;
	input = (struct mt_tqx_fill_input){
		.destination_va = dst->slot->va + r->offset, .command_va = MT_TQX_CMD_VA,
		.element_bytes = 4, .width = r->width, .height = r->height,
		.x = r->x, .y = r->y, .rect_width = r->rect_width, .rect_height = r->rect_height,
		.color = {r->color, 0, 0, 0}};
	mutex_lock(&d->state.trial_lock);
	ret = faulted ? -EIO : idle();
	if (ret)
		goto unlock_session;
	ret = mt_tqx_fill_work_prepare(&work, fill_workspace, &upload, &d->shared_boot,
		&d->gem.profile, cores, &context, bos, &input, MT_TQX_DMA_VA, MT_TQX_STATE_VA);
	if (!ret && !retained) {
		ret = d->address_spaces.ops->seal(space);
		if (!ret) {
			/* Root withdrawal is not implemented. Stable owners must remain. */
			__module_get(THIS_MODULE);
			retained = true;
		}
	}
	if (ret)
		goto cancel;
	d->markers.ready = true;
	d->markers.work_ready = true;
	ret = d->markers.ops->submit_tqx_work(&d->markers, &work, &fence);
	d->markers.work_ready = false;
	d->markers.ready = false;
	if (ret)
		goto cancel;
	published = true;
	submitted++;
	last_sequence = fence->seqno;
	dma_resv_add_fence(objects[0]->resv, fence, DMA_RESV_USAGE_WRITE);
	drm_syncobj_replace_fence(sync, fence);
	goto unlock_session;
cancel:
	if (work.context) {
		int cancelled = mt_tqx_work_cancel(&work);
		if (WARN_ON(cancelled)) {
			faulted = true;
			__module_get(THIS_MODULE);
		}
	}
unlock_session:
	mutex_unlock(&d->state.trial_lock);
unlock_reservations:
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
			if (!ret)
				ret = -EIO;
			faulted = true;
		}
	}
unlock_submit:
	mutex_unlock(&submit_lock);
put:
	dma_fence_put(fence);
	if (sync)
		drm_syncobj_put(sync);
	drm_gem_object_put(objects[0]);
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
};
DEFINE_DRM_GEM_FOPS(mt_drm_fops);
static const struct drm_driver mt_driver = {
	.driver_features = DRIVER_GEM | DRIVER_RENDER | DRIVER_SYNCOBJ,
	.open = mt_open, .gem_prime_import = deny_import,
	.ioctls = ioctls, .num_ioctls = ARRAY_SIZE(ioctls), .fops = &mt_drm_fops,
	.name = "mtvgpu", .desc = "MT vGPU experimental transfer and native clear",
	.major = 0, .minor = 2, .patchlevel = 0,
};

/* Unpublished root only. Caller holds original session lock. */
static void release_unpublished(void)
{
	u32 i;
	if (work.context)
		WARN_ON(mt_tqx_work_cancel(&work));
	if (context.process) {
		WARN_ON(mt_tqx_context_pool_slices_release(&context));
		WARN_ON(mt_execution_context_destroy(&context));
	}
	if (process.store)
		WARN_ON(mt_execution_process_destroy(&process));
	if (space)
		WARN_ON(d->address_spaces.ops->destroy(space));
	for (i = 0; i < MT_DRM_SLOT_COUNT; i++)
		if (slots[i].bo.refs)
			WARN_ON(mt_bo_put(&slots[i].bo));
	if (command.refs) WARN_ON(mt_bo_put(&command));
	if (dma.refs) WARN_ON(mt_bo_put(&dma));
	if (state.refs) WARN_ON(mt_bo_put(&state));
}
static int prepare_context(void)
{
	struct mt_bo *private[3] = {&command, &dma, &state};
	const u64 va[3] = {MT_TQX_CMD_VA, MT_TQX_DMA_VA, MT_TQX_STATE_VA};
	const u32 size[3] = {MT_TQX_CMD_BO_BYTES, MT_TQX_DMA_BO_BYTES, MT_TQX_STATE_BO_BYTES};
	struct mt_bo *bos[5];
	struct mt_tqx_submission_input input = {
		.stream = {.copy = {0x40100000, 0x40200000, 256}, .va = {MT_TQX_CMD_VA}},
		.dma_va = MT_TQX_DMA_VA, .state_va = MT_TQX_STATE_VA};
	u32 i;
	int ret = idle();
	if (ret)
		return ret;
	ret = mt_tqx_topology_from_info(&cores, &d->gem.profile, (void *)d->state.info, PAGE_SIZE);
	if (ret || cores != d->gem.tqx_cores)
		return ret ? ret : -EINVAL;
	ret = d->address_spaces.ops->create(&d->address_spaces, 32, &space);
	if (ret)
		return ret;
	for (i = 0; i < 3; i++) {
		ret = mt_bo_create(private[i], d->buffers.ops, &d->buffers, size[i], PAGE_SIZE);
		if (!ret)
			ret = d->address_spaces.ops->bind(space, private[i], va[i], 0, size[i], MT_GPU_MAP_DEFAULT);
		if (ret)
			return ret;
	}
	for (i = 0; i < MT_DRM_SLOT_COUNT; i++) {
		slots[i].va = slot_va(i);
		ret = mt_bo_create(&slots[i].bo, d->buffers.ops, &d->buffers, slot_bytes(i), PAGE_SIZE);
		if (!ret)
			ret = d->address_spaces.ops->bind(space, &slots[i].bo,
				slots[i].va, 0, slot_bytes(i), MT_GPU_MAP_DEFAULT);
		if (ret)
			return ret;
	}
	ret = d->address_spaces.ops->bind_boot_shared(space, &d->gem.profile);
	if (!ret)
		ret = mt_execution_process_create(&d->execution, &process, &space->vm, task_tgid_nr(current));
	if (!ret)
		ret = mt_execution_context_create(&context, &process, 1, 0);
	if (ret)
		return ret;
	bos[0] = &command; bos[1] = &slots[0].bo; bos[2] = &slots[1].bo;
	bos[3] = &dma; bos[4] = &state;
	input.stream.copy.src = slots[0].va;
	input.stream.copy.dst = slots[1].va;
	ret = mt_tqx_work_prepare_from_pools(&work, &d->shared_boot, workspace,
		&upload, &d->gem.profile, cores, &context, bos, &input);
	if (!ret)
		ret = mt_tqx_work_cancel(&work);
	if (!ret)
		pr_info("mt_live_drm: prepared root=%llx pages=%u mappings=%u token=%llu no_submission\n",
			space->tables.backing.gpu_pa, space->vm.used_pages, space->vm.count, process.token);
	return ret;
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
	device = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(14, 0));
	if (!device)
		goto free_workspace;
	device_lock(&device->dev);
	if (device->vendor != 0x1ed5 || device->device != 0x0222 ||
	    device->subsystem_vendor != 0x1ed5 || device->subsystem_device != 0x1101 ||
	    !device->driver || strcmp(device->driver->name, "mt_guest_probe"))
		goto unlock_device;
	owner = device->driver->driver.owner;
	if (!owner || !try_module_get(owner))
		goto unlock_device;
	d = pci_get_drvdata(device);
	if (!d || d->markers.lock != &d->state.trial_lock ||
	    d->markers.opaque != &d->state || !d->markers.can_submit ||
	    !d->markers.ops || !d->markers.ops->submit_tqx_work)
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
	/* Do not overwrite the original PCI drvdata. */
	ret = drm_dev_register(drm, 0);
	if (!ret) {
		WRITE_ONCE(ready, true);
		device_unlock(&device->dev);
		pr_info("mt_live_drm: registered mtvgpu GEM/render/syncobj; no display modesetting\n");
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
	WARN_ON(retained); /* A retained instance owns a permanent module reference. */
	release_unpublished();
	mutex_unlock(&d->state.trial_lock);
	module_put(owner);
	pci_dev_put(device);
	kvfree(workspace);
	kvfree(fill_workspace);
}
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Bounded direct-VRAM GEM and syncobj front end for MT vGPU");
