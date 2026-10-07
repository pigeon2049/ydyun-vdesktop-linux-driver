// SPDX-License-Identifier: GPL-2.0
/* r29: bounded copies on the self-pinned r28 mt_live_tqx context.
 * Never changes mappings, zeros engine state, withdraws roots or resets HW.
 * Context lifetime belongs to the original self-pinned helper. All published
 * jobs and fence callbacks belong to the original driver's ops, not this module.
 * Load only after verifying the retained helper binary and ABI. */
#include "../mt_guest_device.h"
#include "../mt_addr_plan.h"

static bool enable, attached, attempted, roots_unchanged;
module_param(enable, bool, 0400);
module_param(attached, bool, 0444);
module_param(roots_unchanged, bool, 0444);
static int result = -ENODATA;
module_param(result, int, 0444);
static unsigned int passed, submitted;
module_param(passed, uint, 0444);
module_param(submitted, uint, 0444);
static unsigned long long sequence;
module_param(sequence, ullong, 0444);
static struct pci_dev *device;
static struct module *owner;
static struct mt_guest_device *d;
static struct mt_gpu_vm *vm;
static struct mt_execution_context *context;
static struct mt_bo *ordinary[5];
static unsigned int references;
static struct mt_tqx_work work;
static struct mt_tqx_submission_workspace *workspace;
static u8 source[4096], expected[4096], observed[4096];
static void *root_before, *root_after;
static u32 cores;
static_assert(sizeof(struct mt_guest_device) == 30784);
static_assert(offsetof(struct mt_guest_device, markers) == 29888);

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


struct copy_case { u32 bytes, src, dst; };
static const struct copy_case cases[] = {
	{256, 0, 0}, {1, 0, 0}, {15, 0, 0}, {16, 0, 0},
	{31, 0, 0}, {257, 0, 0}, {1024, 0, 0}, {4096, 0, 0},
	{255, 1, 3}, {257, 3, 17}, {1023, 17, 33}, {2048, 64, 128},
	{7, 4089, 4089}, {16, 4080, 4077}, {3000, 111, 113},
};

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

static int one_copy(u32 iteration)
{
	const struct copy_case *c = &cases[iteration % ARRAY_SIZE(cases)];
	struct mt_tqx_submission_input input = {
		.stream = {.copy = {MT_TQX_STREAM_SRC_VA + c->src, MT_TQX_STREAM_DST_VA + c->dst, c->bytes},
			.va = {MT_TQX_CMD_VA}},
		.dma_va = MT_TQX_DMA_VA, .state_va = MT_TQX_STATE_VA,
	};
	struct dma_fence *fence = NULL;
	long waited;
	u32 i;
	int ret;
	mutex_lock(&d->state.trial_lock);
	ret = idle();
	if (ret)
		goto unlock;
	for (i = 0; i < sizeof(source); i++)
		source[i] = (u8)((i * 73 + 19 + iteration * 37) ^ (i >> 3) ^ (iteration >> 2));
	memset(expected, (u8)(0xa5 ^ (iteration * 13)), sizeof(expected));
	ret = write_bo(ordinary[1], 0, source, sizeof(source));
	if (!ret)
		ret = read_bo(ordinary[1], 0, observed, sizeof(observed));
	if (!ret && memcmp(source, observed, sizeof(source)))
		ret = -EIO;
	if (!ret)
		ret = write_bo(ordinary[2], 0, expected, sizeof(expected));
	if (!ret)
		ret = read_bo(ordinary[2], 0, observed, sizeof(observed));
	if (!ret && memcmp(expected, observed, sizeof(expected)))
		ret = -EIO;
	if (ret)
		goto unlock;
	memcpy(expected + c->dst, source + c->src, c->bytes);
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
	submitted++;
	sequence = fence->seqno;
	mutex_unlock(&d->state.trial_lock);
	waited = dma_fence_wait_timeout(fence, false, msecs_to_jiffies(5000));
	ret = waited > 0 ? dma_fence_get_status(fence) :
		(waited < 0 ? (int)waited : -ETIMEDOUT);
	mutex_lock(&d->state.trial_lock);
	if (ret == 1) {
		ret = idle();
		if (!ret)
			ret = read_bo(ordinary[2], 0, observed, sizeof(observed));
		if (!ret && memcmp(expected, observed, sizeof(expected)))
			ret = -EILSEQ;
		if (!ret)
			ret = read_bo(ordinary[1], 0, observed, sizeof(observed));
		if (!ret && memcmp(source, observed, sizeof(source)))
			ret = -EILSEQ;
		if (!ret)
			passed++;
	}
	goto unlock;
cancel:
	if (work.context) {
		int cancelled = mt_tqx_work_cancel(&work);
		if (WARN_ON(cancelled))
			__module_get(THIS_MODULE);
	}
unlock:
	mutex_unlock(&d->state.trial_lock);
	dma_fence_put(fence);
	pr_info("mt_live_tqx_repeat: case=%u bytes=%u src=%u dst=%u sequence=%llu result=%d passed=%u\n",
		iteration, c->bytes, c->src, c->dst, sequence, ret, passed);
	return ret;
}

