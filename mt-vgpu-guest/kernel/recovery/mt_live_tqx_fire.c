// SPDX-License-Identifier: GPL-2.0
/* Live TQX fill fire on its own render node (r282).
 *
 * Why a separate node: the default bridge's renderD128 is held open by the
 * session desktop itself (fd survives kills; systemd relaunches it), so the
 * bridge can no longer be reloaded on the live session (r281). This module
 * never touches the bridge: it acquires the live probe session directly
 * (pci_get_drvdata + try_module_get, the mt_live_tqx pattern), builds its
 * own 64-page space + TQX context + slices, registers its own render node
 * (driver "mtlivefire", no ioctls), and fires the dry-run-verified fill
 * (r226 digest) in scratch-sized row strips. Trigger is the root-only
 * `run` param (mt_live_tqx pattern); all waits are bounded and outside
 * trial_lock; single-shot (second run gets -EBUSY). One live module at a
 * time; rmmod after the run tears everything down.
 */
#include "../mt_guest_device.h"
#include "../mt_addr_plan.h"
#include "../mt_transfer_fill.h"
#include "../mt_tqx_fill_work.h"
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_ioctl.h>

#define MT_FIRE_MAX_CHUNKS 64U

static bool enable;
module_param(enable, bool, 0400);
MODULE_PARM_DESC(enable, "opt-in: acquire session and bring up fire context at load");

/* Fire rectangle inputs (0400): defaults are the r226 dry-run values. */
static u32 width = 1280U;
module_param(width, uint, 0400);
static u32 height = 1024U;
module_param(height, uint, 0400);
static u32 color = 0xff0000ffU;
module_param(color, uint, 0400);

/* Read-only outcome (0444). */
static bool prepared, slices_ready, fired, verified;
module_param(prepared, bool, 0444);
module_param(slices_ready, bool, 0444);
module_param(fired, bool, 0444);
module_param(verified, bool, 0444);
static u32 chunks, chunk_h, bad, first, last;
module_param(chunks, uint, 0444);
module_param(chunk_h, uint, 0444);
module_param(bad, uint, 0444);
module_param(first, uint, 0444);
module_param(last, uint, 0444);
static int result = -ENODATA;
module_param(result, int, 0444);

static struct pci_dev *device;
static struct module *owner;
static struct mt_guest_device *d;
static struct drm_device *drm;
static struct mt_vm_vram *vm;
static struct mt_execution_process process;
static struct mt_execution_context tqx_ctx;
static struct mt_bo cmd_bo, dma_bo, state_bo, tmp_src, tmp_dst, scratch;
static struct mt_tqx_submission_workspace *workspace;
static struct mt_tqx_fill_workspace *fill_ws;
static bool attempted;

static_assert(sizeof(struct mt_guest_device) == 30784);
static_assert(offsetof(struct mt_guest_device, markers) == 29888);

/* Module-local upload device for TQX prepares (bridge WRITE_ONCE pattern).
 * Valid only inside prepare/fire paths serialized by single-shot + init.
 */
static struct mt_guest_device *upload_dev;

static int fire_bo_write(struct mt_bo *bo, u64 off, const void *src,
			 u64 bytes)
{
	struct mt_guest_device *dev = READ_ONCE(upload_dev);
	struct mt_bo_vram_handle *handle;
	void *mapping;
	int ret;

	if (!dev)
		return -ENODEV;
	if (bo->store != &dev->buffers || bo->ops != dev->buffers.ops)
		return -EXDEV;
	ret = mt_bo_check_range(bo, off, bytes);
	if (ret)
		return ret;
	ret = mt_bo_cpu_begin(bo, &mapping);
	if (ret)
		return ret;
	handle = bo->backing.handle;
	if (handle->system)
		memcpy((u8 *)mapping + off, src, bytes);
	else
		memcpy_toio((void __iomem *)mapping + off, src, bytes);
	return mt_bo_cpu_end(bo);
}

