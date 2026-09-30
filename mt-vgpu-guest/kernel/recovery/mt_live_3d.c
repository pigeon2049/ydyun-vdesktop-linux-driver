// SPDX-License-Identifier: GPL-2.0
/* Multi-Frame 3D Workload Execution & Stress Test on Data Master 2 (Universal Queue)
 * - Allocates and maps 11 3D context BOs (86,300 bytes, 29 pages)
 * - Fills initial state from verified mt_gfx_context_data.h
 * - Builds 248-byte CSW block pointing to established GPU VAs
 * - Prepares execution context (node_type=5, DM=2, Universal Queue)
 * - Submits batch/loop of N 3D workloads with dynamic frame metadata to DM2
 * - Measures latencies, captures sequence progression, verifies hardware ring stability
 * - Provides graceful teardown upon module unload
 */
#include <linux/delay.h>
#include <linux/ktime.h>
#include "../mt_guest_device.h"
#include "../mt_gfx_context.h"
#include "../mt_gfx_context_data.h"
#include "../mt_gfx_packet.h"
#include "../mt_gfx_packet_template.h"

static bool enable = true;
module_param(enable, bool, 0644);

static unsigned int count = 5;
module_param(count, uint, 0644);

static unsigned int delay_ms = 0;
module_param(delay_ms, uint, 0644);

static unsigned int completed_frames = 0;
module_param(completed_frames, uint, 0444);

static int last_result = -ENODATA;
module_param(last_result, int, 0444);

static unsigned long long last_sequence = 0;
module_param(last_sequence, ullong, 0444);

static unsigned int min_latency_us = 0;
module_param(min_latency_us, uint, 0444);

static unsigned int max_latency_us = 0;
module_param(max_latency_us, uint, 0444);

static unsigned int avg_latency_us = 0;
module_param(avg_latency_us, uint, 0444);

static_assert(sizeof(struct mt_guest_device) == 30784);
static_assert(offsetof(struct mt_guest_device, markers) == 29888);

struct mt_live_3d_state {
	struct pci_dev *pdev;
	struct module *owner;
	struct mt_guest *g;
	struct mt_guest_device *d;
	struct mt_vm_vram *space;
	struct mt_execution_process process;
	struct mt_execution_context context;
	struct mt_bo context_bos[MT_GFX_CONTEXT_BO_COUNT];
	u64 context_vas[MT_GFX_CONTEXT_BO_COUNT];
	struct mt_bo command_bo;
	u64 command_va;
	u8 csw[MT_GFX_CONTEXT_CSW_BYTES];
	bool resources_allocated;
};

static struct mt_live_3d_state *st;

