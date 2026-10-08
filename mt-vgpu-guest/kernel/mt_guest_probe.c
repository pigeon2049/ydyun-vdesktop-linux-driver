// SPDX-License-Identifier: GPL-2.0
/* Experimental Guest transport bring-up, not a DRM/render driver.
 * Reference: mtkm64.sys 140026b30 (information request), 140025a9c (free).
 * Host 2.3 vgpu_access_pci_bar1_region confirms a synchronous GPA->HVA copy.
 * The info page is not a DMA-engine buffer. Do not substitute an IOVA for GPA.
 */
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/vmalloc.h>
#include "mt_guest_state.h"
#include "mt_guest_announcements.h"
#include "mt_rpc_publish.h"
#include "mt_rpc_service.h"
#include "mt_runtime_context.h"
#include "mt_bo_vram.h"
#include "mt_vm_vram.h"
#include "mt_gem.h"
#include "mt_marker_fence.h"
#ifndef CONFIG_X86_64
#error This experimental transport has only been reviewed for x86_64.
#endif

static bool enable_probe;
module_param(enable_probe, bool, 0400);
MODULE_PARM_DESC(enable_probe, "Explicitly permit binding the investigated 0000:00:0e.0 device");
static bool query_info;
module_param(query_info, bool, 0400);
MODULE_PARM_DESC(query_info, "Issue one synchronous BAR1+0xc8 device-information request");
static bool probe_rpc;
module_param(probe_rpc, bool, 0400);
MODULE_PARM_DESC(probe_rpc, "Temporarily register four shared pages and query Host protocol mode");
static bool recover_channels;
module_param(recover_channels, bool, 0400);
MODULE_PARM_DESC(recover_channels, "Recover orphaned communication pages in Guest=1/FW=1; no firmware or VRAM writes");
static bool refresh_osid;
module_param(refresh_osid, bool, 0400);
MODULE_PARM_DESC(refresh_osid, "With recover_channels: once publish queried OSID to BAR1+0x110 and re-query information");
static bool snapshot_memory;
module_param(snapshot_memory, bool, 0400);
MODULE_PARM_DESC(snapshot_memory, "Read first 1 MiB of firmware segment and first 64 KiB of shared segment");
static bool reserve_memory;
module_param(reserve_memory, bool, 0400);
MODULE_PARM_DESC(reserve_memory, "Reserve BAR2 and allocate/map firmware, table and auxiliary ranges without writes");
static bool test_memory_write;
module_param(test_memory_write, bool, 0400);
MODULE_PARM_DESC(test_memory_write, "With reserve_memory: back up, test, and restore only the private auxiliary page");
static bool prepare_resources;
module_param(prepare_resources, bool, 0400);
MODULE_PARM_DESC(prepare_resources, "With reserve_memory: reserve bootstrap resources and construct CPU staging images without uploading");
static bool load_firmware;
module_param(load_firmware, bool, 0400);
MODULE_PARM_DESC(load_firmware, "With prepare_resources: load and verify the fixed firmware image into CPU memory");
static bool test_firmware_upload;
module_param(test_firmware_upload, bool, 0400);
MODULE_PARM_DESC(test_firmware_upload, "With load_firmware: back up, upload/readback and restore the 8 MiB firmware allocation without connecting");
static bool trial_connect;
module_param(trial_connect, bool, 0400);
MODULE_PARM_DESC(trial_connect, "With probe_rpc and load_firmware: publish prepared firmware, attempt connect/disconnect; retain resources if unacknowledged");
static bool runtime_context;
module_param(runtime_context, bool, 0400);
MODULE_PARM_DESC(runtime_context, "With load_firmware: prepare persistent context; trial_connect also retains successful connection and publishes it; no jobs/withdrawal yet");

#define MT_MEMORY_SNAPSHOT_SIZE (SZ_1M + SZ_64K)

/* Four rings inside shared page 1: each has 16 32-byte records and a
 * 16-byte header. Head/tail are bytes 0/1. Recovered from 14002bf90/14002bd70.
 */
struct mt_rpc_record {
	__le64 value;
	u8 type, subtype, status, operation;
	u8 reserved[20];
};

#include "mt_guest_device.h"

static struct mt_execution_store *mt_execution(struct mt_guest *g)
{
	return &container_of(g, struct mt_guest_device, state)->execution;
}

static struct mt_marker_store *mt_markers(struct mt_guest *g)
{
	return &container_of(g, struct mt_guest_device, state)->markers;
}

static struct mt_gem_store *mt_gem(struct mt_guest *g)
{
	return &container_of(g, struct mt_guest_device, state)->gem;
}

static struct mt_bo_store *mt_buffers(struct mt_guest *g)
{
	return &container_of(g, struct mt_guest_device, state)->buffers;
}

static struct mt_vm_store *mt_address_spaces(struct mt_guest *g)
{
	return &container_of(g, struct mt_guest_device, state)->address_spaces;
}

static struct mt_runtime_context *mt_runtime(struct mt_guest *g)
{
	return &container_of(g, struct mt_guest_device, state)->runtime;
}

static void mt_runtime_fini(struct mt_guest *g)
{
	struct mt_runtime_context *r = mt_runtime(g);
	if (WARN_ON(mt_runtime_context_can_release(r)))
		return;
	if (r->descriptor)
		free_page((unsigned long)r->descriptor);
	if (r->windows)
		free_page((unsigned long)r->windows);
	memset(r, 0, sizeof(*r));
}

static int mt_prepare_paging_memory(struct pci_dev *pdev, struct mt_guest *g)
{
	struct mt_guest_window_inputs inputs = {
		.system_memory_bytes = (u64)totalram_pages() << PAGE_SHIFT,
		.platform_9d8 = 0x8000000000ULL,
	};
	struct mt_system_address address;
	u8 windows[MT_GUEST_WINDOWS_BYTES];
	int ret;
	if (!g->queried || !g->info)
		return -EINVAL;
	ret = mt_guest_windows_build(windows, sizeof(windows), (void *)g->info,
		PAGE_SIZE, &inputs, pci_resource_start(pdev, 2), pci_resource_len(pdev, 2));
	if (ret)
		return ret;
	ret = mt_system_address_init(&address, (void *)g->info, PAGE_SIZE,
		windows, sizeof(windows), pci_resource_start(pdev, 4), pci_resource_len(pdev, 4));
	if (ret)
		return ret;
	return mt_system_memory_prepare(&container_of(g, struct mt_guest_device, state)->paging_command,
		MT_PAGING_COMMAND_BYTES, &address);
}

static int mt_runtime_prepare(struct pci_dev *pdev, struct mt_guest *g)
{
	struct mt_runtime_context *r = mt_runtime(g);
	struct mt_guest_heap_plan *plan;
	struct mt_fw_context_addresses a = {0};
	struct mt_guest_window_inputs inputs = {
		.system_memory_bytes = (u64)totalram_pages() << PAGE_SHIFT,
		.platform_9d8 = 0x8000000000ULL,
	};
	void *descriptor, *windows;
	u64 bar = pci_resource_start(pdev, 2), size = pci_resource_len(pdev, 2);
	int ret;
	if (!g->queried || !g->boot.prepared || !g->info || r->prepared ||
	    g->vram_blocks[0].bar_offset > size ||
	    MT_FW_MAP_SIZE > size - g->vram_blocks[0].bar_offset ||
	    bar > ~(u64)0 - g->vram_blocks[0].bar_offset)
		return -EINVAL;
	plan = kzalloc(sizeof(*plan), GFP_KERNEL);
	descriptor = (void *)get_zeroed_page(GFP_KERNEL);
	windows = (void *)get_zeroed_page(GFP_KERNEL);
	if (!plan || !descriptor || !windows) {
		ret = -ENOMEM;
		goto free_buffers;
	}
	mt_guest_plan_heaps(plan);
	a.root_pa = g->vram_blocks[1].gpu_pa;
	a.firmware_gpa = bar + g->vram_blocks[0].bar_offset;
	a.firmware_va = plan->heaps[6].base;
	a.info_gpa = virt_to_phys((void *)g->info);
	a.aperture_gpa = virt_to_phys(windows);
	/* S3000 Guest skips Native board-cap discovery; reference getter
	 * 1400229c0 returns zero for the initially NULL board-cap pointer. */
	a.device_config = 0;
	ret = mt_runtime_context_build(r, descriptor, windows, virt_to_phys(descriptor),
		&a, (void *)g->info, PAGE_SIZE, &inputs, bar, size);
	if (!ret) {
		kfree(plan);
		return 0;
	}
free_buffers:
	kfree(plan);
	if (descriptor)
		free_page((unsigned long)descriptor);
	if (windows)
		free_page((unsigned long)windows);
	return ret;
}

static void mt_runtime_write(void *opaque, u32 offset, u64 value)
{
	struct mt_guest *g = opaque;
	mb();
	writeq(value, g->custom + offset);
	mb();
}

static void mt_trial_clear_master(struct pci_dev *pdev, struct mt_guest *g)
{
	struct mt_guest_device *d = container_of(g, struct mt_guest_device, state);

	if (d->trial_bus_master) {
		pci_clear_master(pdev);
		d->trial_bus_master = false;
	}
}

static struct mt_rpc_service *mt_service(struct mt_guest *g)
{
	return &container_of(g, struct mt_guest_device, state)->service;
}

static void mt_guest_trial_delay(void *opaque)
{
	struct mt_guest *g = container_of(opaque, struct mt_guest, trial);
	mt_rpc_service_poll(mt_service(g));
	msleep(25);
}

