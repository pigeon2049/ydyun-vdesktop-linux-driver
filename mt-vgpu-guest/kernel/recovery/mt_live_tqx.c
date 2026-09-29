// SPDX-License-Identifier: GPL-2.0
/* Bounded live experiment: prepare one 256-byte TQX copy, then explicitly
 * submit through the retained driver's ops. Sealed mappings stay retained,
 * including on timeout. This is not a public render or general command API. */
#include "../mt_guest_device.h"

static bool enable;
module_param(enable, bool, 0400);
static int result = -ENODATA;
module_param(result, int, 0444);
static bool prepared, submitted, verified;
module_param(prepared, bool, 0444);
module_param(submitted, bool, 0444);
module_param(verified, bool, 0444);
static unsigned long long sequence;
module_param(sequence, ullong, 0444);
static struct pci_dev *device;
static struct module *owner;
static struct mt_guest_device *d;
static struct mt_vm_vram *vm;
static struct mt_execution_process process;
static struct mt_execution_context context;
static struct mt_bo ordinary[5];
static struct mt_tqx_work work;
static struct mt_tqx_submission_workspace *workspace;
static u8 source[4096], observed[4096];
static bool attempted;

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

static int run_set(const char *value, const struct kernel_param *param)
{
	struct dma_fence *fence = NULL;
	struct mt_marker_store *markers;
	bool run;
	long waited;
	int ret;
	if (kstrtobool(value, &run) || !run)
		return -EINVAL;
	if (!READ_ONCE(prepared))
		return -ENODEV;
	markers = &d->markers;
	mutex_lock(&d->state.trial_lock);
	if (attempted || markers->total || markers->ready || markers->work_ready) {
		ret = -EBUSY;
		goto unlock;
	}
	ret = markers->can_submit(&d->state);
	if (ret)
		goto unlock;
	attempted = true;
	markers->ready = true;
	markers->work_ready = true;
	ret = markers->ops->submit_tqx_work(markers, &work, &fence);
	markers->work_ready = false;
	markers->ready = false;
	if (!ret) {
		submitted = true;
		sequence = fence->seqno;
	}
unlock:
	mutex_unlock(&d->state.trial_lock);
	if (ret)
		goto done;
	waited = dma_fence_wait_timeout(fence, false, msecs_to_jiffies(5000));
	ret = waited > 0 ? dma_fence_get_status(fence) :
		(waited < 0 ? (int)waited : -ETIMEDOUT);
	if (ret == 1) {
		mutex_lock(&d->state.trial_lock);
		ret = read_bo(&ordinary[2], 0, observed, sizeof(observed));
		if (!ret && (memcmp(observed, source, 256) ||
		    memchr_inv(observed + 256, 0xa5, sizeof(observed) - 256)))
			ret = -EILSEQ;
		if (!ret)
			ret = read_bo(&ordinary[1], 0, observed, sizeof(observed));
		if (!ret && memcmp(observed, source, sizeof(source)))
			ret = -EILSEQ;
		verified = !ret;
		mutex_unlock(&d->state.trial_lock);
	}
done:
	dma_fence_put(fence);
	result = ret;
	pr_info("mt_live_tqx: submitted=%d sequence=%llu result=%d verified=%d retained=1\n",
		submitted, sequence, result, verified);
	return ret;
}
static const struct kernel_param_ops run_ops = { .set = run_set };
module_param_cb(run, &run_ops, NULL, 0200);