static int mt_live_3d_write_bo(struct mt_guest_device *d, struct mt_bo *bo,
			       u64 off, const void *src, u64 bytes)
{
	struct mt_bo_vram_handle *handle;
	void *mapping;
	int ret;

	if (bo->store != &d->buffers || bo->ops != d->buffers.ops)
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

static void mt_live_3d_cleanup(struct mt_live_3d_state *s)
{
	u32 i;
	if (!s)
		return;

	if (s->resources_allocated) {
		mt_execution_context_destroy(&s->context);
		mt_execution_process_destroy(&s->process);
		for (i = 0; i < MT_GFX_CONTEXT_BO_COUNT; i++) {
			if (s->context_bos[i].refs)
				mt_bo_put(&s->context_bos[i]);
		}
		if (s->command_bo.refs)
			mt_bo_put(&s->command_bo);
		if (s->space)
			s->d->address_spaces.ops->destroy(s->space);
		s->resources_allocated = false;
	}

	if (s->g)
		mutex_unlock(&s->g->trial_lock);

	if (s->owner)
		module_put(s->owner);
	if (s->pdev) {
		device_unlock(&s->pdev->dev);
		pci_dev_put(s->pdev);
	}
	kfree(s);
}

static int __init mt_live_3d_init(void)
{
	struct pci_dev *pdev;
	struct module *owner = NULL;
	struct mt_guest *g;
	struct mt_guest_device *d;
	struct mt_marker_store *s;
	struct mt_gfx_context_bo_addresses addrs;
	struct mt_execution_request req;
	u64 total_lat_us = 0;
	u32 f, i;
	int ret = -ENODEV;
	(void)mt_fw_event_io_ops;

	if (!enable)
		return -EPERM;
	if (count == 0)
		count = 1;
	if (count > 1000)
		count = 1000;

	pdev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(14, 0));
	if (!pdev)
		return -ENODEV;
	device_lock(&pdev->dev);
	if (pdev->vendor != 0x1ed5 || pdev->device != 0x0222 ||
	    pdev->subsystem_vendor != 0x1ed5 || pdev->subsystem_device != 0x1101 ||
	    !pdev->driver || strcmp(pdev->driver->name, "mt_guest_probe")) {
		device_unlock(&pdev->dev);
		pci_dev_put(pdev);
		return -ENODEV;
	}

	owner = pdev->driver->driver.owner;
	if (!owner || !try_module_get(owner)) {
		device_unlock(&pdev->dev);
		pci_dev_put(pdev);
		return -EBUSY;
	}
	g = pci_get_drvdata(pdev);
	if (!g) {
		module_put(owner);
		device_unlock(&pdev->dev);
		pci_dev_put(pdev);
		return -ENODEV;
	}
	d = container_of(g, struct mt_guest_device, state);
	s = &d->markers;

	st = kzalloc(sizeof(*st), GFP_KERNEL);
	if (!st) {
		module_put(owner);
		device_unlock(&pdev->dev);
		pci_dev_put(pdev);
		return -ENOMEM;
	}
	st->pdev = pdev;
	st->owner = owner;
	st->g = g;
	st->d = d;

	mutex_lock(&g->trial_lock);

	/* Pre-conditions: active connection, no pending marker errors */
	if (!d->runtime.published || d->runtime.event_result ||
	    !g->trial.pinned || !g->trial.connected || !d->service.running ||
	    readl(g->regs + 0x890) != 2 || mt_trial_fw_state(&g->trial) != 2 ||
	    !mt_trial_started(&g->trial)) {
		pr_err("mt_live_3d: HW connection not ready: guest=%u fw=%u started=%u\n",
		       readl(g->regs + 0x890), mt_trial_fw_state(&g->trial), mt_trial_started(&g->trial));
		ret = -EHOSTDOWN;
		goto err_cleanup;
	}

	/* 1. Create VM address space */
	ret = d->address_spaces.ops->create(&d->address_spaces, 32, &st->space);
	if (ret) {
		pr_err("mt_live_3d: failed to create VM space: %d\n", ret);
		goto err_cleanup;
	}

	/* 2. Allocate and bind 11 context BOs with init data */
	for (i = 0; i < MT_GFX_CONTEXT_BO_COUNT; i++) {
		u32 bytes = mt_gfx_context_bo_specs[i].bytes;
		u32 alloc_size = PAGE_ALIGN(bytes);
		u64 va = 0x50000000ULL + i * 0x100000ULL; /* 1MB spacing */

		st->context_vas[i] = va;
		addrs.va[i] = va;

		ret = mt_bo_create(&st->context_bos[i], d->buffers.ops, &d->buffers, alloc_size, PAGE_SIZE);
		if (ret) {
			pr_err("mt_live_3d: failed to create BO %u: %d\n", i, ret);
			goto err_cleanup;
		}

		ret = mt_live_3d_write_bo(d, &st->context_bos[i], 0,
					  mt_gfx_bo_init_metas[i].data, bytes);
		if (ret) {
			pr_err("mt_live_3d: failed to write init data to BO %u: %d\n", i, ret);
			goto err_cleanup;
		}

		ret = d->address_spaces.ops->bind(st->space, &st->context_bos[i],
						  va, 0, alloc_size, MT_GPU_MAP_DEFAULT);
		if (ret) {
			pr_err("mt_live_3d: failed to bind BO %u to VA %#llx: %d\n", i, va, ret);
			goto err_cleanup;
		}
	}

	/* 3. Build 248-byte CSW block */
	ret = mt_gfx_context_build_csw(st->csw, sizeof(st->csw), &addrs);
	if (ret) {
		pr_err("mt_live_3d: failed to build CSW: %d\n", ret);
		goto err_cleanup;
	}

	/* 4. Allocate command BO (32 KiB) */
	st->command_va = 0x48000000ULL;
	ret = mt_bo_create(&st->command_bo, d->buffers.ops, &d->buffers, 32768, PAGE_SIZE);
	if (ret) {
		pr_err("mt_live_3d: failed to create command BO: %d\n", ret);
		goto err_cleanup;
	}

	/* Copy Linux minimal 3D packet template into command BO */
	ret = mt_live_3d_write_bo(d, &st->command_bo, 0, mt_gfx_linux_packet_template, MT_GFX_LINUX_PACKET_BYTES);
	if (ret) {
		pr_err("mt_live_3d: failed to write packet template: %d\n", ret);
		goto err_cleanup;
	}

	/* Fixup CSW GPU VA in envelope header: offset +0x10 points to command_va + 0x58 */
	{
		u64 csw_va = st->command_va + 0x58ULL;
		ret = mt_live_3d_write_bo(d, &st->command_bo, 0x10, &csw_va, 8);
		if (ret) {
			pr_err("mt_live_3d: failed to write CSW pointer: %d\n", ret);
			goto err_cleanup;
		}
	}

	/* Copy CSW into command BO at Linux CSW offset +0x58 */
	ret = mt_live_3d_write_bo(d, &st->command_bo, 0x58, st->csw, sizeof(st->csw));
	if (ret) {
		pr_err("mt_live_3d: failed to write CSW into command BO: %d\n", ret);
		goto err_cleanup;
	}

	/* Bind command BO to GPU VM */
	ret = d->address_spaces.ops->bind(st->space, &st->command_bo,
					  st->command_va, 0, 32768, MT_GPU_MAP_DEFAULT);
	if (ret) {
		pr_err("mt_live_3d: failed to bind command BO: %d\n", ret);
		goto err_cleanup;
	}

	/* 5. Bind boot shared memory & create execution process */
	ret = d->address_spaces.ops->bind_boot_shared(st->space, &d->gem.profile);
	if (ret) {
		pr_err("mt_live_3d: failed to bind boot shared: %d\n", ret);
		goto err_cleanup;
	}

	ret = mt_execution_process_create(&d->execution, &st->process, &st->space->vm, task_tgid_nr(current));
	if (ret) {
		pr_err("mt_live_3d: failed to create execution process: %d\n", ret);
		goto err_cleanup;
	}

	/* 6. Create execution context on node_type=5 (DM=2, Universal Queue) */
	ret = mt_execution_context_create(&st->context, &st->process, 5, 0);
	if (ret) {
		pr_err("mt_live_3d: failed to create 3D context (node=5): %d\n", ret);
		goto err_cleanup;
	}

	/* 7. Upload and seal page tables before HW submission */
	ret = d->address_spaces.ops->upload(st->space);
	if (ret) {
		pr_err("mt_live_3d: failed to upload VM space: %d\n", ret);
		goto err_cleanup;
	}

	ret = d->address_spaces.ops->seal(st->space);
	if (ret) {
		pr_err("mt_live_3d: failed to seal VM space: %d\n", ret);
		goto err_cleanup;
	}

	st->resources_allocated = true;
	mutex_unlock(&g->trial_lock);

	/* 8. Loop submit workloads to DM2 */
	req.command_va = st->command_va;
	req.bytes = MT_GFX_LINUX_PACKET_BYTES;
	req.type = 3;
	req.submit_flags = 0;

	pr_info("mt_live_3d: starting batch submission: %u frames, delay_ms=%u\n", count, delay_ms);
	min_latency_us = ~0U;
	max_latency_us = 0;
	completed_frames = 0;

	for (f = 0; f < count; f++) {
		struct dma_fence *fence = NULL;
		ktime_t t_start, t_end;
		u32 lat_us;
		long waited;
		u64 frame_tag = (u64)(f + 1);

		/* Dynamic variation: write frame_tag into Envelope header +0x08 */
		mt_live_3d_write_bo(d, &st->command_bo, 0x08, &frame_tag, 8);

		mutex_lock(&g->trial_lock);
		s->ready = true;
		s->work_ready = true;
		ret = s->ops->submit_context(s, &st->context, &st->command_bo, &req, &fence);
		s->work_ready = false;
		s->ready = false;
		mutex_unlock(&g->trial_lock);

		if (ret) {
			pr_err("mt_live_3d: submit frame %u failed: %d\n", f + 1, ret);
			last_result = ret;
			break;
		}

		t_start = ktime_get();
		last_sequence = fence->seqno;

		waited = dma_fence_wait_timeout(fence, false, msecs_to_jiffies(3000));
		t_end = ktime_get();
		lat_us = (u32)ktime_to_us(ktime_sub(t_end, t_start));

		ret = waited > 0 ? dma_fence_get_status(fence) :
			(waited < 0 ? (int)waited : -ETIMEDOUT);
		if (ret == 1)
			ret = 0;
		dma_fence_put(fence);

		if (ret) {
			pr_err("mt_live_3d: frame %u timeout/error: %d\n", f + 1, ret);
			last_result = ret;
			/* Rollback cursor on failure */
			{
				u32 cur = 2 * MT_FW_DM_BYTES + MT_FW_CURSOR_OFFSET;
				u32 tail = readl(g->firmware_queue.queue + cur + 8);
				mutex_lock(&g->trial_lock);
				writel(tail, g->firmware_queue.queue + cur);
				mb();
				if (!list_empty(&s->pending[2])) {
					struct mt_marker_fence *mf = list_first_entry(&s->pending[2], struct mt_marker_fence, link);
					list_del(&mf->link);
					if (s->count[2]) s->count[2]--;
					if (s->total) s->total--;
					dma_fence_set_error(&mf->fence, -ETIMEDOUT);
					dma_fence_signal(&mf->fence);
					dma_fence_put(&mf->fence);
				}
				mutex_unlock(&g->trial_lock);
			}
			break;
		}

		if (lat_us < min_latency_us) min_latency_us = lat_us;
		if (lat_us > max_latency_us) max_latency_us = lat_us;
		total_lat_us += lat_us;
		completed_frames++;
		last_result = 0;

		if (delay_ms > 0 && f + 1 < count)
			msleep(delay_ms);
	}

	if (completed_frames > 0)
		avg_latency_us = (u32)(total_lat_us / completed_frames);
	else
		min_latency_us = 0;

	pr_info("mt_live_3d: batch finished: completed=%u/%u result=%d latencies [min=%u avg=%u max=%u us]\n",
		completed_frames, count, last_result, min_latency_us, avg_latency_us, max_latency_us);

	return 0;

err_cleanup:
	st->resources_allocated = true;
	mt_live_3d_cleanup(st);
	st = NULL;
	return ret;
}

static void __exit mt_live_3d_exit(void)
{
	pr_info("mt_live_3d: exiting and cleaning up 3D execution resources\n");
	if (st) {
		mt_live_3d_cleanup(st);
		st = NULL;
	}
}

module_init(mt_live_3d_init);
module_exit(mt_live_3d_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Multi-frame 3D workload submission on DM2 for mt-vgpu-guest");