static u32 mt_guest_trial_gpu_normal(void *opaque)
{
	struct mt_guest *g = container_of(opaque, struct mt_guest, trial);
	if (g->registered != 15 || !g->channel[0])
		return 0;
	return READ_ONCE(*(u8 *)g->channel[0]);
}

static const struct mt_fw_connection_ops mt_guest_trial_ops = {
	.firmware_state = mt_trial_fw_state, .firmware_started = mt_trial_started,
	.guest_state = mt_trial_guest_state, .notify_online = mt_trial_online,
	.send_command = mt_trial_send, .work_idle = mt_trial_work_idle,
	.control_idle = mt_trial_control_idle, .delay_25ms = mt_guest_trial_delay,
	.gpu_normal = mt_guest_trial_gpu_normal,
};

static int mt_runtime_can_submit(void *opaque)
{
	struct mt_guest *g = opaque;
	struct mt_runtime_context *r = mt_runtime(g);
	lockdep_assert_held(&g->trial_lock);
	if (!r->published || r->event_result || !g->trial.pinned || !g->trial.connected ||
	    !mt_service(g)->running || !mt_guest_trial_gpu_normal(&g->trial) ||
	    readl(g->regs + 0x890) != 2 || mt_trial_fw_state(&g->trial) != 2 ||
	    !mt_trial_started(&g->trial))
		return -EHOSTDOWN;
	/* The reference waits while this shared scheduling flag is exactly 1.
	 * Return backpressure instead of sleeping under the session mutex. */
	return mt_fw_submission_paused(READ_ONCE(*(u32 *)(g->channel[0] + 0x330))) ?
		-EAGAIN : 0;
}

static int mt_runtime_event(void *opaque, u32 dm, const struct mt_fw_event *event)
{
	struct mt_guest *g = opaque;
	struct mt_fw_trial *t = &g->trial;
	struct mt_marker_store *s = mt_markers(g);
	/* A matching pending marker completes its real dma_fence. Unexpected
	 * events cannot retire pending work or silently drop its ownership. */
	if (s->count[dm])
		return mt_marker_complete(s, dm, event);
	if (t->event_count == ARRAY_SIZE(t->events))
		return -ENOSPC;
	t->events[t->event_count].dm = dm;
	memcpy(t->events[t->event_count].words, event->words, sizeof(event->words));
	t->event_count++;
	return 0;
}

static void mt_runtime_poll(struct mt_guest *g)
{
	struct mt_runtime_context *r = mt_runtime(g);
	u32 consumed;
	if (r->published && !r->event_result)
		r->event_result = mt_fw_event_drain(&mt_fw_event_io_ops, &g->firmware_queue,
			MT_FW_QUEUE_BYTES, MT_FW_DM_COUNT * 63, mt_runtime_event, g, &consumed);
}

static int mt_runtime_start(struct mt_guest *g)
{
	struct mt_runtime_context *r = mt_runtime(g);
	int ret, cleanup;
	if (!r->prepared || r->published || g->trial.published || g->trial.pinned)
		return -EINVAL;
	ret = mt_trial_start(&g->trial, &mt_guest_trial_ops);
	if (!ret)
		ret = mt_runtime_context_publish(r, readl(g->regs + 0x890),
			mt_trial_fw_state(&g->trial), mt_trial_started(&g->trial),
			mt_guest_trial_gpu_normal(&g->trial), g->trial.pinned,
			mt_info_u64((void *)g->info, 0x10), mt_runtime_write, g);
	r->result = ret;
	if (r->published) {
		mt_service(g)->poll_session = mt_runtime_poll;
		return ret;
	}
	/* Before context publication, the bounded trial's teardown applies.
	 * Once its GPA is exposed, neither firmware disconnect nor local
	 * pointer clearing establishes context withdrawal. Keep the session. */
	if (g->trial.published && g->trial.pinned) {
		cleanup = mt_trial_disconnect(&g->trial, &mt_guest_trial_ops);
		if (cleanup)
			return cleanup;
	}
	return ret;
}


static int mt_test_firmware_upload(struct mt_guest *g)
{
	void *observed;
	void __iomem *mapping = g->vram_blocks[0].mapping;
	if (!g->firmware_image.data || !g->boot.prepared || !mapping ||
	    g->vram_blocks[0].size != MT_FW_MAP_SIZE)
		return -EINVAL;
	if (readl(g->regs + 0x890) != 0 || readl(g->regs + 0x898) != 1)
		return -EBUSY;
	g->firmware_backup = kvmalloc(MT_FW_MAP_SIZE, GFP_KERNEL);
	observed = kvmalloc(MT_FW_MAP_SIZE, GFP_KERNEL);
	if (!g->firmware_backup || !observed) {
		kvfree(observed);
		kvfree(g->firmware_backup);
		g->firmware_backup = NULL;
		return -ENOMEM;
	}
	memcpy_fromio(g->firmware_backup, mapping, MT_FW_MAP_SIZE);
	if (readl(g->regs + 0x890) != 0 || readl(g->regs + 0x898) != 1) {
		kvfree(observed);
		return -EBUSY;
	}
	/* The image contains no submitted command. No Guest-state, firmware
	 * address publication or doorbell writes occur anywhere in this test.
	 */
	g->firmware_written = true;
	memcpy_toio(mapping, g->firmware_image.data, MT_FW_MAP_SIZE);
	mb();
	memcpy_fromio(observed, mapping, MT_FW_MAP_SIZE);
	g->firmware_verified = !memcmp(observed, g->firmware_image.data, MT_FW_MAP_SIZE);
	/* Restore on every path after upload, even if readback differs. Keep
	 * the complete backup available through root-only sysfs until unload.
	 */
	memcpy_toio(mapping, g->firmware_backup, MT_FW_MAP_SIZE);
	mb();
	memcpy_fromio(observed, mapping, MT_FW_MAP_SIZE);
	g->firmware_restored = !memcmp(observed, g->firmware_backup, MT_FW_MAP_SIZE);
	kvfree(observed);
	return g->firmware_verified && g->firmware_restored ? 0 : -EIO;
}

static int mt_test_memory_write(struct mt_guest *g)
{
	u8 *backup, *pattern, *observed;
	void __iomem *page = g->vram_blocks[2].mapping;
	u32 i;
	int ret = 0;
	backup = kmalloc(3 * PAGE_SIZE, GFP_KERNEL);
	if (!backup)
		return -ENOMEM;
	pattern = backup + PAGE_SIZE;
	observed = pattern + PAGE_SIZE;
	if (readl(g->regs + 0x890) != 0 || readl(g->regs + 0x898) != 1) {
		ret = -EBUSY;
		goto free;
	}
	memcpy_fromio(backup, page, PAGE_SIZE);
	for (i = 0; i < PAGE_SIZE; i++)
		pattern[i] = (u8)(i ^ (i >> 8) ^ 0xa5);
	g->vram_write_performed = true;
	memcpy_toio(page, pattern, PAGE_SIZE);
	mb();
	memcpy_fromio(observed, page, PAGE_SIZE);
	g->vram_write_verified = !memcmp(observed, pattern, PAGE_SIZE);
	/* Restore on every path after the first write, including a mismatch.
	 * No address from this allocation is published to Host/FW or a GPU MMU.
	 */
	memcpy_toio(page, backup, PAGE_SIZE);
	mb();
	memcpy_fromio(observed, page, PAGE_SIZE);
	g->vram_restore_verified = !memcmp(observed, backup, PAGE_SIZE);
	if (!g->vram_write_verified || !g->vram_restore_verified)
		ret = -EIO;
free:
	kfree(backup);
	return ret;
}

static int mt_reserve_memory(struct pci_dev *pdev, struct mt_guest *g)
{
	struct mt_vram_block overflow = {0};
	u64 first_table_pa;
	u32 dm;
	int ret;
	u32 reg890;
	if (!g->queried)
		return -ENODATA;
	/* 0x890==2 accepted (see mt_probe). */
	reg890 = readl(g->regs + 0x890);
	if ((reg890 != 0 && reg890 != 2) || readl(g->regs + 0x898) != 1)
		return -EBUSY;
	ret = mt_vram_init(&g->vram, pdev, (void *)g->info, PAGE_SIZE);
	if (ret)
		return ret;
	ret = mt_vram_alloc(&g->vram, MT_POOL_FIRMWARE, MT_FW_MAP_SIZE, PAGE_SIZE, &g->vram_blocks[0]);
	if (ret)
		goto fail;
	ret = mt_vram_alloc(&g->vram, MT_POOL_NORMAL, MT_FW_TABLE_BYTES, PAGE_SIZE, &g->vram_blocks[1]);
	if (ret)
		goto fail;
	ret = mt_vram_alloc(&g->vram, MT_POOL_NORMAL, PAGE_SIZE, PAGE_SIZE, &g->vram_blocks[2]);
	if (ret)
		goto fail;
	/* Exercise exhaustion and reuse before any device memory can be published. */
	ret = mt_vram_alloc(&g->vram, MT_POOL_NORMAL, g->vram.layout.pool[MT_POOL_NORMAL].size,
			    PAGE_SIZE, &overflow);
	if (ret != -ENOSPC) {
		if (!ret)
			mt_vram_free(&g->vram, &overflow);
		ret = -EUCLEAN;
		goto fail;
	}
	first_table_pa = g->vram_blocks[1].gpu_pa;
	mt_vram_free(&g->vram, &g->vram_blocks[1]);
	ret = mt_vram_alloc(&g->vram, MT_POOL_NORMAL, MT_FW_TABLE_BYTES, PAGE_SIZE, &g->vram_blocks[1]);
	if (ret)
		goto fail;
	if (g->vram_blocks[1].gpu_pa != first_table_pa) {
		ret = -EUCLEAN;
		goto fail;
	}
	memcpy_fromio(g->vram_samples, g->vram_blocks[0].mapping, PAGE_SIZE);
	memcpy_fromio(g->vram_samples + PAGE_SIZE, g->vram_blocks[1].mapping, PAGE_SIZE);
	ret = mt_fw_queue_bind(&g->firmware_queue, g->vram_blocks[0].mapping,
			       g->vram_blocks[0].size, g->regs);
	if (ret)
		goto fail;
	/* Read the pre-existing queue counters only. Empty counters do not prove
	 * that resources are mapped or that firmware has accepted a connection.
	 */
	for (dm = 0; dm < MT_FW_DM_COUNT; dm++)
		g->queue_idle[dm] = mt_fw_queue_dm_idle(&mt_fw_io_ops,
			&g->firmware_queue, MT_FW_QUEUE_BYTES, dm);
	if (test_memory_write) {
		ret = mt_test_memory_write(g);
		dev_info(&pdev->dev, "auxiliary page test: written=%u pattern_verified=%u restore_verified=%u result=%d\n",
			 g->vram_write_performed, g->vram_write_verified, g->vram_restore_verified, ret);
		if (ret)
			goto fail;
	}
	return 0;
fail:
	mt_vram_fini(&g->vram);
	return ret;
}