static int prepare(void)
{
	const u64 va[5] = {0x40000000, 0x40100000, 0x40200000, 0x40010000, 0x40020000};
	const u32 bytes[5] = {4096, 4096, 4096, 8192, 4096};
	struct mt_tqx_submission_input input = {
		.stream = {.copy = {0x40100000, 0x40200000, 256}, .va = {0x40000000}},
		.dma_va = 0x40010000, .state_va = 0x40020000,
	};
	struct mt_bo *bos[5];
	u32 i, cores;
	int ret;
	ret = mt_tqx_topology_from_info(&cores, &d->gem.profile, (void *)d->state.info, PAGE_SIZE);
	if (ret || cores != d->gem.tqx_cores)
		return ret ? ret : -EINVAL;
	ret = d->address_spaces.ops->create(&d->address_spaces, 32, &vm);
	if (ret)
		return ret;
	for (i = 0; i < 5; i++) {
		bos[i] = &ordinary[i];
		ret = mt_bo_create(bos[i], d->buffers.ops, &d->buffers, bytes[i], PAGE_SIZE);
		if (ret)
			return ret;
		ret = d->address_spaces.ops->bind(vm, bos[i], va[i], 0, bytes[i], MT_GPU_MAP_DEFAULT);
		if (ret)
			return ret;
		pr_info("mt_live_tqx: bo=%u va=%llx gpu_pa=%llx bar_offset=%llx bytes=%u\n",
			i, va[i], bos[i]->backing.gpu_pa, bos[i]->backing.bar_offset, bytes[i]);
	}
	ret = d->address_spaces.ops->bind_boot_shared(vm, &d->gem.profile);
	if (!ret)
		ret = mt_execution_process_create(&d->execution, &process, &vm->vm, task_tgid_nr(current));
	if (!ret)
		ret = mt_execution_context_create(&context, &process, 1, 0);
	if (ret)
		return ret;
	for (i = 0; i < sizeof(source); i++)
		source[i] = (u8)((i * 73 + 19) ^ (i >> 3));
	memset(observed, 0xa5, sizeof(observed));
	ret = write_bo(&ordinary[2], 0, observed, sizeof(observed));
	if (!ret)
		ret = read_bo(&ordinary[2], 0, observed, sizeof(observed));
	if (!ret && memchr_inv(observed, 0xa5, sizeof(observed)))
		ret = -EIO;
	if (!ret)
		ret = write_bo(&ordinary[1], 0, source, sizeof(source));
	if (!ret)
		ret = read_bo(&ordinary[1], 0, observed, sizeof(observed));
	if (!ret && memcmp(source, observed, sizeof(source)))
		ret = -EIO;
	if (!ret)
		ret = mt_tqx_work_prepare_from_pools(&work, &d->shared_boot, workspace,
			&upload, &d->gem.profile, cores, &context, bos, &input);
	if (!ret)
		ret = d->address_spaces.ops->seal(vm);
	if (!ret)
		pr_info("mt_live_tqx: prepared=1 cores=%u root=%llx pages=%u mappings=%u pins=%u token=%llu\n",
			cores, vm->tables.backing.gpu_pa, vm->vm.used_pages, vm->vm.count,
			work.job.count, process.token);
	return ret;
}

static int __init mt_live_tqx_init(void)
{
	int ret = -ENODEV;
	u32 i;
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
	if (!owner || !try_module_get(owner)) {
		owner = NULL;
		goto unlock_device;
	}
	d = pci_get_drvdata(device); /* state is the first member. */
	if (!d)
		goto put_owner;
	mutex_lock(&d->state.trial_lock);
	ret = -EBUSY;
	if (!d->runtime.published || d->runtime.event_result || !d->state.trial.pinned ||
	    !d->state.trial.connected || !d->service.running ||
	    d->markers.lock != &d->state.trial_lock || d->markers.opaque != &d->state ||
	    d->markers.total || d->markers.ready || d->markers.work_ready ||
	    !d->markers.completed || !d->markers.can_submit ||
	    !d->markers.ops || !d->markers.ops->submit_tqx_work ||
	    d->address_spaces.objects || d->buffers.objects)
		goto unlock_session;
	ret = d->markers.can_submit(&d->state);
	if (!ret)
		ret = prepare();
	if (!ret) {
		/* Both the root and callback-referenced context have stable lifetime.
		 * Retain even after successful completion: root withdrawal is unknown. */
		__module_get(THIS_MODULE);
		WRITE_ONCE(prepared, true);
		result = 0;
		mutex_unlock(&d->state.trial_lock);
		device_unlock(&device->dev);
		return 0;
	}
	if (work.context)
		mt_tqx_work_cancel(&work);
	if (context.process) {
		mt_tqx_context_pool_slices_release(&context);
		mt_execution_context_destroy(&context);
	}
	if (process.store)
		mt_execution_process_destroy(&process);
	if (vm)
		d->address_spaces.ops->destroy(vm);
	for (i = 0; i < 5; i++)
		if (ordinary[i].refs)
			mt_bo_put(&ordinary[i]);
unlock_session:
	mutex_unlock(&d->state.trial_lock);
put_owner:
	module_put(owner);
unlock_device:
	device_unlock(&device->dev);
	pci_dev_put(device);
free_workspace:
	kvfree(workspace);
	pr_info("mt_live_tqx: preparation failed=%d no submission\n", ret);
	return ret;
}
static void __exit mt_live_tqx_exit(void) {}
module_init(mt_live_tqx_init);
module_exit(mt_live_tqx_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Retained one-shot TQX copy experiment");
