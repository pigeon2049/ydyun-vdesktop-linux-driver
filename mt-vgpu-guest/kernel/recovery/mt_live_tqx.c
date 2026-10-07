// SPDX-License-Identifier: GPL-2.0
/* Bounded live experiment: prepare one 256-byte TQX copy, then explicitly
 * submit through the retained driver's ops. Optional dma_source backs the
 * source BO with one DMA-mapped system page. Sealed mappings stay retained,
 * including on timeout. This is not a public render or general command API. */
#include "../mt_guest_device.h"
#include "../mt_addr_plan.h"
#include <linux/dma-mapping.h>

static bool enable;
module_param(enable, bool, 0400);
static int result = -ENODATA;
module_param(result, int, 0444);
static bool prepared, submitted, verified;
module_param(prepared, bool, 0444);
module_param(submitted, bool, 0444);
module_param(verified, bool, 0444);
static bool destination_verified, source_verified;
module_param(destination_verified, bool, 0444);
module_param(source_verified, bool, 0444);
static u32 destination_first_word, source_first_word, dma_cpu_first_word;
static u32 destination_mismatch_offset = U32_MAX, source_mismatch_offset = U32_MAX;
module_param(destination_first_word, uint, 0444);
module_param(source_first_word, uint, 0444);
module_param(dma_cpu_first_word, uint, 0444);
module_param(destination_mismatch_offset, uint, 0444);
module_param(source_mismatch_offset, uint, 0444);
static unsigned long long sequence;
module_param(sequence, ullong, 0444);
static bool dma_source;
module_param(dma_source, bool, 0400);
MODULE_PARM_DESC(dma_source, "Read TQX source bytes from one DMA-mapped system-RAM page");
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
static struct mt_system_memory dma_source_memory;
static struct mt_system_address dma_source_address;
static dma_addr_t *dma_source_dma_addrs;
static u32 dma_source_mapped_pages;
static bool attempted;

static u32 source_difference(const u8 *actual)
{
	u32 i;

	for (i = 0; i < sizeof(source); i++)
		if (actual[i] != source[i])
			return i;
	return U32_MAX;
}

static u32 destination_difference(const u8 *actual)
{
	u32 i;

	for (i = 0; i < 256; i++)
		if (actual[i] != source[i])
			return i;
	for (; i < sizeof(observed); i++)
		if (actual[i] != 0xa5)
			return i;
	return U32_MAX;
}

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

/* Manufacture a borrowed system-RAM BO in the main module's store. Its page
 * list carries GPU PAs translated from Guest physical pages through the
 * negotiated system window; the separate DMA API addresses are retained for
 * sync/unmap. Pin the main module separately: its BO free callback drops one
 * owner reference.
 */
