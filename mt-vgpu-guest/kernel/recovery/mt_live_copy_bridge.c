// SPDX-License-Identifier: GPL-2.0
/* Experimental userspace staging bridge on the verified, self-pinned r28
 * context. Original modules retain all GPU roots, BO owners and callbacks.
 * Requests are serialized across files; buffers never outlive kernel copies.
 * This is not a DRM/UMD implementation and does not enable desktop rendering.
 */
#include "../mt_guest_device.h"
#include "../mt_addr_plan.h"
#include <linux/capability.h>
#include <linux/compat.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include "../../include/mt_copy_uapi.h"

static bool enable;
module_param(enable, bool, 0400);
static DEFINE_MUTEX(submit_lock);
static bool faulted;
static u64 submitted, completed, last_sequence;
static struct pci_dev *device;
static struct module *owner;
static struct mt_guest_device *d;
static struct mt_gpu_vm *vm;
static struct mt_execution_context *context;
static struct mt_bo *ordinary[5];
static unsigned int references;
static struct mt_tqx_work work;
static struct mt_tqx_submission_workspace *workspace;
static u8 expected[4096], observed[4096];
static u32 cores;
static_assert(sizeof(struct mt_guest_device) == 30784);
static_assert(offsetof(struct mt_guest_device, markers) == 29888);
static_assert(sizeof(struct mt_copy_request) == 8232);
static_assert(sizeof(struct mt_copy_query) == 48);

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
	    vm->active_uses || context->active_jobs)
		return -EBUSY;
	return d->markers.can_submit(&d->state);
}

/* submit_lock spans prepare, fence wait and CPU readback. trial_lock must be
 * released during the wait so the original event handler can complete work.
 * No caller-owned pointers remain in the published command or its callbacks. */
static int execute_copy(struct mt_copy_request *r)
{
	struct mt_tqx_submission_input input = {
		.stream = {.copy = {MT_TQX_STREAM_SRC_VA + r->source_offset,
			MT_TQX_STREAM_DST_VA + r->destination_offset, r->bytes}, .va = {MT_TQX_CMD_VA}},
		.dma_va = MT_TQX_DMA_VA, .state_va = MT_TQX_STATE_VA,
	};
	struct dma_fence *fence = NULL;
	bool published = false;
	long waited;
	int ret;
	lockdep_assert_held(&submit_lock);
	mutex_lock(&d->state.trial_lock);
	ret = faulted ? -EIO : idle();
	if (ret)
		goto unlock;
	ret = write_bo(ordinary[1], 0, r->source, sizeof(r->source));
	if (!ret)
		ret = read_bo(ordinary[1], 0, observed, sizeof(observed));
	if (!ret && memcmp(r->source, observed, sizeof(observed)))
		ret = -EIO;
	if (!ret)
		ret = write_bo(ordinary[2], 0, r->destination, sizeof(r->destination));
	if (!ret)
		ret = read_bo(ordinary[2], 0, observed, sizeof(observed));
	if (!ret && memcmp(r->destination, observed, sizeof(observed)))
		ret = -EIO;
	if (ret)
		goto unlock;
	/* expected is verification only: returned data is always read from BO2. */
	memcpy(expected, r->destination, sizeof(expected));
	memcpy(expected + r->destination_offset, r->source + r->source_offset, r->bytes);
	ret = mt_tqx_work_prepare_from_pools(&work, &d->shared_boot, workspace,
		&upload, &d->gem.profile, cores, context, ordinary, &input);
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
	mutex_unlock(&d->state.trial_lock);
	/* Bounded wait also keeps abrupt process exit from releasing GPU owners. */
	waited = dma_fence_wait_timeout(fence, false, msecs_to_jiffies(5000));
	ret = waited > 0 ? dma_fence_get_status(fence) :
		(waited < 0 ? (int)waited : -ETIMEDOUT);
	mutex_lock(&d->state.trial_lock);
	if (ret == 1) {
		ret = idle();
		if (!ret)
			ret = read_bo(ordinary[2], 0, r->destination, sizeof(r->destination));
		if (!ret && memcmp(r->destination, expected, sizeof(expected)))
			ret = -EILSEQ;
		if (!ret)
			ret = read_bo(ordinary[1], 0, observed, sizeof(observed));
		if (!ret && memcmp(r->source, observed, sizeof(observed)))
			ret = -EILSEQ;
		if (!ret) {
			r->sequence = fence->seqno;
			completed++;
		}
	}
	goto unlock;
cancel:
	if (work.context) {
		int cancelled = mt_tqx_work_cancel(&work);
		if (WARN_ON(cancelled)) {
			/* Unexpected ownership failure: retain rather than free live work. */
			faulted = true;
			__module_get(THIS_MODULE);
		}
	}
unlock:
	if (published && ret)
		faulted = true;
	mutex_unlock(&d->state.trial_lock);
	dma_fence_put(fence);
	return ret;
}