static int fire_bo_read(struct mt_bo *bo, u64 off, void *dst, u64 bytes)
{
	struct mt_guest_device *dev = READ_ONCE(upload_dev);
	struct mt_bo_vram_handle *handle;
	void *mapping;
	int ret;

	if (!dev)
		return -ENODEV;
	if (bo->store != &dev->buffers || bo->ops != dev->buffers.ops)
		return -EXDEV;
	ret = mt_bo_check_range(bo, off, bytes);
	if (ret)
		return ret;
	ret = mt_bo_cpu_begin(bo, &mapping);
	if (ret)
		return ret;
	handle = bo->backing.handle;
	if (handle->system)
		memcpy(dst, (u8 *)mapping + off, bytes);
	else
		memcpy_fromio(dst, (void __iomem *)mapping + off, bytes);
	return mt_bo_cpu_end(bo);
}

static const struct mt_tqx_upload_ops upload = {
	.write = fire_bo_write,
	.read = fire_bo_read,
};

/* Pool-slices bring-up (mirrors the bridge translator slices, r261/r266).
 * trial_lock is NOT held here (r265: buffers->lock must never nest inside
 * trial_lock); the init path drops it across this call. Non-fatal.
 */
static void fire_slices(void)
{
	struct mt_tqx_work work = { 0 };
	struct mt_tqx_submission_input input = {
		.stream = {.copy = {MT_TQX_STREAM_SRC_VA,
				    MT_TQX_STREAM_DST_VA, 256},
			   .va = {MT_TQX_CMD_VA}},
		.dma_va = MT_TQX_DMA_VA, .state_va = MT_TQX_STATE_VA,
	};
	struct mt_bo *bos[5];
	u32 cores;
	int ret;

	ret = mt_tqx_topology_from_info(&cores, &d->gem.profile,
					(void *)d->state.info, PAGE_SIZE);
	if (ret || cores != d->gem.tqx_cores) {
		pr_info("mt_live_tqx_fire: slices: topology %d cores=%u\n",
			ret, cores);
		return;
	}
	bos[0] = &cmd_bo;
	bos[1] = &tmp_src;
	bos[2] = &tmp_dst;
	bos[3] = &dma_bo;
	bos[4] = &state_bo;
	WRITE_ONCE(upload_dev, d);
	mutex_lock(d->shared_boot.buffers->lock);
	ret = mt_tqx_work_prepare_from_pools(&work, &d->shared_boot,
			workspace, &upload, &d->gem.profile, cores,
			&tqx_ctx, bos, &input);
	mutex_unlock(d->shared_boot.buffers->lock);
	WRITE_ONCE(upload_dev, NULL);
	if (!ret)
		ret = mt_tqx_work_cancel(&work);
	if (!ret) {
		WRITE_ONCE(slices_ready, true);
		pr_info("mt_live_tqx_fire: slices: ready cores=%u\n", cores);
	} else {
		pr_info("mt_live_tqx_fire: slices: prepare %d\n", ret);
	}
}

DEFINE_DRM_GEM_FOPS(mtfire_fops);
static const struct drm_driver mtfire_driver = {
	.driver_features = DRIVER_GEM | DRIVER_RENDER,
	.fops = &mtfire_fops,
	.name = "mtlivefire",
	.desc = "MT live TQX fill fire on its own render node (no ioctls)",
	.major = 0, .minor = 1, .patchlevel = 0,
};

/* Bring-up: own space + BOs + process + TQX context + slices + seal.
 * trial_lock held on entry; dropped across slices (r265). Every failure
 * reports its source line (r275 lesson).
 */