static int mt_snapshot_memory(struct pci_dev *pdev, struct mt_guest *g)
{
	const u8 *info = (const u8 *)g->info;
	u64 cursor, size, flags, base, actual, bar_len;
	u64 fw_size = 0, shared_size = 0;
	u32 count, i;
	void __iomem *window;
	int ret;

	if (!g->queried || le32_to_cpup((const __le32 *)info) != 0xaa557491 ||
	    le32_to_cpup((const __le32 *)(info + 4)) != 2)
		return -EPROTO;
	flags = le64_to_cpup((const __le64 *)(info + 0x10));
	if (!(flags & 0x80))
		return -EOPNOTSUPP;
	actual = le64_to_cpup((const __le64 *)(info + 0x20));
	bar_len = pci_resource_len(pdev, 2);
	if (!(pci_resource_flags(pdev, 2) & IORESOURCE_MEM) || actual > bar_len)
		return -ERANGE;
	cursor = flags & 0x10 ? le32_to_cpup((const __le32 *)(info + 0xc98)) : 0;
	count = le32_to_cpup((const __le32 *)(info + 0xc50));
	if (count > (0xc48 - 0x28) / 24)
		return -EPROTO;
	for (i = 0; i < count; i++) {
		const u8 *segment = info + 0x28 + i * 24;
		base = le64_to_cpup((const __le64 *)segment);
		size = le64_to_cpup((const __le64 *)(segment + 8));
		flags = le64_to_cpup((const __le64 *)(segment + 16));
		if (flags & 7) {
			if (cursor > actual || size > actual - cursor)
				return -ERANGE;
			if ((flags & 4) && !fw_size) {
				g->firmware_bar_offset = cursor;
				fw_size = size;
			}
			cursor += size;
		}
		if (flags & 0x20) {
			if (shared_size || base > bar_len || size > bar_len - base)
				return -ERANGE;
			g->shared_bar_offset = base;
			shared_size = size;
		}
	}
	if (fw_size < SZ_1M || shared_size < SZ_64K ||
	    !IS_ALIGNED(g->firmware_bar_offset, PAGE_SIZE) ||
	    !IS_ALIGNED(g->shared_bar_offset, PAGE_SIZE))
		return -ERANGE;
	if (readl(g->regs + 0x890) != (recover_channels ? 1 : 0) ||
	    readl(g->regs + 0x898) != 1)
		return -EBUSY;
	g->memory_snapshot = vzalloc(MT_MEMORY_SNAPSHOT_SIZE);
	if (!g->memory_snapshot)
		return -ENOMEM;
	ret = pci_request_region(pdev, 2, "mt_guest_probe_snapshot");
	if (ret)
		return ret;
	window = pci_iomap_range(pdev, 2, g->firmware_bar_offset, SZ_1M);
	if (!window) {
		ret = -ENOMEM;
		goto release;
	}
	memcpy_fromio(g->memory_snapshot, window, SZ_1M);
	pci_iounmap(pdev, window);
	window = pci_iomap_range(pdev, 2, g->shared_bar_offset, SZ_64K);
	if (!window) {
		ret = -ENOMEM;
		goto release;
	}
	memcpy_fromio(g->memory_snapshot + SZ_1M, window, SZ_64K);
	pci_iounmap(pdev, window);
	ret = 0;
release:
	pci_release_region(pdev, 2);
	return ret;
}

static int mt_rpc_send(struct mt_guest *g, u8 type, u8 subtype, u8 operation,
		       u64 value)
{
	u8 *ring = (u8 *)g->channel[1];
	u8 head = READ_ONCE(ring[0]), tail = READ_ONCE(ring[1]);
	struct mt_rpc_record record = {
		.value = cpu_to_le64(value), .type = type, .subtype = subtype,
		.operation = operation,
	};
	if (head > 15 || tail > 15)
		return -EPROTO;
	if (((head + 1) & 15) == tail)
		return -ENOSPC;
	memcpy(ring + 0x10 + head * 0x20, &record, sizeof(record));
	wmb();
	WRITE_ONCE(ring[0], (head + 1) & 15);
	wmb();
	/* 140027350 notifies BAR1+0x138 with the sending ring index (zero). */
	writeq(0, g->custom + 0x138);
	mb();
	return 0;
}

static int mt_rpc_query(struct mt_guest *g, u8 subtype, u64 value, u64 *answer)
{
	u8 *ring = (u8 *)g->channel[1] + 3 * 0x210;
	struct mt_rpc_record record;
	u8 head, tail;
	int i, ret;
	ret = mt_rpc_send(g, 0, subtype, 1, value);
	if (ret)
		return ret;
	for (i = 0; i < 100; i++) {
		mt_rpc_service_poll(mt_service(g));
		head = READ_ONCE(ring[0]);
		tail = READ_ONCE(ring[1]);
		if (head > 15 || tail > 15)
			return -EPROTO;
		if (head != tail) {
			rmb();
			memcpy(&record, ring + 0x10 + tail * 0x20, sizeof(record));
			mb();
			WRITE_ONCE(ring[1], (tail + 1) & 15);
			if (record.type || record.subtype != subtype || record.operation != 2)
				return -EPROTO;
			*answer = le64_to_cpu(record.value);
			return record.status ? -EREMOTEIO : 0;
		}
		usleep_range(1000, 2000);
	}
	return -ETIMEDOUT;
}

static void mt_release_channels(struct pci_dev *pdev, struct mt_guest *g)
{
	int i;
	if (g->mode_started) {
		/* Matching Windows 1400273d4 -> 14002b7c4(...,1,1,0). */
		int stop = mt_rpc_send(g, 2, 1, 0, 0);
		if (stop)
			dev_warn(&pdev->dev, "mode stop notification failed: %d\n", stop);
	}
	g->mode_started = false;
	for (i = 3; i >= 0; i--) {
		if (g->channel[i])
			memcpy(g->channel_snapshot + i * PAGE_SIZE, (void *)g->channel[i], PAGE_SIZE);
		if (g->registered & BIT(i)) {
			/* 1400271d8: clear valid, select slot, synchronous unregister.
			 * No guessed reset or interrupt acknowledge is issued.
			 */
			writeq(i << 1, g->custom + 0x158);
			mb();
			g->registered &= ~BIT(i);
		}
	}
	/* Host no longer references these pages. Join IRQ/work before freeing
	 * any page that the handler or worker can still access. */
	mt_rpc_service_stop(mt_service(g));
	for (i = 3; i >= 0; i--) {
		if (g->channel[i]) {
			free_page(g->channel[i]);
			g->channel[i] = 0;
		}
	}
}

static int mt_probe_channels(struct pci_dev *pdev, struct mt_guest *g)
{
	int i, ret = -ENOMEM;
	u64 response;
	u8 package[32];

	BUILD_BUG_ON(sizeof(struct mt_rpc_record) != 32);
	/* Allocate all pages before handing any address to the Host. */
	for (i = 0; i < 4; i++) {
		g->channel[i] = get_zeroed_page(GFP_KERNEL);
		if (!g->channel[i])
			goto release;
	}
	ret = mt_rpc_service_start(mt_service(g), pdev, g);
	if (ret)
		goto release;
	if (recover_channels) {
		if (readl(g->regs + 0x890) != 1 || readl(g->regs + 0x898) != 1) {
			ret = -EBUSY;
			goto release;
		}
		/* 140025a48 / 1400271d8 withdraw shared slots in reverse order.
		 * After a Guest reboot their old CPU pages are no longer ours.
		 * Withdraw only the Host registrations; never access old GPAs.
		 * This does not reset firmware or revoke its other mappings.
		 */
		for (i = 3; i >= 0; i--) {
			writeq(i << 1, g->custom + 0x158);
			mb();
			response = readq(g->custom + 0x158);
			dev_info(&pdev->dev, "orphan channel %d withdrawal response=%#llx\n", i, response);
			if (response) {
				ret = -EREMOTEIO;
				goto release;
			}
		}
	}
	for (i = 0; i < 4; i++) {
		/* 140024ee4: length in bits [40:9], slot in [8:1], valid in bit 0. */
		wmb();
		writeq(virt_to_phys((void *)g->channel[i]), g->custom + 0x150);
		writeq((4096ULL << 9) | (i << 1) | 1, g->custom + 0x158);
		response = readq(g->custom + 0x158);
		if (response) {
			dev_warn(&pdev->dev, "channel %d registration rejected: %#llx\n", i, response);
			ret = -EREMOTEIO;
			goto release;
		}
		g->registered |= BIT(i);
		if (i == 0) {
			/* 140024f14 sets gpu_normal after the Host accepts page 0. */
			WRITE_ONCE(*(u8 *)g->channel[0], 1);
			wmb();
		} else if (i == 1) {
			/* 14002b7c4(...,1,0,0) announces normal messaging mode. */
			ret = mt_rpc_send(g, 2, 0, 0, 0);
			if (ret)
				goto release;
			g->mode_started = true;
		}
	}
	ret = mt_rpc_query(g, 1, 0, &g->rpc_value);
	if (!ret && g->rpc_value == 1) {
		ret = mt_package_announcement(g->rpc_value, package);
		if (!ret)
			ret = mt_rpc_publish((void *)g->channel[1], g->custom, 0, package, 1);
		if (ret)
			goto release;
		/* 14002680c / DAT_1411065a8: version negotiation for the
		 * reference protocol 2.7.5, revision 5. This is not an installed
		 * Linux driver version. No package-update commands are sent.
		 */
		g->version_result = mt_rpc_query(g, 2, 0x0005000500070002ULL,
						 &g->host_version);
	}
	if ((trial_connect || recover_channels) && !ret && g->rpc_value == 1 && !g->version_result &&
	    (g->host_version & BIT_ULL(56)))
		return 0;
release:
	mt_release_channels(pdev, g);
	return ret;
}