static int bridge_open(struct inode *inode, struct file *file)
{
	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;
	return nonseekable_open(inode, file);
}

static long bridge_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *user = (void __user *)arg;
	struct mt_copy_request *r;
	struct mt_copy_query q = { .abi = MT_COPY_ABI,
		.max_bytes = MT_COPY_PAGE_BYTES,
		.capabilities = MT_COPY_CAP_SYNC | MT_COPY_CAP_SERIAL };
	int ret;
	/* Recheck after descriptor inheritance or transfer to another process. */
	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;
	if (cmd == MT_COPY_QUERY) {
		mutex_lock(&submit_lock);
		q.faulted = faulted;
		q.submitted = submitted;
		q.completed = completed;
		q.last_sequence = last_sequence;
		mutex_unlock(&submit_lock);
		return copy_to_user(user, &q, sizeof(q)) ? -EFAULT : 0;
	}
	if (cmd != MT_COPY_EXEC)
		return -ENOTTY;
	r = memdup_user(user, sizeof(*r));
	if (IS_ERR(r))
		return PTR_ERR(r);
	/* Validate the copied image once; never re-read request fields from user. */
	if (r->abi != MT_COPY_ABI || r->flags || r->sequence ||
	    memchr_inv(r->reserved, 0, sizeof(r->reserved)) || !r->bytes ||
	    r->bytes > MT_COPY_PAGE_BYTES ||
	    r->source_offset > MT_COPY_PAGE_BYTES - r->bytes ||
	    r->destination_offset > MT_COPY_PAGE_BYTES - r->bytes) {
		ret = -EINVAL;
		goto free_request;
	}
	ret = mutex_lock_interruptible(&submit_lock);
	if (ret)
		goto free_request;
	ret = execute_copy(r);
	mutex_unlock(&submit_lock);
	if (!ret && copy_to_user(user, r, sizeof(*r)))
		ret = -EFAULT;
free_request:
	kfree(r);
	return ret;
}
#ifdef CONFIG_COMPAT
static long bridge_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return bridge_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
#endif
static const struct file_operations bridge_fops = {
	.owner = THIS_MODULE, .open = bridge_open, .unlocked_ioctl = bridge_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = bridge_compat_ioctl,
#endif
};
static struct miscdevice bridge_device = {
	.minor = MISC_DYNAMIC_MINOR, .name = "mt-vgpu-copy", .fops = &bridge_fops,
	.mode = 0600,
};