static int prepare(void)
{
	static const u64 va[6] = { MT_TQX_CMD_VA, MT_TQX_DMA_VA,
		MT_TQX_STATE_VA, MT_TQX_STREAM_SRC_VA,
		MT_TQX_STREAM_DST_VA, MT_TQX_SCRATCH_VA };
	static const u32 bytes[6] = { MT_TQX_CMD_BO_BYTES,
		MT_TQX_DMA_BO_BYTES, MT_TQX_STATE_BO_BYTES,
		MT_TQX_STREAM_SLOT_BYTES, MT_TQX_STREAM_SLOT_BYTES,
		MT_TQX_SCRATCH_BYTES };
	struct mt_bo *bos[6] = { &cmd_bo, &dma_bo, &state_bo,
		&tmp_src, &tmp_dst, &scratch };
	u32 cores, i;
	int ret, fail_at = 0;

	ret = mt_tqx_topology_from_info(&cores, &d->gem.profile,
					(void *)d->state.info, PAGE_SIZE);
	if (ret || cores != d->gem.tqx_cores) {
		fail_at = __LINE__;
		goto out;
	}
	ret = d->address_spaces.ops->create(&d->address_spaces,
					    MT_TRANSLATE_SPACE_PAGES, &vm);
	if (ret) {
		fail_at = __LINE__;
		goto out;
	}
	for (i = 0; i < 6; i++) {
		ret = mt_bo_create(bos[i], d->buffers.ops, &d->buffers,
				   bytes[i], PAGE_SIZE);
		if (ret) {
			fail_at = __LINE__;
			goto out;
		}
		ret = d->address_spaces.ops->bind(vm, bos[i], va[i], 0,
						  bytes[i],
						  MT_GPU_MAP_DEFAULT);
		if (ret) {
			fail_at = __LINE__;
			goto out;
		}
	}
	ret = d->address_spaces.ops->bind_boot_shared(vm, &d->gem.profile);
	if (ret) {
		fail_at = __LINE__;
		goto out;
	}
	ret = d->address_spaces.ops->upload(vm);
	if (ret) {
		fail_at = __LINE__;
		goto out;
	}
	ret = d->address_spaces.ops->seal(vm);
	if (ret) {
		fail_at = __LINE__;
		goto out;
	}
	ret = mt_execution_process_create(&d->execution, &process, &vm->vm,
					  task_tgid_nr(current));
	if (ret) {
		fail_at = __LINE__;
		goto out;
	}
	ret = mt_execution_context_create(&tqx_ctx, &process, 1, 0);
	if (ret) {
		fail_at = __LINE__;
		goto out;
	}
	mutex_unlock(&d->state.trial_lock);
	fire_slices();
	mutex_lock(&d->state.trial_lock);
	if (!slices_ready) {
		ret = -ENETDOWN;
		fail_at = __LINE__;
		goto out;
	}
	pr_info("mt_live_tqx_fire: prepared=1 cores=%u pages=%u\n",
		cores, vm->vm.used_pages);
	return 0;
out:
	if (ret)
		pr_info("mt_live_tqx_fire: prepare failed at line %d: %d\n",
			fail_at, ret);
	return ret;
}

static void release_all(void)
{
	u32 i;
	struct mt_bo *bos[6] = { &cmd_bo, &dma_bo, &state_bo,
		&tmp_src, &tmp_dst, &scratch };

	if (tqx_ctx.process) {
		if (slices_ready)
			WARN_ON(mt_tqx_context_pool_slices_release(&tqx_ctx));
		WARN_ON(mt_execution_context_destroy(&tqx_ctx));
	}
	if (process.store)
		WARN_ON(mt_execution_process_destroy(&process));
	if (vm)
		WARN_ON(d->address_spaces.ops->destroy(vm));
	for (i = 0; i < 6; i++)
		if (bos[i]->refs)
			WARN_ON(mt_bo_put(bos[i]));
}

/* Fire the configured rect in scratch-sized strips (r280 chunk math).
 * Root-only param write; single-shot. Submits take trial_lock one at a
 * time; all fence waits happen after unlock with the 5s fence budget;
 * a failed chunk stops the loop without piling more work (redline: timeout
 * means stop). Only the last chunk's content is verified (earlier chunks
 * are overwritten at the reused scratch base; their fences prove they ran).
 */