static ssize_t connection_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct mt_guest *g = dev_get_drvdata(dev);
	return sysfs_emit(buf, "driver=%u firmware=%u info_queried=%u rpc_result=%d rpc_value=%llu version_result=%d host_version=%#llx memory_result=%d firmware_bar_offset=%#llx shared_bar_offset=%#llx render_ready=0\n",
			  readl(g->regs + 0x890), readl(g->regs + 0x898), g->queried,
			  g->rpc_result, g->rpc_value, g->version_result, g->host_version,
			  g->memory_result, g->firmware_bar_offset, g->shared_bar_offset);
}
static DEVICE_ATTR_RO(connection);

static ssize_t rpc_service_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mt_guest *g = dev_get_drvdata(dev);
	struct mt_rpc_service *s = mt_service(g);
	ssize_t ret;
	mutex_lock(&g->trial_lock);
	ret = sysfs_emit(buf, "running=%u ready=%u irq=%u acknowledgments=%lld replies=%llu rejected=%llu\n",
		s->running, s->ready, to_pci_dev(dev)->irq,
		atomic64_read(&s->acknowledgments), s->replies, s->rejected);
	mutex_unlock(&g->trial_lock);
	return ret;
}
static DEVICE_ATTR_RO(rpc_service);

static ssize_t publication_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mt_guest *g = dev_get_drvdata(dev);
	ssize_t ret;
	mutex_lock(&g->trial_lock);
	/* These are configuration readbacks, not proof of Host mappings. */
	ret = sysfs_emit(buf, "bar2_gpa=%#llx local_mmu_gpa=%#llx firmware_pa=%#llx firmware_bytes=%#llx\n",
		readq(g->custom + 0x20), readq(g->custom + 0x28),
		readq(g->custom + 0x30), readq(g->custom + 0x38));
	mutex_unlock(&g->trial_lock);
	return ret;
}
static DEVICE_ATTR_RO(publication);

static ssize_t retained_status_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct mt_guest *g = dev_get_drvdata(dev);
	void __iomem *window;
	u32 dm, ring, cursor;
	int at;
	/* Unlike memory_raw's probe-time snapshot, these are live reads.
	 * Reuse the range already checked against the fresh device-info page.
	 * Never initialize, consume or acknowledge any firmware queue here.
	 */
	if (!recover_channels || g->memory_result)
		return -ENODATA;
	BUILD_BUG_ON(MT_FW_STATE_BYTES + MT_FW_QUEUE_BYTES > SZ_1M);
	mutex_lock(&g->trial_lock);
	at = pci_request_region(pdev, 2, "mt_guest_retained_read");
	if (at)
		goto unlock;
	window = pci_iomap_range(pdev, 2, g->firmware_bar_offset, SZ_1M);
	if (!window) {
		at = -ENOMEM;
		goto release;
	}
	at = sysfs_emit(buf, "guest=%u firmware=%u started=%u\n",
		readl(g->regs + 0x890), readl(g->regs + 0x898), readl(window + 4));
	for (dm = 0; dm < MT_FW_DM_COUNT; dm++)
		for (ring = 0; ring < 3; ring++) {
			cursor = MT_FW_STATE_BYTES + dm * MT_FW_DM_BYTES +
				MT_FW_CURSOR_OFFSET + ring * 16;
			at += sysfs_emit_at(buf, at, "dm=%u ring=%u head=%u tail=%u\n",
				dm, ring, readl(window + cursor), readl(window + cursor + 8));
		}
	pci_iounmap(pdev, window);
release:
	pci_release_region(pdev, 2);
unlock:
	mutex_unlock(&g->trial_lock);
	return at;
}
static DEVICE_ATTR_RO(retained_status);

static ssize_t retained_control_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct mt_guest *g = dev_get_drvdata(dev);
	struct mt_guest_device *d = container_of(g, struct mt_guest_device, state);
	void __iomem *window;
	u32 dm, ring, cursor, i;
	int ret;

	if (!sysfs_streq(buf, "kick") || !recover_channels || g->memory_result)
		return -EINVAL;
	mutex_lock(&g->trial_lock);
	ret = -EALREADY;
	if (d->retained_kick_attempted)
		goto unlock;
	ret = -EBUSY;
	/* Limit this experiment to the retained first trial, never new jobs.
	 * Address readback is only a guard, not proof of Host mapping validity.
	 */
	if (g->registered != 15 || !mt_service(g)->ready ||
	    readl(g->regs + 0x890) != 1 || readl(g->regs + 0x898) != 1 ||
	    g->firmware_bar_offset != 0x3f000000ULL ||
	    readq(g->custom + 0x20) != pci_resource_start(pdev, 2) ||
	    readq(g->custom + 0x30) != 0x771fef000ULL ||
	    readq(g->custom + 0x38) != MT_FW_MAP_SIZE)
		goto unlock;
	ret = pci_request_region(pdev, 2, "mt_guest_retained_kick");
	if (ret)
		goto unlock;
	window = pci_iomap_range(pdev, 2, g->firmware_bar_offset, SZ_1M);
	if (!window) {
		ret = -ENOMEM;
		goto release;
	}
	ret = -EBUSY;
	if (readl(window + 4))
		goto unmap;
	for (dm = 0; dm < MT_FW_DM_COUNT; dm++)
		for (ring = 0; ring < 3; ring++) {
			cursor = MT_FW_STATE_BYTES + dm * MT_FW_DM_BYTES +
				MT_FW_CURSOR_OFFSET + ring * 16;
			if (readl(window + cursor) != ((!dm && !ring) ? 5 : 0) ||
			    readl(window + cursor + 8))
				goto unmap;
		}
	for (i = 0; i < 5; i++)
		if (readl(window + MT_FW_STATE_BYTES + i * MT_FW_COMMAND_BYTES + 0xc) !=
		    (i == 4 ? MT_FW_DISCONNECT : MT_FW_CONNECT))
			goto unmap;
	d->retained_kick_attempted = true;
	/* 14002249c and 14000bf20: only notify about the already queued
	 * four connects and disconnect. Do not enqueue another request, reset
	 * cursors, acknowledge events, rewrite firmware, or alter Guest state.
	 */
	writeq(1, g->custom + 0x148);
	mb();
	writel(0, g->regs + 0xb00);
	mb();
	for (i = 0; i < 400; i++) {
		mt_rpc_service_poll(mt_service(g));
		if (readl(g->regs + 0x898) != 1 || readl(window + 4) ||
		    readl(window + MT_FW_STATE_BYTES + MT_FW_CURSOR_OFFSET + 8))
			break;
		msleep(25);
	}
	dev_info(dev, "retained queue kick: polls=%u guest=%u firmware=%u started=%u head=%u tail=%u; no command added\n",
		i, readl(g->regs + 0x890), readl(g->regs + 0x898), readl(window + 4),
		readl(window + MT_FW_STATE_BYTES + MT_FW_CURSOR_OFFSET),
		readl(window + MT_FW_STATE_BYTES + MT_FW_CURSOR_OFFSET + 8));
	/* A completed write means the notification was sent, not connected. */
	ret = 0;
unmap:
	pci_iounmap(pdev, window);
release:
	pci_release_region(pdev, 2);
unlock:
	mutex_unlock(&g->trial_lock);
	return ret ? ret : count;
}
static DEVICE_ATTR_WO(retained_control);