static int dma_source_bo_create(struct mt_bo *bo)
{
	struct mt_bo_vram_handle *handle;
	u32 i, bytes = PAGE_SIZE;
	int ret = -ENOMEM;

	lockdep_assert_held(&d->state.trial_lock);
	if (!bo || bo->refs || !d->buffers.ops || !d->buffers.ops->free ||
	    !d->buffers.ops->map || !owner || !device || !d->state.info ||
	    !d->runtime.windows || bytes != sizeof(source))
		return -EINVAL;
	/* vzalloc guarantees page alignment; dma_map_page must map the same page
	 * that the CPU pattern occupies, not the containing slab page at offset 0.
	 */
	dma_source_memory.cpu = vzalloc(bytes);
	dma_source_memory.page_pa = kvcalloc(1, sizeof(*dma_source_memory.page_pa),
					    GFP_KERNEL);
	dma_source_dma_addrs = kvcalloc(1, sizeof(*dma_source_dma_addrs), GFP_KERNEL);
	if (!dma_source_memory.cpu || !dma_source_memory.page_pa ||
	    !dma_source_dma_addrs)
		goto fail;
	if (!IS_ALIGNED((unsigned long)dma_source_memory.cpu, PAGE_SIZE)) {
		ret = -EINVAL;
		goto fail;
	}
	dma_source_memory.bytes = bytes;
	memcpy(dma_source_memory.cpu, source, bytes);
	ret = mt_system_address_init(&dma_source_address,
		(void *)d->state.info, PAGE_SIZE, d->runtime.windows,
		MT_GUEST_WINDOWS_BYTES, pci_resource_start(device, 4),
		pci_resource_len(device, 4));
	if (ret)
		goto fail;
	for (i = 0; i < bytes / PAGE_SIZE; i++) {
		void *cpu_page = (u8 *)dma_source_memory.cpu + i * PAGE_SIZE;
		struct page *page = is_vmalloc_addr(cpu_page) ?
			vmalloc_to_page(cpu_page) : virt_to_page(cpu_page);
		u64 gpu_pa;
		dma_addr_t addr;

		if (!page) {
			ret = -EFAULT;
			goto fail;
		}
		ret = mt_system_page_address(&dma_source_address,
			page_to_phys(page), &gpu_pa);
		if (ret)
			goto fail;
		addr = dma_map_page(&device->dev, page, 0, PAGE_SIZE,
				    DMA_BIDIRECTIONAL);
		if (dma_mapping_error(&device->dev, addr)) {
			ret = -EIO;
			goto fail;
		}
		/* dma_addr_t is the PCI DMA API mapping, while the GPU page table
		 * consumes the Guest system-page GPU PA translated through the
		 * negotiated windows. Keep both domains explicit for readback.
		 */
		dma_source_dma_addrs[i] = addr;
		dma_source_memory.page_pa[i] = gpu_pa;
		dma_source_mapped_pages++;
		if (!IS_ALIGNED(addr, PAGE_SIZE) ||
		    !IS_ALIGNED(gpu_pa, PAGE_SIZE) ||
		    gpu_pa >= (1ULL << MT_GPU_VA_BITS)) {
			ret = -ERANGE;
			goto fail;
		}
	}
	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		goto fail;
	if (!try_module_get(owner)) {
		kfree(handle);
		ret = -ENODEV;
		goto fail;
	}
	handle->system = &dma_source_memory;
	handle->borrowed = true;
	d->buffers.objects++;
	d->buffers.allocated_bytes += bytes;
	*bo = (struct mt_bo){
		.backing = {.handle = handle,
			.gpu_pa = dma_source_memory.page_pa[0], .bytes = bytes},
		.ops = d->buffers.ops,
		.store = &d->buffers,
		.requested_bytes = bytes,
		.refs = 1,
		.page_pa = dma_source_memory.page_pa,
	};
	pr_info("mt_live_tqx: system source GPA=%#llx DMA IOVA=%#llx GPU PA=%#llx cpu_page_offset=%lu bytes=%u\n",
		page_to_phys(vmalloc_to_page(dma_source_memory.cpu)),
		dma_source_dma_addrs[0], dma_source_memory.page_pa[0],
		offset_in_page(dma_source_memory.cpu), bytes);
	return 0;
fail:
	while (dma_source_mapped_pages)
		dma_unmap_page(&device->dev,
			dma_source_dma_addrs[--dma_source_mapped_pages],
			PAGE_SIZE, DMA_BIDIRECTIONAL);
	kvfree(dma_source_memory.cpu);
	kvfree(dma_source_memory.page_pa);
	kvfree(dma_source_dma_addrs);
	memset(&dma_source_memory, 0, sizeof(dma_source_memory));
	memset(&dma_source_address, 0, sizeof(dma_source_address));
	dma_source_dma_addrs = NULL;
	return ret;
}

/* Successful retained runs leave the mapping and its source page alive because
 * the sealed root still names it. Failed preparation calls this only after
 * destroying the VM and dropping the borrowed BO reference. Unmap the DMA
 * API addresses (dma_source_dma_addrs), never the GPU page-table addresses
 * in page_pa: the two domains split in the GPA-window fix, and unmapping a
 * GPU PA would hand the DMA API an address it never mapped.
 */
static void dma_source_release(void)
{
	while (dma_source_mapped_pages)
		dma_unmap_page(&device->dev,
			dma_source_dma_addrs[--dma_source_mapped_pages],
			PAGE_SIZE, DMA_BIDIRECTIONAL);
	kvfree(dma_source_memory.cpu);
	kvfree(dma_source_memory.page_pa);
	kvfree(dma_source_dma_addrs);
	memset(&dma_source_memory, 0, sizeof(dma_source_memory));
	memset(&dma_source_address, 0, sizeof(dma_source_address));
	dma_source_dma_addrs = NULL;
}

static int run_set(const char *value, const struct kernel_param *param)
{
	struct dma_fence *fence = NULL;
	struct mt_marker_store *markers;
	bool run;
	long waited;
	u32 i;
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
		int source_ret;

		mutex_lock(&d->state.trial_lock);
		if (dma_source)
			for (i = 0; i < dma_source_mapped_pages; i++)
				dma_sync_single_for_cpu(&device->dev,
					dma_source_dma_addrs[i], PAGE_SIZE,
					DMA_BIDIRECTIONAL);
		if (dma_source)
			memcpy(&dma_cpu_first_word, dma_source_memory.cpu,
			       sizeof(dma_cpu_first_word));
		ret = read_bo(&ordinary[2], 0, observed, sizeof(observed));
		if (!ret) {
			memcpy(&destination_first_word, observed,
			       sizeof(destination_first_word));
			destination_mismatch_offset = destination_difference(observed);
			destination_verified = destination_mismatch_offset == U32_MAX;
			source_ret = read_bo(&ordinary[1], 0, observed, sizeof(observed));
			if (source_ret) {
				ret = source_ret;
			} else {
				memcpy(&source_first_word, observed, sizeof(source_first_word));
				source_mismatch_offset = source_difference(observed);
				source_verified = source_mismatch_offset == U32_MAX;
				if (!destination_verified || !source_verified)
					ret = -EILSEQ;
			}
		}
		verified = !ret;
		mutex_unlock(&d->state.trial_lock);
	}