static int run_set(const char *value, const struct kernel_param *param)
{
	struct dma_fence *fences[MT_FIRE_MAX_CHUNKS] = { NULL };
	struct mt_tqx_fill_input fi;
	struct mt_bo *bos[4];
	u64 row_bytes;
	u32 rows_per_chunk = 0, nchunks = 0, c, pixels = 0;
	bool run;
	long waited;
	u32 i, mismatch = 0;
	u32 w0 = 0, wN = 0;
	int ret;

	(void)param;
	if (kstrtobool(value, &run) || !run)
		return -EINVAL;
	if (!READ_ONCE(prepared))
		return -ENODEV;
	if (xchg(&attempted, true))
		return -EBUSY;
	if (!READ_ONCE(slices_ready)) {
		ret = -ENETDOWN;
		goto done;
	}
	if (!width || !height) {
		ret = -EINVAL;
		goto done;
	}
	row_bytes = (u64)width * MT_TRANSFER_PIXEL_BYTES;
	if (!row_bytes || row_bytes > MT_TQX_SCRATCH_BYTES) {
		ret = -E2BIG;
		goto done;
	}
	rows_per_chunk = (u32)(MT_TQX_SCRATCH_BYTES / row_bytes);
	nchunks = (height + rows_per_chunk - 1) / rows_per_chunk;
	if (!nchunks || nchunks > MT_FIRE_MAX_CHUNKS) {
		ret = -E2BIG;
		goto done;
	}
	mutex_lock(&d->state.trial_lock);
	if (d->markers.total || d->markers.ready || d->markers.work_ready) {
		ret = -EBUSY;
		goto unlock;
	}
	ret = d->markers.can_submit(&d->state);
	if (ret)
		goto unlock;
	bos[0] = &cmd_bo;
	bos[1] = &scratch;
	bos[2] = &dma_bo;
	bos[3] = &state_bo;
	for (c = 0; c < nchunks; c++) {
		struct mt_tqx_work work = { 0 };
		u32 rows = min(rows_per_chunk, height - c * rows_per_chunk);

		memset(&work, 0, sizeof(work));
		fi = (struct mt_tqx_fill_input){
			.destination_va = MT_TQX_SCRATCH_VA,
			.command_va = MT_TQX_CMD_VA,
			.element_bytes = MT_TRANSFER_PIXEL_BYTES,
			.width = width, .height = rows,
			.x = 0, .y = 0, .rect_width = width,
			.rect_height = rows,
			.color = { color, 0, 0, 0 },
		};
		/* buffers->lock without trial_lock (r265); the upload path
		 * uses the module-local device pointer, never file objects.
		 */
		mutex_unlock(&d->state.trial_lock);
		WRITE_ONCE(upload_dev, d);
		mutex_lock(d->shared_boot.buffers->lock);
		ret = mt_tqx_fill_work_prepare(&work, fill_ws, &upload,
				&d->shared_boot, &d->gem.profile,
				d->gem.tqx_cores, &tqx_ctx, bos, &fi,
				MT_TQX_DMA_VA, MT_TQX_STATE_VA);
		mutex_unlock(d->shared_boot.buffers->lock);
		WRITE_ONCE(upload_dev, NULL);
		mutex_lock(&d->state.trial_lock);
		if (ret) {
			if (work.context)
				WARN_ON(mt_tqx_work_cancel(&work));
			break;
		}
		d->markers.ready = true;
		d->markers.work_ready = true;
		ret = d->markers.ops->submit_tqx_work(&d->markers, &work,
						      &fences[c]);
		d->markers.work_ready = false;
		d->markers.ready = false;
		if (ret) {
			if (work.context)
				WARN_ON(mt_tqx_work_cancel(&work));
			break;
		}
		if (c == nchunks - 1)
			pixels = width * rows;
	}
unlock:
	mutex_unlock(&d->state.trial_lock);
	if (ret)
		goto put;
	for (c = 0; c < nchunks; c++) {
		waited = dma_fence_wait_timeout(fences[c], false,
				msecs_to_jiffies(MT_TRANSLATE_FENCE_WAIT_MS));
		ret = waited > 0 ? dma_fence_get_status(fences[c]) :
			(waited < 0 ? (int)waited : -ETIMEDOUT);
		if (ret != 1)
			break;
		ret = 0;
	}
	if (!ret && pixels) {
		u32 v;

		mutex_lock(&d->state.trial_lock);
		for (i = 0; i < pixels; i++) {
			ret = fire_bo_read(&scratch,
					    (u64)i * sizeof(u32),
					    &v, sizeof(v));
			if (ret)
				break;
			if (i == 0)
				w0 = v;
			wN = v;
			if (v != color) {
				if (!mismatch)
					mismatch = i;
				bad++;
			}
		}
		mutex_unlock(&d->state.trial_lock);
		if (!ret && bad)
			ret = -EILSEQ;
	}
put:
	for (c = 0; c < nchunks; c++) {
		if (fences[c]) {
			dma_fence_put(fences[c]);
			fences[c] = NULL;
		}
	}
done:
	chunks = nchunks;
	chunk_h = rows_per_chunk;
	first = w0;
	last = wN;
	if (!ret) {
		WRITE_ONCE(fired, true);
		WRITE_ONCE(verified, !bad && pixels);
	}
	result = ret;
	pr_info("mt_live_tqx_fire: fired=%d chunks=%u verified=%d bad=%u/%u first=%#x last=%#x result=%d\n",
		fired, nchunks, verified, bad, pixels, w0, wN, ret);
	return ret;
}
static const struct kernel_param_ops run_ops = { .set = run_set };
module_param_cb(run, &run_ops, NULL, 0200);