static ssize_t vram_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mt_guest *g = dev_get_drvdata(dev);
	int i, at = 0;
	at += sysfs_emit_at(buf, at, "result=%d region_owned=%u device_memory_written=%u pattern_verified=%u restore_verified=%u\n",
			    g->vram_result, g->vram.region_owned,
			    g->vram_write_performed || g->firmware_written || g->trial.upload[0].written,
			    g->vram_write_verified, g->vram_restore_verified);
	if (g->vram_result)
		return at;
	for (i = 0; i < MT_POOL_COUNT; i++)
		at += sysfs_emit_at(buf, at, "pool=%d bar_offset=%#llx gpu_pa=%#llx size=%llu available=%zu\n",
			 i, g->vram.layout.pool[i].bar_offset, g->vram.layout.pool[i].gpu_pa,
			 g->vram.layout.pool[i].size, gen_pool_avail(g->vram.pool[i]));
	for (i = 0; i < ARRAY_SIZE(g->vram_blocks); i++)
		at += sysfs_emit_at(buf, at, "allocation=%d pool=%u bar_offset=%#llx gpu_pa=%#llx size=%u\n",
			 i, g->vram_blocks[i].pool, g->vram_blocks[i].bar_offset,
			 g->vram_blocks[i].gpu_pa, g->vram_blocks[i].size);
	for (i = 0; i < MT_FW_DM_COUNT; i++)
		at += sysfs_emit_at(buf, at, "queue_dm=%d idle_snapshot=%d initialized=0 submitted=0\n",
				   i, g->queue_idle[i]);
	return at;
}
static DEVICE_ATTR_RO(vram);

static ssize_t bootstrap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	static const char * const names[MT_BOOT_ALLOCATION_COUNT] = {
		"dummy", "pds", "usc", "yuv", "dm-kill", "fence", "paging-context", "pb"
	};
	struct mt_guest *g = dev_get_drvdata(dev);
	int i, at;
	at = sysfs_emit(buf, "prepared=%u table_pages=%u stage_bytes=%u uploaded=0 root_published=0\n",
		g->boot.prepared, g->boot.table_pages, g->boot.prepared ? MT_BOOT_STAGE_BYTES : 0);
	if (!g->boot.prepared)
		return at;
	for (i = 0; i < MT_BOOT_ALLOCATION_COUNT; i++)
		at += sysfs_emit_at(buf, at, "name=%s pool=%u gpu_pa=%#llx bar_offset=%#llx size=%u planned_va=%#llx mapped=0\n",
			names[i], g->boot.blocks[i].pool, g->boot.blocks[i].gpu_pa,
			g->boot.blocks[i].bar_offset, g->boot.blocks[i].size, g->boot.va[i]);
	return at;
}
static DEVICE_ATTR_RO(bootstrap);

static ssize_t firmware_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mt_guest *g = dev_get_drvdata(dev);
	return sysfs_emit(buf, "loaded=%u bytes=%u sha256=%*phN upload_result=%d written=%u verified=%u restored=%u published=%u connected=%u\n",
		!!g->firmware_image.data, g->firmware_image.data ? MT_FW_MAP_SIZE : 0,
		SHA256_DIGEST_SIZE, g->firmware_image.sha256, g->firmware_upload_result,
		g->firmware_written || g->trial.upload[0].written,
		g->firmware_verified || g->trial.verified, g->firmware_restored || g->trial.restored,
		g->trial.published, g->trial.connected);
}
static DEVICE_ATTR_RO(firmware);

static ssize_t firmware_image_read(struct file *file, struct kobject *kobj,
		struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	struct mt_guest *g = dev_get_drvdata(kobj_to_dev(kobj));
	if (!g->firmware_image.data)
		return -ENODATA;
	return memory_read_from_buffer(buf, count, &off, g->firmware_image.data, MT_FW_MAP_SIZE);
}
static BIN_ATTR_ADMIN_RO(firmware_image, MT_FW_MAP_SIZE);

static ssize_t firmware_backup_read(struct file *file, struct kobject *kobj,
		struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	struct mt_guest *g = dev_get_drvdata(kobj_to_dev(kobj));
	const void *backup = g->firmware_backup ? g->firmware_backup : g->trial.upload[0].before;
	if (!backup)
		return -ENODATA;
	return memory_read_from_buffer(buf, count, &off, backup, MT_FW_MAP_SIZE);
}
static BIN_ATTR_ADMIN_RO(firmware_backup, MT_FW_MAP_SIZE);

static ssize_t trial_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mt_guest *g = dev_get_drvdata(dev);
	struct mt_fw_trial *t = &g->trial;
	u32 dm, ring, cursor;
	int at;
	at = sysfs_emit(buf, "result=%d connect_result=%d disconnect_result=%d published=%u connected=%u disconnected=%u restored=%u pinned=%u registered=%u online=%u connect_commands=%u disconnect_commands=%u\n",
		t->result, t->connect_result, t->disconnect_result, t->published, t->connected,
		t->disconnected, t->restored, t->pinned, g->registered, t->online_count,
		t->connect_commands, t->disconnect_commands);
	if (!t->scratch)
		return at;
	at += sysfs_emit_at(buf, at, "guest=%u firmware=%u started=%u events=%u\n",
		readl(g->regs + 0x890), mt_trial_fw_state(t), mt_trial_started(t), t->event_count);
	for (dm = 0; dm < MT_FW_DM_COUNT; dm++)
		for (ring = 0; ring < 3; ring++) {
			cursor = dm * MT_FW_DM_BYTES + MT_FW_CURSOR_OFFSET + ring * 16;
			at += sysfs_emit_at(buf, at, "dm=%u ring=%u head=%u tail=%u\n", dm, ring,
				readl(g->firmware_queue.queue + cursor), readl(g->firmware_queue.queue + cursor + 8));
		}
	return at;
}
static DEVICE_ATTR_RO(trial);

static ssize_t runtime_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mt_guest *g = dev_get_drvdata(dev);
	struct mt_runtime_context *r = mt_runtime(g);
	ssize_t ret;
	mutex_lock(&g->trial_lock);
	ret = sysfs_emit(buf, "prepared=%u published=%u notified=%u result=%d retained=%u guest=%u firmware=%u started=%u event_result=%d events=%u render_ready=0\n",
		r->prepared, r->published, r->notified, r->result, g->trial.pinned,
		readl(g->regs + 0x890), readl(g->regs + 0x898),
		g->trial.scratch ? mt_trial_started(&g->trial) : 0,
		r->event_result, g->trial.event_count);
	mutex_unlock(&g->trial_lock);
	return ret;
}
static DEVICE_ATTR_RO(runtime);

static ssize_t buffers_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mt_guest *g = dev_get_drvdata(dev);
	struct mt_bo_store *s = mt_buffers(g);
	ssize_t ret;
	mutex_lock(&g->trial_lock);
	ret = sysfs_emit(buf, "objects=%u allocated_bytes=%llu normal_pool_ready=%u address_spaces=%u gem_objects=%u processes=%u contexts=%u family=%u ce_version=%u transfer_version=%u drm_registered=0\n",
		s->objects, s->allocated_bytes, g->vram.region_owned, mt_address_spaces(g)->objects,
		mt_gem(g)->objects, mt_execution(g)->processes, mt_execution(g)->contexts,
		mt_gem(g)->profile.family, mt_gem(g)->profile.ce_version, mt_gem(g)->profile.transfer_version);
	mutex_unlock(&g->trial_lock);
	return ret;
}
static DEVICE_ATTR_RO(buffers);

static ssize_t completions_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mt_guest *g = dev_get_drvdata(dev);
	struct mt_marker_store *s = mt_markers(g);
	ssize_t ret;
	mutex_lock(&g->trial_lock);
	ret = sysfs_emit(buf, "pending=%u completed=%llu submit_enabled=%u workload_submit=0\n",
		s->total, s->completed, s->ready);
	mutex_unlock(&g->trial_lock);
	return ret;
}
static DEVICE_ATTR_RO(completions);

static ssize_t runtime_context_raw_read(struct file *file, struct kobject *kobj,
		struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	struct mt_guest *g = dev_get_drvdata(kobj_to_dev(kobj));
	struct mt_runtime_context *r = mt_runtime(g);
	ssize_t ret;
	mutex_lock(&g->trial_lock);
	ret = r->prepared ? memory_read_from_buffer(buf, count, &off,
		r->descriptor, MT_FW_CONTEXT_BYTES) : -ENODATA;
	mutex_unlock(&g->trial_lock);
	return ret;
}
static BIN_ATTR_ADMIN_RO(runtime_context_raw, MT_FW_CONTEXT_BYTES);

static ssize_t runtime_windows_raw_read(struct file *file, struct kobject *kobj,
		struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	struct mt_guest *g = dev_get_drvdata(kobj_to_dev(kobj));
	struct mt_runtime_context *r = mt_runtime(g);
	ssize_t ret;
	mutex_lock(&g->trial_lock);
	ret = r->prepared ? memory_read_from_buffer(buf, count, &off,
		r->windows, MT_GUEST_WINDOWS_BYTES) : -ENODATA;
	mutex_unlock(&g->trial_lock);
	return ret;
}
static BIN_ATTR_ADMIN_RO(runtime_windows_raw, MT_GUEST_WINDOWS_BYTES);

static ssize_t trial_events_read(struct file *file, struct kobject *kobj,
		struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	struct mt_guest *g = dev_get_drvdata(kobj_to_dev(kobj));
	ssize_t ret;
	mutex_lock(&g->trial_lock);
	ret = memory_read_from_buffer(buf, count, &off, g->trial.events,
		g->trial.event_count * sizeof(g->trial.events[0]));
	mutex_unlock(&g->trial_lock);
	return ret;
}
static BIN_ATTR_ADMIN_RO(trial_events, 128 * 28);