static int run_set(const char *value, const struct kernel_param *param)
{
	bool run;
	u32 i;
	int ret;
	if (kstrtobool(value, &run) || !run)
		return -EINVAL;
	if (!attached)
		return -ENODEV;
	mutex_lock(&d->state.trial_lock);
	ret = attempted ? -EALREADY : idle();
	if (!ret) {
		attempted = true;
		ret = read_bo(vm->tables, 0, root_before, vm->capacity);
	}
	mutex_unlock(&d->state.trial_lock);
	if (ret)
		return ret;
	for (i = 0; i < 80; i++) {
		ret = one_copy(i);
		if (ret)
			break;
		cond_resched();
	}
	mutex_lock(&d->state.trial_lock);
	if (!vm->active_uses && !vm->tables->gpu_users) {
		int read_result = read_bo(vm->tables, 0, root_after, vm->capacity);
		roots_unchanged = !read_result && !memcmp(root_before, root_after, vm->capacity);
		if (!ret && !roots_unchanged)
			ret = read_result ? read_result : -EUCLEAN;
	}
	mutex_unlock(&d->state.trial_lock);
	result = ret;
	pr_info("mt_live_tqx_repeat: submitted=%u passed=%u result=%d root_unchanged=%d\n",
		submitted, passed, result, roots_unchanged);
	return ret;
}
static const struct kernel_param_ops run_ops = {.set = run_set};
module_param_cb(run, &run_ops, NULL, 0200);

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

static int __init repeat_init(void)
{
	int ret = -ENODEV;
	(void)mt_fw_event_io_ops;
	if (!enable)
		return -EPERM;
	workspace = kvzalloc(sizeof(*workspace), GFP_KERNEL);
	root_before = kvzalloc(131072, GFP_KERNEL);
	root_after = kvzalloc(131072, GFP_KERNEL);
	if (!workspace || !root_before || !root_after) {
		ret = -ENOMEM;
		goto free_memory;
	}
	device = mt_guest_find_s3000();
	if (!device)
		goto free_memory;
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
	    !d->markers.ops || !d->markers.ops->submit_tqx_work)
		goto put_owner;
	mutex_lock(&d->state.trial_lock);
	ret = attach_context();
	if (ret) {
		while (references)
			mt_bo_put(ordinary[--references]);
	} else {
		attached = true;
		result = 0;
	}
	mutex_unlock(&d->state.trial_lock);
	if (!ret) {
		device_unlock(&device->dev);
		pr_info("mt_live_tqx_repeat: attached=1 root=%llx cores=%u no_submission\n",
			vm->tables->backing.gpu_pa, cores);
		return 0;
	}
put_owner:
	module_put(owner);
unlock_device:
	device_unlock(&device->dev);
	pci_dev_put(device);
free_memory:
	kvfree(root_after);
	kvfree(root_before);
	kvfree(workspace);
	return ret;
}
static void __exit repeat_exit(void)
{
	mutex_lock(&d->state.trial_lock);
	while (references)
		mt_bo_put(ordinary[--references]);
	mutex_unlock(&d->state.trial_lock);
	module_put(owner);
	pci_dev_put(device);
	kvfree(root_after);
	kvfree(root_before);
	kvfree(workspace);
}
module_init(repeat_init);
module_exit(repeat_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Bounded repeat-copy validation on retained TQX context");