done:
	dma_fence_put(fence);
	result = ret;
	pr_info("mt_live_tqx: submitted=%d sequence=%llu result=%d verified=%d dma_source=%u destination_ok=%u destination_word=%#x destination_mismatch=%u source_ok=%u source_word=%#x source_mismatch=%u dma_cpu_word=%#x retained=1\n",
		submitted, sequence, result, verified, dma_source,
		destination_verified, destination_first_word,
		destination_mismatch_offset, source_verified, source_first_word,
		source_mismatch_offset, dma_cpu_first_word);
	return ret;
}
static const struct kernel_param_ops run_ops = { .set = run_set };
module_param_cb(run, &run_ops, NULL, 0200);

static int prepare(void)
{
	const u64 va[5] = {MT_TQX_CMD_VA, MT_TQX_STREAM_SRC_VA, MT_TQX_STREAM_DST_VA, MT_TQX_DMA_VA, MT_TQX_STATE_VA};
	const u32 bytes[5] = {MT_TQX_CMD_BO_BYTES, MT_TQX_STREAM_SLOT_BYTES, MT_TQX_STREAM_SLOT_BYTES, MT_TQX_DMA_BO_BYTES, MT_TQX_STATE_BO_BYTES};
	struct mt_tqx_submission_input input = {
		.stream = {.copy = {MT_TQX_STREAM_SRC_VA, MT_TQX_STREAM_DST_VA, 256}, .va = {MT_TQX_CMD_VA}},
		.dma_va = MT_TQX_DMA_VA, .state_va = MT_TQX_STATE_VA,
	};
	struct mt_bo *bos[5];
	u32 i, cores;
	int ret;
	for (i = 0; i < sizeof(source); i++)
		source[i] = (u8)((i * 73 + 19) ^ (i >> 3));
	ret = mt_tqx_topology_from_info(&cores, &d->gem.profile, (void *)d->state.info, PAGE_SIZE);
	if (ret || cores != d->gem.tqx_cores)
		return ret ? ret : -EINVAL;
	ret = d->address_spaces.ops->create(&d->address_spaces, 32, &vm);
	if (ret)
		return ret;
	for (i = 0; i < 5; i++) {
		bos[i] = &ordinary[i];
		ret = dma_source && i == 1 ? dma_source_bo_create(bos[i]) :
			mt_bo_create(bos[i], d->buffers.ops, &d->buffers,
				     bytes[i], PAGE_SIZE);
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
	memset(observed, 0xa5, sizeof(observed));
	ret = write_bo(&ordinary[2], 0, observed, sizeof(observed));
	if (!ret)
		ret = read_bo(&ordinary[2], 0, observed, sizeof(observed));
	if (!ret && memchr_inv(observed, 0xa5, sizeof(observed)))
		ret = -EIO;
	if (!ret && !dma_source)
		ret = write_bo(&ordinary[1], 0, source, sizeof(source));
	if (!ret && !dma_source)
		ret = read_bo(&ordinary[1], 0, observed, sizeof(observed));
	if (!ret && !dma_source && memcmp(source, observed, sizeof(source)))
		ret = -EIO;
	if (!ret)
		ret = mt_tqx_work_prepare_from_pools(&work, &d->shared_boot, workspace,
			&upload, &d->gem.profile, cores, &context, bos, &input);
	if (!ret)
		ret = d->address_spaces.ops->seal(vm);
	if (!ret)
		pr_info("mt_live_tqx: prepared=1 cores=%u root=%llx pages=%u mappings=%u pins=%u token=%llu dma_source=%u\n",
			cores, vm->tables.backing.gpu_pa, vm->vm.used_pages, vm->vm.count,
			work.job.count, process.token, dma_source);
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
	device = mt_guest_find_s3000();
	if (!device)
		goto free_workspace;
	device_lock(&device->dev);
	if (!mt_guest_match_s3000(device->vendor, device->device, device->subsystem_vendor, device->subsystem_device) ||
	    !device->driver || strcmp(device->driver->name, MT_GUEST_DRIVER_NAME))
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
	if (dma_source_mapped_pages)
		dma_source_release();
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