static ssize_t trial_control_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct mt_guest *g = dev_get_drvdata(dev);
	int ret;
	if (recover_channels)
		return -EOPNOTSUPP;
	if (!sysfs_streq(buf, "disconnect") && !sysfs_streq(buf, "restore"))
		return -EINVAL;
	mutex_lock(&g->trial_lock);
	/* Trial restoration also rewrites shared boot allocations. An idle GPU
	 * alone cannot authorize it while a VM still retains borrowed views. */
	if (mt_boot_bo_can_release(&container_of(g, struct mt_guest_device, state)->shared_boot)) {
		mutex_unlock(&g->trial_lock);
		return -EBUSY;
	}
	if (mt_runtime_context_can_release(mt_runtime(g))) {
		mutex_unlock(&g->trial_lock);
		return -EOPNOTSUPP;
	}
	ret = sysfs_streq(buf, "disconnect") ? mt_trial_disconnect(&g->trial, &mt_guest_trial_ops) :
		(g->trial.pinned ? mt_trial_restore(&g->trial) : -EINVAL);
	g->trial.result = ret;
	if (!g->trial.pinned) {
		mt_trial_clear_master(to_pci_dev(dev), g);
		mt_release_channels(to_pci_dev(dev), g);
	}
	mutex_unlock(&g->trial_lock);
	return ret ? ret : count;
}
static DEVICE_ATTR_WO(trial_control);

static ssize_t trial_firmware_read(struct file *file, struct kobject *kobj,
		struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	struct mt_guest *g = dev_get_drvdata(kobj_to_dev(kobj));
	if (!g->trial.scratch)
		return -ENODATA;
	if (off < 0 || off >= MT_FW_MAP_SIZE)
		return 0;
	count = min_t(size_t, count, MT_FW_MAP_SIZE - off);
	memcpy_fromio(buf, g->vram_blocks[0].mapping + off, count);
	return count;
}
static BIN_ATTR_ADMIN_RO(trial_firmware, MT_FW_MAP_SIZE);

static ssize_t trial_backups_read(struct file *file, struct kobject *kobj,
		struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	struct mt_guest *g = dev_get_drvdata(kobj_to_dev(kobj));
	u32 i;
	size_t copied = 0, part;
	if (!g->trial.scratch)
		return -ENODATA;
	if (off < 0)
		return -EINVAL;
	for (i = 0; i < MT_TRIAL_BLOCKS && copied < count; i++) {
		struct mt_trial_upload *u = &g->trial.upload[i];
		if (!u->before)
			return -ENODATA;
		if (off >= u->block->size) {
			off -= u->block->size;
			continue;
		}
		part = min_t(size_t, count - copied, u->block->size - off);
		memcpy(buf + copied, (u8 *)u->before + off, part);
		copied += part;
		off = 0;
	}
	return copied;
}
static BIN_ATTR_ADMIN_RO(trial_backups, MT_TRIAL_BACKUP_BYTES);

static ssize_t bootstrap_raw_read(struct file *file, struct kobject *kobj,
		struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	struct mt_guest *g = dev_get_drvdata(kobj_to_dev(kobj));
	if (!g->boot.prepared)
		return -ENODATA;
	return memory_read_from_buffer(buf, count, &off, g->boot.stage, MT_BOOT_STAGE_BYTES);
}
static BIN_ATTR_ADMIN_RO(bootstrap_raw, MT_BOOT_STAGE_BYTES);

static ssize_t vram_samples_read(struct file *file, struct kobject *kobj,
				struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	struct mt_guest *g = dev_get_drvdata(kobj_to_dev(kobj));
	if (g->vram_result)
		return -ENODATA;
	return memory_read_from_buffer(buf, count, &off, g->vram_samples, sizeof(g->vram_samples));
}
static BIN_ATTR_ADMIN_RO(vram_samples, 2 * PAGE_SIZE);

static ssize_t info_raw_read(struct file *file, struct kobject *kobj,
			     struct bin_attribute *attr, char *buf,
			     loff_t off, size_t count)
{
	struct mt_guest *g = dev_get_drvdata(kobj_to_dev(kobj));
	if (!g->queried)
		return -ENODATA;
	return memory_read_from_buffer(buf, count, &off, (void *)g->info, PAGE_SIZE);
}
static BIN_ATTR_ADMIN_RO(info_raw, PAGE_SIZE);

static ssize_t channels_raw_read(struct file *file, struct kobject *kobj,
				 struct bin_attribute *attr, char *buf,
				 loff_t off, size_t count)
{
	struct mt_guest *g = dev_get_drvdata(kobj_to_dev(kobj));
	int i;
	if (!probe_rpc)
		return -ENODATA;
	mutex_lock(&g->trial_lock);
	for (i = 0; i < 4; i++)
		if (g->channel[i])
			memcpy(g->channel_snapshot + i * PAGE_SIZE, (void *)g->channel[i], PAGE_SIZE);
	mutex_unlock(&g->trial_lock);
	return memory_read_from_buffer(buf, count, &off, g->channel_snapshot,
				       sizeof(g->channel_snapshot));
}
static BIN_ATTR_ADMIN_RO(channels_raw, 4 * PAGE_SIZE);

static ssize_t memory_raw_read(struct file *file, struct kobject *kobj,
			       struct bin_attribute *attr, char *buf,
			       loff_t off, size_t count)
{
	struct mt_guest *g = dev_get_drvdata(kobj_to_dev(kobj));
	if (g->memory_result)
		return g->memory_result;
	return memory_read_from_buffer(buf, count, &off, g->memory_snapshot,
				       MT_MEMORY_SNAPSHOT_SIZE);
}
static BIN_ATTR_ADMIN_RO(memory_raw, MT_MEMORY_SNAPSHOT_SIZE);

static struct attribute *mt_attrs[] = {
	&dev_attr_buffers.attr,
	&dev_attr_completions.attr,
	&dev_attr_runtime.attr,
	&dev_attr_publication.attr, &dev_attr_retained_status.attr,
	&dev_attr_retained_control.attr,
	&dev_attr_connection.attr, &dev_attr_vram.attr, &dev_attr_bootstrap.attr,
	&dev_attr_rpc_service.attr,
	&dev_attr_firmware.attr, &dev_attr_trial.attr, &dev_attr_trial_control.attr, NULL
};
static struct bin_attribute *mt_bin_attrs[] = {
	&bin_attr_runtime_context_raw, &bin_attr_runtime_windows_raw,
	&bin_attr_info_raw, &bin_attr_channels_raw, &bin_attr_memory_raw, &bin_attr_vram_samples,
	&bin_attr_bootstrap_raw, &bin_attr_firmware_image, &bin_attr_firmware_backup,
	&bin_attr_trial_firmware, &bin_attr_trial_backups, &bin_attr_trial_events, NULL
};
static const struct attribute_group mt_group = {
	.name = "mt_guest",
	.attrs = mt_attrs,
	.bin_attrs = mt_bin_attrs,
};

static int mt_read_device_info(struct pci_dev *pdev, struct mt_guest *g)
{
	if (!g->info)
		return -EINVAL;
	/* 0x890==2 accepted when trial_connect (see mt_probe). */
	u32 reg890 = readl(g->regs + 0x890);
	if ((reg890 != (recover_channels ? 1 : 0) &&
	     !(trial_connect && !recover_channels && reg890 == 2)) ||
	    readl(g->regs + 0x898) != 1)
		return -EBUSY;
	memset((void *)g->info, 0, PAGE_SIZE);
	/* Windows sets these request capability bits before submission. */
	*(__le64 *)(g->info + 0xc48) = cpu_to_le64(3);
	wmb();
	writeq(virt_to_phys((void *)g->info), g->custom + 0xc8);
	/* 140026b30 publishes the PCI aperture GPA after the info request.
	 * Keep this separate from firmware's GPU physical address. */
	writeq(pci_resource_start(pdev, 2), g->custom + 0x20);
	/* 14002fe44 obtains +0x518 from BAR4. This VM has no BAR4,
	 * so 140026b30 publishes zero for the local-MMU aperture. */
	writeq(pci_resource_start(pdev, 4), g->custom + 0x28);
	mb();
	g->queried = true;
	dev_info(&pdev->dev, "info response: magic=%08x version=%u osid=%u; rendering unavailable\n",
		 le32_to_cpup((__le32 *)g->info),
		 le32_to_cpup((__le32 *)(g->info + 4)),
		 le32_to_cpup((__le32 *)(g->info + 8)));
	if (le32_to_cpup((__le32 *)g->info) != 0xaa557491 ||
	    le32_to_cpup((__le32 *)(g->info + 4)) != 2)
		return -EPROTO;
	if (mt_gem(g)->profile.family == 2)
		return mt_gem_configure_tqx_info_locked(mt_gem(g), (void *)g->info, PAGE_SIZE);
	return 0;
}