static int attach_context(void)
{
	const u64 va[5] = {MT_TQX_CMD_VA, MT_TQX_STREAM_SRC_VA, MT_TQX_STREAM_DST_VA, MT_TQX_DMA_VA, MT_TQX_STATE_VA};
	const u32 bytes[5] = {MT_TQX_CMD_BO_BYTES, MT_TQX_STREAM_SLOT_BYTES, MT_TQX_STREAM_SLOT_BYTES, MT_TQX_DMA_BO_BYTES, MT_TQX_STATE_BO_BYTES};
	const u32 pool_order[3] = {1, 0, 2};
	struct mt_vm_vram *v;
	u32 i, j;
	int ret;
	if (d->execution.processes != 1 || d->execution.contexts != 1 ||
	    d->address_spaces.objects != 1 || !d->reserved_pools.prepared ||
	    !d->reserved_pools.slices[1].slices)
		return -ENOENT;
	context = d->reserved_pools.slices[1].slices->context;
	if (!context || !context->process || context->process->store != &d->execution ||
	    context->process->contexts != 1 || context->pool_slices != 3 ||
	    !context->process->vm || context->route.type != 1 || context->route.dm != 1)
		return -EXDEV;
	for (i = 0; i < 3; i++) {
		struct mt_pool_state *pool = &d->reserved_pools.slices[pool_order[i]];
		struct mt_pool_slice *slice = pool->slices;
		if (!slice || slice->next || slice != context->tqx_pool_slices[i] ||
		    slice->state != pool || slice->context != context || slice->bytes < 4096 ||
		    slice->bo != d->shared_boot.slots[MT_PROCESS_SHARED_COUNT + pool_order[i]])
			return -EXDEV;
	}
	vm = context->process->vm;
	v = container_of(vm, struct mt_vm_vram, vm);
	if (v->store != &d->address_spaces || vm->tables != &v->tables ||
	    !vm->sealed || !vm->uploaded || vm->capacity != 131072 ||
	    vm->count != 14 || vm->owners != 1 || !mt_boot_bo_bound(&d->shared_boot, vm))
		return -EXDEV;
	ret = idle();
	if (ret)
		return ret;
	ret = mt_tqx_topology_from_info(&cores, &d->gem.profile, (void *)d->state.info, PAGE_SIZE);
	if (ret || cores != d->gem.tqx_cores)
		return ret ? ret : -EINVAL;
	for (i = 0; i < 5; i++) {
		for (j = 0; j < vm->count; j++) {
			struct mt_vm_binding *b = &vm->bindings[j];
			if (b->va == va[i] && !b->offset && b->bytes == bytes[i] && !b->flags) {
				if (ordinary[i])
					return -EUCLEAN;
				ordinary[i] = b->bo;
			}
		}
		if (!ordinary[i] || ordinary[i]->store != &d->buffers ||
		    ordinary[i]->ops != d->buffers.ops || ordinary[i]->cpu_users ||
		    ordinary[i]->gpu_users)
			return -EXDEV;
		ret = mt_bo_get(ordinary[i]);
		if (ret)
			return ret;
		references++;
	}
	return 0;
}


static int __init bridge_init(void)
{
	int ret = -ENODEV;
	(void)mt_fw_event_io_ops;
	if (!enable)
		return -EPERM;
	workspace = kvzalloc(sizeof(*workspace), GFP_KERNEL);
	if (!workspace)
		return -ENOMEM;
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
	ret = attach_context();
	mutex_unlock(&d->state.trial_lock);
	if (!ret) {
		bridge_device.parent = &device->dev;
		ret = misc_register(&bridge_device);
	}
	if (!ret) {
		device_unlock(&device->dev);
		pr_info("mt_live_copy_bridge: /dev/mt-vgpu-copy ready, max_bytes=4096 serialized=1\n");
		return 0;
	}
	mutex_lock(&d->state.trial_lock);
	while (references)
		mt_bo_put(ordinary[--references]);
	mutex_unlock(&d->state.trial_lock);
put_owner:
	module_put(owner);
unlock_device:
	device_unlock(&device->dev);
	pci_dev_put(device);
free_workspace:
	kvfree(workspace);
	return ret;
}
static void __exit bridge_exit(void)
{
	/* fops owner keeps the module loaded through open files and ioctls.
	 * Published jobs still belong to the retained original marker store. */
	misc_deregister(&bridge_device);
	mutex_lock(&d->state.trial_lock);
	while (references)
		mt_bo_put(ordinary[--references]);
	mutex_unlock(&d->state.trial_lock);
	module_put(owner);
	pci_dev_put(device);
	kvfree(workspace);
}
module_init(bridge_init);
module_exit(bridge_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Serialized userspace staging-copy bridge for retained TQX");