static int __init mt_live_tqx_fire_init(void)
{
	int ret = -ENODEV;

	(void)mt_fw_event_io_ops;
	if (!enable)
		return -EPERM;
	workspace = kvzalloc(sizeof(*workspace), GFP_KERNEL);
	fill_ws = kvzalloc(sizeof(*fill_ws), GFP_KERNEL);
	if (!workspace || !fill_ws) {
		ret = -ENOMEM;
		goto free_workspace;
	}
	device = mt_guest_find_s3000();
	if (!device)
		goto free_workspace;
	device_lock(&device->dev);
	if (!mt_guest_match_s3000(device->vendor, device->device,
				  device->subsystem_vendor,
				  device->subsystem_device) ||
	    !device->driver ||
	    strcmp(device->driver->name, MT_GUEST_DRIVER_NAME))
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
	if (!d->runtime.published || d->runtime.event_result ||
	    !d->state.trial.pinned || !d->state.trial.connected ||
	    !d->service.running ||
	    d->markers.lock != &d->state.trial_lock ||
	    d->markers.opaque != &d->state ||
	    d->markers.total || d->markers.ready || d->markers.work_ready ||
	    !d->markers.completed || !d->markers.can_submit ||
	    !d->markers.ops || !d->markers.ops->submit_tqx_work ||
	    d->address_spaces.objects || d->buffers.objects)
		goto unlock_session;
	ret = d->markers.can_submit(&d->state);
	if (!ret)
		ret = prepare();
	if (ret)
		goto release_all;
	WRITE_ONCE(prepared, true);
	result = 0;
	mutex_unlock(&d->state.trial_lock);
	drm = drm_dev_alloc(&mtfire_driver, &device->dev);
	if (IS_ERR(drm)) {
		ret = PTR_ERR(drm);
		mutex_lock(&d->state.trial_lock);
		goto release_all;
	}
	ret = drm_dev_register(drm, 0);
	if (!ret) {
		device_unlock(&device->dev);
		pr_info("mt_live_tqx_fire: prepared on own render node\n");
		return 0;
	}
	drm_dev_put(drm);
	mutex_lock(&d->state.trial_lock);
release_all:
	release_all();
unlock_session:
	mutex_unlock(&d->state.trial_lock);
put_owner:
	module_put(owner);
unlock_device:
	device_unlock(&device->dev);
	pci_dev_put(device);
free_workspace:
	kvfree(workspace);
	kvfree(fill_ws);
	pr_info("mt_live_tqx_fire: preparation failed=%d no submission\n",
		ret);
	return ret;
}

static void __exit mt_live_tqx_fire_exit(void)
{
	if (drm) {
		drm_dev_unregister(drm);
		drm_dev_put(drm);
		drm = NULL;
	}
	if (!READ_ONCE(prepared))
		goto put;
	mutex_lock(&d->state.trial_lock);
	release_all();
	mutex_unlock(&d->state.trial_lock);
put:
	if (owner)
		module_put(owner);
	if (device)
		pci_dev_put(device);
	kvfree(workspace);
	kvfree(fill_ws);
}
module_init(mt_live_tqx_fire_init);
module_exit(mt_live_tqx_fire_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Live TQX fill fire on its own render node (r282)");