static int mt_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct mt_guest *g;
	struct mt_device_profile profile;
	u8 announcements[96];
	u16 tag, command;
	int cap, ret;

	BUILD_BUG_ON(PAGE_SIZE != 4096);
	if (refresh_osid && !recover_channels)
		return -EINVAL;
	if (runtime_context && (!query_info || !load_firmware))
		return -EINVAL;
	if (recover_channels && (!query_info || !probe_rpc || trial_connect ||
			reserve_memory || test_memory_write ||
			prepare_resources || load_firmware || test_firmware_upload || runtime_context))
		return -EINVAL;
	if ((test_memory_write || prepare_resources) && !reserve_memory)
		return -EINVAL;
	if ((load_firmware && !prepare_resources) || (test_firmware_upload && !load_firmware))
		return -EINVAL;
	if (trial_connect && (!query_info || !load_firmware || !probe_rpc ||
			      test_firmware_upload || test_memory_write))
		return -EINVAL;
	if (!enable_probe || pci_domain_nr(pdev->bus) || pdev->bus->number ||
	    pdev->devfn != PCI_DEVFN(14, 0))
		return -ENODEV;
	cap = pci_find_capability(pdev, 0xaa);
	if (!cap || pci_read_config_word(pdev, cap + 2, &tag) || tag != 0xaaaa)
		return -ENODEV;
	if (pci_read_config_word(pdev, PCI_COMMAND, &command) ||
	    !(command & PCI_COMMAND_MEMORY) || (command & PCI_COMMAND_MASTER))
		return -EBUSY;
	if (pci_resource_len(pdev, 0) != SZ_64K ||
	    pci_resource_len(pdev, 1) != SZ_64K ||
	    !(pci_resource_flags(pdev, 0) & IORESOURCE_MEM) ||
	    !(pci_resource_flags(pdev, 1) & IORESOURCE_MEM))
		return -ENODEV;
	/* state is first, preserving pci_get_drvdata's historical ABI. */
	ret = mt_device_profile_select(&profile, pdev->vendor, pdev->device);
	if (ret)
		return ret;
	g = (struct mt_guest *)kzalloc(sizeof(struct mt_guest_device), GFP_KERNEL);
	if (!g)
		return -ENOMEM;
	g->rpc_result = -ENODATA;
	g->version_result = -ENODATA;
	g->memory_result = -ENODATA;
	g->vram_result = -ENODATA;
	g->trial.result = g->trial.connect_result = g->trial.disconnect_result = -ENODATA;
	mt_runtime(g)->result = -ENODATA;
	mutex_init(&g->trial_lock);
	mt_bo_store_init(mt_buffers(g), &g->vram, &g->trial_lock);
	mt_vm_store_init(mt_address_spaces(g), mt_buffers(g));
	mt_gem_store_init(mt_gem(g), mt_buffers(g), &pdev->dev, &profile);
	mt_marker_store_init(mt_markers(g), &g->trial_lock, &g->firmware_queue, &profile);
	mt_markers(g)->can_submit = mt_runtime_can_submit;
	mt_markers(g)->opaque = g;
	mt_markers(g)->buffers = mt_buffers(g);
	mt_execution_store_init(mt_execution(g), mt_buffers(g), &profile);
	mutex_lock(&g->trial_lock);
	ret = pci_enable_device_mem(pdev);
	if (ret)
		goto free_state;
	/* Explicit DMA mask: the device previously inherited 40 bits from the
	 * vendor driver, but a first-bind by this driver would fall back to
	 * the default and fail every dma_map_page in the session DMA service.
	 * 40 bits matches the BAR2 aperture (0x800000000, 1 TB) and system RAM.
	 */
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(40));
	if (ret)
		goto disable;
	ret = pci_request_selected_regions(pdev, BIT(0) | BIT(1), "mt_guest_probe");
	if (ret)
		goto disable;
	g->regs = pci_iomap(pdev, 0, SZ_4K);
	g->custom = pci_iomap(pdev, 1, SZ_4K);
	if (!g->regs || !g->custom) {
		ret = -ENOMEM;
		goto unmap;
	}
	pci_set_drvdata(pdev, g);
	if (query_info) {
		/* Recovery is restricted to the orphaned Guest=1/FW=1 session.
		 * 0x890==2 (trial session active, per mt_runtime_can_submit) is also
		 * accepted when trial_connect: firmware may boot with the session
		 * indicator set (cold boot does not clear it). */
		u32 reg890 = readl(g->regs + 0x890);
		if ((reg890 != (recover_channels ? 1 : 0) &&
		     !(trial_connect && !recover_channels && reg890 == 2)) ||
		    readl(g->regs + 0x898) != 1) {
			ret = -EBUSY;
			goto unmap;
		}
		/* mtkm64.sys 140003af4, suffix 140003d94: Guest COMMAND |= 6.
		 * pci_enable_device_mem above covers MEMORY. Enable MASTER only
		 * for an explicitly requested clean firmware trial, before channel
		 * registration. Retained-session recovery keeps its existing state.
		 */
		if (trial_connect) {
			struct mt_guest_device *d = container_of(g, struct mt_guest_device, state);

			pci_set_master(pdev);
			d->trial_bus_master = true;
			ret = pci_read_config_word(pdev, PCI_COMMAND, &command);
			if (ret || (command & (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) !=
				   (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) {
				ret = -EIO;
				goto unmap;
			}
		}
		g->info = get_zeroed_page(GFP_KERNEL);
		if (!g->info) {
			ret = -ENOMEM;
			goto unmap;
		}
		/* For a full trial, match 140025740: register channels and negotiate
		 * first, then request authoritative memory information. */
		if (!trial_connect && !recover_channels) {
			ret = mt_read_device_info(pdev, g);
			if (ret)
				goto free_info;
		}
	}
	if (probe_rpc) {
		if (!trial_connect && !recover_channels && !g->queried) {
			ret = -EPROTO;
			goto free_info;
		}
		g->rpc_result = mt_probe_channels(pdev, g);
		dev_info(&pdev->dev, "shared channel round-trip result=%d mode=%llu registered=%u\n",
			 g->rpc_result, g->rpc_value, g->registered);
		if ((trial_connect || recover_channels) &&
		    (g->rpc_result || g->version_result || g->registered != 15)) {
			ret = -EPROTO;
			goto free_info;
		}
		if (trial_connect || recover_channels) {
			ret = mt_read_device_info(pdev, g);
			if (ret)
				goto free_info;
			if (refresh_osid) {
				u32 osid = le32_to_cpup((__le32 *)(g->info + 8));
				void *before;

				/* 1400270e0: use the existing OSID, then refresh the
				 * information page. This isolated step makes no claim
				 * that the entire Windows resume sequence is complete.
				 * In particular do not send its later +0x108/+0xf8
				 * completion notifications without rebuilding resources.
				 */
				if (osid != 4) {
					ret = -ENODEV;
					goto free_info;
				}
				before = kmemdup((void *)g->info, 0xcc8, GFP_KERNEL);
				if (!before) {
					ret = -ENOMEM;
					goto free_info;
				}
				writeq(osid, g->custom + 0x110);
				mb();
				ret = mt_read_device_info(pdev, g);
				dev_info(&pdev->dev, "OSID refresh: osid=%u result=%d info_changed=%u guest=%u firmware=%u\n",
					osid, ret, !!memcmp(before, (void *)g->info, 0xcc8),
					readl(g->regs + 0x890), readl(g->regs + 0x898));
				kfree(before);
				if (ret)
					goto free_info;
			}
			ret = mt_shared_announcements((void *)g->info, PAGE_SIZE,
				pci_resource_start(pdev, 2), pci_resource_len(pdev, 2), announcements);
			if (!ret)
				ret = mt_rpc_publish((void *)g->channel[1], g->custom, 0, announcements, 3);
			if (ret)
				goto free_info;
		}
	}
	if (snapshot_memory) {
		g->memory_result = mt_snapshot_memory(pdev, g);
		dev_info(&pdev->dev, "memory snapshot result=%d fw=%#llx shared=%#llx\n",
			 g->memory_result, g->firmware_bar_offset, g->shared_bar_offset);
	}
	if (reserve_memory) {
		g->vram_result = mt_reserve_memory(pdev, g);
		dev_info(&pdev->dev, "private memory reservation result=%d written=%u restored=%u\n",
			 g->vram_result, g->vram_write_performed, g->vram_restore_verified);
		if (g->vram_result) {
			ret = g->vram_result;
			goto free_info;
		}
	}
	if (prepare_resources) {
		ret = mt_reserved_pools_prepare(&g->vram,
			&container_of(g, struct mt_guest_device, state)->reserved_pools, &mt_gem(g)->profile);
		if (ret)
			goto free_info;
		ret = mt_boot_resources_prepare(&g->vram, &g->boot,
				&g->vram_blocks[0], &g->vram_blocks[1], &mt_gem(g)->profile);
		if (ret)
			goto free_info;
		ret = mt_prepare_paging_memory(pdev, g);
		if (ret)
			goto free_info;
		ret = mt_boot_bo_init(&container_of(g, struct mt_guest_device, state)->shared_boot,
			mt_buffers(g), &g->boot,
			&container_of(g, struct mt_guest_device, state)->paging_command,
			&container_of(g, struct mt_guest_device, state)->reserved_pools);
		if (ret)
			goto free_info;
		mt_address_spaces(g)->boot = &container_of(g, struct mt_guest_device, state)->shared_boot;
		dev_info(&pdev->dev, "bootstrap resources reserved; CPU stage=%u bytes, uploaded=0\n",
			 MT_BOOT_STAGE_BYTES);
	}
	if (load_firmware) {
		ret = mt_fw_image_load(&pdev->dev, &g->firmware_image, (void *)g->info, PAGE_SIZE);
		if (ret) {
			dev_err(&pdev->dev, "firmware image validation/load failed: %d\n", ret);
			goto free_info;
		}
	}
	if (runtime_context) {
		ret = mt_runtime_prepare(pdev, g);
		if (ret)
			goto free_info;
	}
	if (trial_connect) {
		ret = mt_trial_prepare(&g->trial, &g->firmware_queue, g->custom,
			g->vram_blocks, &g->boot, g->firmware_image.data);
		if (ret)
			goto free_info;
	}
	ret = sysfs_create_group(&pdev->dev.kobj, &mt_group);
	if (ret)
		goto free_info;
	if (test_firmware_upload) {
		g->firmware_upload_result = mt_test_firmware_upload(g);
		dev_info(&pdev->dev, "firmware memory test: written=%u verified=%u restored=%u result=%d; unpublished\n",
			g->firmware_written, g->firmware_verified, g->firmware_restored, g->firmware_upload_result);
		/* Preserve mappings and backup for inspection if verification failed. */
	}
	if (trial_connect) {
		mt_rpc_service_poll(mt_service(g));
		g->trial.result = runtime_context ? mt_runtime_start(g) :
			mt_trial_run(&g->trial, &mt_guest_trial_ops);
		if (!g->trial.pinned) {
			mt_trial_clear_master(pdev, g);
			mt_release_channels(pdev, g);
		}
		dev_info(&pdev->dev, "firmware trial: connect=%d disconnect=%d restored=%u pinned=%u result=%d\n",
			g->trial.connect_result, g->trial.disconnect_result, g->trial.restored,
			g->trial.pinned, g->trial.result);
	}
	WRITE_ONCE(mt_service(g)->ready, mt_service(g)->running);
	mutex_unlock(&g->trial_lock);
	dev_info(&pdev->dev, "experimental Guest transport bound; trial_bus_master=%u; no render node\n",
		 container_of(g, struct mt_guest_device, state)->trial_bus_master);
	return 0;

free_info:
	WARN_ON(mt_bo_store_fini(mt_buffers(g)));
	mt_trial_clear_master(pdev, g);
	mt_runtime_fini(g);
	mt_trial_fini(&g->trial);
	mt_release_channels(pdev, g);
	mt_fw_image_fini(&g->firmware_image);
	kvfree(g->firmware_backup);
	mt_system_memory_fini(&container_of(g, struct mt_guest_device, state)->paging_command);
	mt_reserved_pools_fini(&g->vram, &container_of(g, struct mt_guest_device, state)->reserved_pools);
	mt_boot_resources_fini(&g->vram, &g->boot);
	mt_vram_fini(&g->vram);
	vfree(g->memory_snapshot);
	if (g->info)
		free_page(g->info);
unmap:
	mt_trial_clear_master(pdev, g);
	if (g->custom)
		pci_iounmap(pdev, g->custom);
	if (g->regs)
		pci_iounmap(pdev, g->regs);
	pci_release_selected_regions(pdev, BIT(0) | BIT(1));
disable:
	pci_disable_device(pdev);
free_state:
	pci_set_drvdata(pdev, NULL);
	mutex_unlock(&g->trial_lock);
	kfree(g);
	return ret;
}

static void mt_remove(struct pci_dev *pdev)
{
	struct mt_guest *g = pci_get_drvdata(pdev);
	/* Normal rmmod is prevented by the self-reference while pinned. An
	 * unexpected PCI removal must not release Host-referenced CPU pages.
	 */
	if (WARN_ON(g->trial.pinned || mt_runtime_context_can_release(mt_runtime(g)) ||
		mt_boot_bo_can_release(&container_of(g, struct mt_guest_device, state)->shared_boot) ||
		mt_buffers(g)->objects || mt_address_spaces(g)->objects || mt_gem(g)->objects ||
		mt_markers(g)->total || mt_execution(g)->processes || mt_execution(g)->contexts))
		return;
	sysfs_remove_group(&pdev->dev.kobj, &mt_group);
	mutex_lock(&g->trial_lock);
	WARN_ON(mt_bo_store_fini(mt_buffers(g)));
	mt_trial_clear_master(pdev, g);
	mt_runtime_fini(g);
	mt_trial_fini(&g->trial);
	mt_release_channels(pdev, g);
	mt_fw_image_fini(&g->firmware_image);
	kvfree(g->firmware_backup);
	mt_system_memory_fini(&container_of(g, struct mt_guest_device, state)->paging_command);
	mt_reserved_pools_fini(&g->vram, &container_of(g, struct mt_guest_device, state)->reserved_pools);
	mt_boot_resources_fini(&g->vram, &g->boot);
	mt_vram_fini(&g->vram);
	vfree(g->memory_snapshot);
	/* The information page is a synchronous copy; retained channels have
	 * already been unregistered above before freeing their backing pages.
	 */
	if (g->info)
		free_page(g->info);
	pci_iounmap(pdev, g->custom);
	pci_iounmap(pdev, g->regs);
	pci_release_selected_regions(pdev, BIT(0) | BIT(1));
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
	mutex_unlock(&g->trial_lock);
	kfree(g);
}



/* r376: Probe-side formal TA VM API.
 * Replaces r375's bridge-side manual VM assembly (which caused oops).
 * Uses proper mt_gpu_vm_init() with synthetic page-table BO (no borrow).
 */

#define MT_PROBE_TA_VM_PT_PAGES  4
#define MT_PROBE_TA_VM_PT_BYTES  (MT_PROBE_TA_VM_PT_PAGES * 4096)

/* Opaque handle for bridge. Definition visible to bridge via header. */
struct mt_probe_ta_vm {
	struct mt_gpu_vm vm;
	struct mt_bo tables;	/* Synthetic BO; page_pa==NULL per init requirement. */
	void *pt_pages;		/* 4 x 4KiB page-table pages (CPU). */
	void *image;		/* VM image buffer. */
	void *scratch;		/* VM scratch buffer. */
};

/* Create a per-file TA VM with proper initialization.
 * Uses synthetic page-table BO (gpu_pa from page_to_phys, page_pa==NULL)
 * following the proven 3D pattern (pvr_gpu_vm_ensure).
 * Returns NULL on failure. */
struct mt_probe_ta_vm *mt_probe_ta_vm_create(void)
{
	struct mt_probe_ta_vm *tvm;
	u64 pt_pa;
	int ret;

	tvm = kzalloc(sizeof(*tvm), GFP_KERNEL);
	if (!tvm)
		return NULL;

	/* Allocate page-table pages (CPU memory, zeroed). */
	tvm->pt_pages = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
						 get_order(MT_PROBE_TA_VM_PT_BYTES));
	if (!tvm->pt_pages)
		goto fail_tvm;
	pt_pa = page_to_phys(virt_to_page(tvm->pt_pages));

	/* Allocate VM image/scratch buffers. */
	tvm->image = kvzalloc(MT_PROBE_TA_VM_PT_BYTES, GFP_KERNEL);
	if (!tvm->image)
		goto fail_pages;
	tvm->scratch = kvzalloc(MT_PROBE_TA_VM_PT_BYTES, GFP_KERNEL);
	if (!tvm->scratch)
		goto fail_image;

	/* Synthetic BO: gpu_pa set, page_pa==NULL (satisfies mt_gpu_vm_init). */
	tvm->tables = (struct mt_bo){
		.backing = {.gpu_pa = pt_pa, .bytes = MT_PROBE_TA_VM_PT_BYTES},
		.ops = &mt_bo_vram_ops,
		.requested_bytes = MT_PROBE_TA_VM_PT_BYTES,
		.refs = 1,
	};

	/* Proper initialization (NOT manual assembly). */
	ret = mt_gpu_vm_init(&tvm->vm, &tvm->tables, tvm->image, tvm->scratch,
			     MT_PROBE_TA_VM_PT_BYTES);
	if (ret)
		goto fail_scratch;

	/* VM holds a reference; release our extra ref. */
	mt_bo_put(&tvm->tables);
	return tvm;

fail_scratch:
	kvfree(tvm->scratch);
fail_image:
	kvfree(tvm->image);
fail_pages:
	free_pages((unsigned long)tvm->pt_pages,
		   get_order(MT_PROBE_TA_VM_PT_BYTES));
fail_tvm:
	kfree(tvm);
	return NULL;
}
EXPORT_SYMBOL_GPL(mt_probe_ta_vm_create);

/* Destroy a TA VM created by mt_probe_ta_vm_create(). */
void mt_probe_ta_vm_destroy(struct mt_probe_ta_vm *tvm)
{
	if (!tvm)
		return;
	mt_gpu_vm_fini(&tvm->vm);
	kvfree(tvm->scratch);
	kvfree(tvm->image);
	free_pages((unsigned long)tvm->pt_pages,
		   get_order(MT_PROBE_TA_VM_PT_BYTES));
	kfree(tvm);
}
EXPORT_SYMBOL_GPL(mt_probe_ta_vm_destroy);

/* Bind mappings to a TA VM. Wrapper for mt_gpu_vm_bind_many().
 * Compiled in probe; no cross-module issues (static inline in header,
 * but called here so the VM pointer is validated in-probe). */
int mt_probe_ta_vm_bind(struct mt_probe_ta_vm *tvm,
			const struct mt_vm_binding *bindings, u32 count)
{
	if (!tvm)
		return -EINVAL;
	return mt_gpu_vm_bind_many(&tvm->vm, bindings, count);
}
EXPORT_SYMBOL_GPL(mt_probe_ta_vm_bind);

/* Safe cross-module borrow wrapper.
 * Compiled in probe, so &mt_bo_vram_ops refers to probe's copy,
 * matching the probe store's ops. Bridge must NOT call
 * mt_bo_system_borrow() directly (address mismatch). */
int mt_probe_bo_borrow(struct mt_bo *bo, struct mt_guest *g,
		       struct mt_system_memory *m)
{
	struct mt_guest_device *d;
	int ret;

	if (!bo || !g || !m)
		return -EINVAL;
	d = container_of(g, struct mt_guest_device, state);
	mutex_lock(d->buffers.lock);
	ret = mt_bo_system_borrow(bo, &d->buffers, m);
	mutex_unlock(d->buffers.lock);
	return ret;
}
EXPORT_SYMBOL_GPL(mt_probe_bo_borrow);


static const struct pci_device_id mt_ids[] = {
	{ PCI_DEVICE_SUB(0x1ed5, 0x0222, 0x1ed5, 0x1101) },
	{ }
};
/* No modalias export: this module is loaded manually for development only. */
static struct pci_driver mt_driver = {
	.name = "mt_guest_probe", .id_table = mt_ids,
	.probe = mt_probe, .remove = mt_remove,
	.driver = { .suppress_bind_attrs = true },
};
module_pci_driver(mt_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Experimental Moore Threads Guest protocol bring-up");
