// SPDX-License-Identifier: GPL-2.0
/* Stage B minimal PVR bridge: a DRM node the legacy MASA user-mode driver can
 * talk to.
 *
 * The driver finds a node by asking for DRM_IOCTL_VERSION and comparing the
 * returned name against "pvr" (strcmp at 0xa4989), then drives two PVR ioctls:
 * a dispatch packet (0xc0206440) carrying bridge id, function id and two user
 * buffers, and an init call (0x40046445). This module implements the 19
 * commands a connect -> device memory -> render context -> sync session
 * exercises, per reports/stage-b-kernel-bridge-design.md.
 *
 * Stage 1 scope: the node serves the ioctls and owns real kernel objects (PMRs
 * are system memory, handles come from a real allocator, heap geometry comes
 * from the plan that matches the vendor table). It does NOT bind the PCI
 * device and performs no MMIO -- the live mt_guest_probe session owns
 * 00:0e.0, and the acceptance test here is the offline UMD driving real ioctls
 * and reproducing the trace captured in bA13. Page tables, fence signalling and
 * submission are later stages.
 */
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_ioctl.h>
#include <linux/ioctl.h>
#include <linux/kref.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include "../mt_pvr_device.h"
#include "../mt_pvr_queue.h"

/* The driver hard-codes these two numbers, so they are spelled out rather than
 * derived: 0xc0206440 = _IOWR('d', 0x40, 32 bytes), 0x40046445 = _IO('d', 0x45, u32).
 */
#define DRM_IOCTL_PVR_BRIDGE _IOWR('d', 0x40, struct mt_pvr_cmd)
#define DRM_IOCTL_PVR_INIT _IO('d', 0x45)

#define MT_PVR_DRV_NAME "pvr"

/* Bridge groups we serve. Anything else is rejected instead of guessed at. */
#define MT_PVR_BRIDGE_SRVCORE 0x1U
#define MT_PVR_BRIDGE_SYNC 0x2U
#define MT_PVR_BRIDGE_MM 0x6U
#define MT_PVR_BRIDGE_RGXTA3D 0x82U
#define MT_PVR_BRIDGE_RGXHWPERF 0x86U

/* The one device the main module drives. */
#define MT_PVR_PCI_DEVFN PCI_DEVFN(14, 0)

enum mt_pvr_kind {
	MT_PVR_KIND_HEAP,
	MT_PVR_KIND_EVENT,
	MT_PVR_KIND_SYNC,
	MT_PVR_KIND_CONTEXT,
	MT_PVR_KIND_RESERVATION,
};

struct mt_pvr_pmr {
	struct list_head link;
	u64 handle;
	u64 bytes;
	void *host;		/* system memory until page tables exist */
	u32 log2_page_size;
	u32 mapped;
};

struct mt_pvr_object {
	struct list_head link;
	u64 handle;
	u32 kind;
};

struct mt_pvr_file {
	struct kref ref;
	struct mutex lock;
	struct mt_pvr_handles handles;
	struct mt_pvr_queue queue;
	struct mt_pvr_heap_table heaps;
	struct mt_pvr_conn *conn;
	struct mt_pvr_features *features;
	void *info_page;
	struct list_head pmrs;
	struct list_head objects;
	u32 init_module;
};

static struct drm_device *pvr_drm;
static bool pvr_ready;

static void pvr_file_release(struct kref *kref)
{
	struct mt_pvr_file *file = container_of(kref, struct mt_pvr_file, ref);
	struct mt_pvr_pmr *pmr, *tmp;
	struct mt_pvr_object *obj, *otmp;

	list_for_each_entry_safe(pmr, tmp, &file->pmrs, link) {
		list_del(&pmr->link);
		vfree(pmr->host);
		kfree(pmr);
	}
	list_for_each_entry_safe(obj, otmp, &file->objects, link) {
		list_del(&obj->link);
		kfree(obj);
	}
	vfree(file->info_page);
	kfree(file->features);
	kfree(file->conn);
	kfree(file);
}

/* True when the main module still owns the GPU. Stage 1 must never race it. */
static bool pvr_device_owned_by_main(void)
{
	struct pci_dev *pdev = pci_get_domain_bus_and_slot(0, 0, MT_PVR_PCI_DEVFN);
	bool owned;

	if (!pdev)
		return false;
	owned = pdev->driver &&
		!strcmp(pdev->driver->driver.name, "mt_guest_probe");
	pci_dev_put(pdev);
	return owned;
}

static int pvr_open(struct drm_device *drm, struct drm_file *drm_file)
{
	static const char *const heap_names[MT_PVR_HEAP_COUNT] = {
		"General", NULL, NULL, NULL, "Component Control", NULL, NULL,
		"PDS Code and Data", "USC Code", NULL, NULL,
	};
	struct mt_guest_heap_plan plan;
	struct mt_pvr_file *file;

	if (!READ_ONCE(pvr_ready))
		return -ENODEV;
	file = kzalloc(sizeof(*file), GFP_KERNEL);
	if (!file)
		return -ENOMEM;
	kref_init(&file->ref);
	mutex_init(&file->lock);
	INIT_LIST_HEAD(&file->pmrs);
	INIT_LIST_HEAD(&file->objects);
	mt_pvr_handles_init(&file->handles);
	mt_pvr_queue_init(&file->queue, MT_PVR_RING_ENTRIES);
	mt_guest_plan_heaps(&plan);
	mt_pvr_heaps_init(&file->heaps, &plan, heap_names);
	/* GetFeatures(conn) returns conn+0xa0+0x620, so the block the UMD reads
	 * starts 0x620 bytes into the allocation.
	 */
	file->features = kzalloc(MT_PVR_FEATURE_SKEW + sizeof(*file->features),
				 GFP_KERNEL);
	file->info_page = vzalloc(MT_PVR_INFO_BYTES);
	file->conn = kzalloc(sizeof(*file->conn), GFP_KERNEL);
	if (!file->features || !file->info_page || !file->conn) {
		kref_put(&file->ref, pvr_file_release);
		return -ENOMEM;
	}
	mt_pvr_info_page_init(file->info_page, MT_PVR_INFO_BYTES);
	mt_pvr_features_init((struct mt_pvr_features *)
			     ((char *)file->features + MT_PVR_FEATURE_SKEW), 1);
	drm_file->driver_priv = file;
	return 0;
}

static void pvr_postclose(struct drm_device *drm, struct drm_file *drm_file)
{
	struct mt_pvr_file *file = drm_file->driver_priv;

	(void)drm;
	if (!file)
		return;
	drm_file->driver_priv = NULL;
	mutex_lock(&file->lock);
	mutex_unlock(&file->lock);
	kref_put(&file->ref, pvr_file_release);
}

static struct mt_pvr_pmr *pvr_pmr_find(struct mt_pvr_file *file, u64 handle)
{
	struct mt_pvr_pmr *pmr;

	list_for_each_entry(pmr, &file->pmrs, link)
		if (pmr->handle == handle)
			return pmr;
	return NULL;
}

static struct mt_pvr_pmr *pvr_pmr_new(struct mt_pvr_file *file, u64 bytes,
				     u32 log2_page_size)
{
	struct mt_pvr_pmr *pmr;

	pmr = kzalloc(sizeof(*pmr), GFP_KERNEL);
	if (!pmr)
		return NULL;
	pmr->host = vzalloc(bytes ? bytes : 1);
	if (!pmr->host) {
		kfree(pmr);
		return NULL;
	}
	if (mt_pvr_handles_alloc(&file->handles, &pmr->handle)) {
		vfree(pmr->host);
		kfree(pmr);
		return NULL;
	}
	pmr->bytes = bytes;
	pmr->log2_page_size = log2_page_size;
	list_add_tail(&pmr->link, &file->pmrs);
	return pmr;
}

static struct mt_pvr_object *pvr_object_new(struct mt_pvr_file *file, u32 kind)
{
	struct mt_pvr_object *obj = kzalloc(sizeof(*obj), GFP_KERNEL);

	if (!obj)
		return NULL;
	if (mt_pvr_handles_alloc(&file->handles, &obj->handle)) {
		kfree(obj);
		return NULL;
	}
	obj->kind = kind;
	list_add_tail(&obj->link, &file->objects);
	return obj;
}

static struct mt_pvr_object *pvr_object_of_kind(struct mt_pvr_file *file,
						u32 kind)
{
	struct mt_pvr_object *obj;

	list_for_each_entry(obj, &file->objects, link)
		if (obj->kind == kind)
			return obj;
	return NULL;
}

static int pvr_out(struct mt_pvr_cmd *cmd, const void *src, size_t bytes)
{
	if (cmd->out_size < bytes)
		return -EINVAL;
	if (copy_to_user(u64_to_user_ptr(cmd->out_ptr), src, bytes))
		return -EFAULT;
	return 0;
}

static int pvr_in(struct mt_pvr_cmd *cmd, void *dst, size_t bytes)
{
	if (cmd->in_size < bytes)
		return -EINVAL;
	if (copy_from_user(dst, u64_to_user_ptr(cmd->in_ptr), bytes))
		return -EFAULT;
	return 0;
}

/* Copy a heap name into the caller's buffer, honouring the length it passes.
 * The driver reads the name back out of its own heap object, and bA5 found the
 * device-memory context failing until "USC Code" was supplied, so this copy is
 * load-bearing rather than cosmetic.
 */
static int pvr_copy_heap_name(struct mt_pvr_file *file, u32 index, u32 length,
			      u64 user_buffer)
{
	const char *name;
	char *buffer;
	int ret = 0;

	if (!length)
		return 0;
	name = file->heaps.entries[index].name;
	buffer = kzalloc(length, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;
	if (name)
		strscpy(buffer, name, length);
	if (copy_to_user(u64_to_user_ptr(user_buffer), buffer, length))
		ret = -EFAULT;
	kfree(buffer);
	return ret;
}

static int pvr_cmd_connect(struct mt_pvr_file *file, struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_connect_out out;

	mt_pvr_connect_result(&out);
	return pvr_out(cmd, &out, sizeof(out));
}

static int pvr_cmd_event_handle(struct mt_pvr_file *file,
				struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_handle_out out = { 0 };
	struct mt_pvr_object *obj;

	obj = pvr_object_new(file, MT_PVR_KIND_EVENT);
	if (!obj)
		return -ENOMEM;
	out.handle = obj->handle;
	return pvr_out(cmd, &out, sizeof(out));
}

static int pvr_cmd_info_page(struct mt_pvr_file *file, struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_handle_out out = { 0 };
	struct mt_pvr_pmr *pmr;

	pmr = pvr_pmr_new(file, MT_PVR_INFO_BYTES, 12);
	if (!pmr)
		return -ENOMEM;
	memcpy(pmr->host, file->info_page, MT_PVR_INFO_BYTES);
	/* The driver mmaps at handle << 12, so hand out the offset and let the
	 * import step derive the handle (bA15: 28 of 28 calls agreed).
	 */
	out.handle = mt_pvr_offset_of(pmr->handle);
	return pvr_out(cmd, &out, sizeof(out));
}

static int pvr_cmd_heap_count(struct mt_pvr_file *file, struct mt_pvr_cmd *cmd)
{
	u32 count = mt_pvr_heaps_count(&file->heaps);

	return pvr_out(cmd, &count, sizeof(count));
}

static int pvr_cmd_heap_details(struct mt_pvr_file *file,
				struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_heap_details_in in;
	struct mt_pvr_heap_details_out out = { 0 };
	const struct mt_pvr_heap_entry *entry;
	u32 index;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	index = in.heap_config_index;
	if (index >= file->heaps.count)
		return -EINVAL;
	entry = &file->heaps.entries[index];
	out.base = entry->base;
	out.length = entry->size;
	out.log2_data_page_size = entry->log2_page_size;
	out.log2_import_alignment = entry->log2_page_size;
	out.heap_name_out = in.heap_name_out;
	ret = pvr_copy_heap_name(file, index, in.heap_name_buf_size,
				 in.heap_name_out);
	if (ret)
		return ret;
	return pvr_out(cmd, &out, sizeof(out));
}

static int pvr_cmd_ctx_create(struct mt_pvr_file *file, struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_ctx_create_out out = { 0 };
	struct mt_pvr_object *ctx;
	u32 i;

	/* The driver refcounts contexts per connection, so a second create must
	 * return the same object rather than a new one.
	 */
	ctx = pvr_object_of_kind(file, MT_PVR_KIND_CONTEXT);
	if (!ctx) {
		ctx = pvr_object_new(file, MT_PVR_KIND_CONTEXT);
		if (!ctx)
			return -ENOMEM;
		for (i = 0; i < file->heaps.count; i++) {
			if (!pvr_object_new(file, MT_PVR_KIND_HEAP))
				return -ENOMEM;
		}
		file->conn->devmem_ctx = ctx->handle;
		file->conn->devmem_refs++;
	}
	out.server_context = ctx->handle;
	out.priv_data = ctx->handle;
	out.cpu_cache_line_size = 64;
	return pvr_out(cmd, &out, sizeof(out));
}

static int pvr_cmd_pmr_alloc(struct mt_pvr_file *file, struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_pmr_in in;
	struct mt_pvr_pmr_out out = { 0 };
	struct mt_pvr_pmr *pmr;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	pmr = pvr_pmr_new(file, in.size ? in.size : in.chunk_size,
			  in.log2_page_size ? in.log2_page_size : 12);
	if (!pmr)
		return -ENOMEM;
	out.pmr = pmr->handle;
	out.out_flags = in.flags;
	out.is_system_mem = 1;
	return pvr_out(cmd, &out, sizeof(out));
}

static int pvr_cmd_pmr_import(struct mt_pvr_file *file, struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_import_in in;
	struct mt_pvr_import_out out = { 0 };
	struct mt_pvr_pmr *pmr;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	pmr = pvr_pmr_find(file, in.ext_handle);
	if (!pmr)
		return -ENOENT;
	out.align = 1ULL << pmr->log2_page_size;
	out.size = pmr->bytes;
	out.pmr = mt_pvr_offset_of(pmr->handle);
	return pvr_out(cmd, &out, sizeof(out));
}

static int pvr_cmd_pmr_map(struct mt_pvr_file *file, struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_map_in in;
	struct mt_pvr_map_out out = { 0 };
	struct mt_pvr_pmr *pmr;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	pmr = pvr_pmr_find(file, in.pmr);
	if (!pmr)
		return -ENOENT;
	pmr->mapped = 1;
	out.mapping = pmr->handle;
	return pvr_out(cmd, &out, sizeof(out));
}

static int pvr_cmd_pmr_reserve(struct mt_pvr_file *file, struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_reserve_in in;
	struct mt_pvr_reserve_out out = { 0 };
	struct mt_pvr_object *obj;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	obj = pvr_object_new(file, MT_PVR_KIND_RESERVATION);
	if (!obj)
		return -ENOMEM;
	out.reservation = obj->handle;
	return pvr_out(cmd, &out, sizeof(out));
}

static int pvr_cmd_sync_block(struct mt_pvr_file *file, struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_sync_block_in in;
	struct mt_pvr_sync_block_out out = { 0 };
	struct mt_pvr_object *obj;
	struct mt_pvr_pmr *pmr;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	/* Only the memType the driver actually sends is served. Succeeding on
	 * any other value would hide a bridge we have not implemented.
	 */
	if (in.mem_type != MT_PVR_SYNC_MEM_TYPE)
		return -ENOTTY;
	obj = pvr_object_new(file, MT_PVR_KIND_SYNC);
	if (!obj)
		return -ENOMEM;
	pmr = pvr_pmr_new(file, 0x1000, 12);
	if (!pmr)
		return -ENOMEM;
	out.sync_handle = obj->handle;
	out.sync_pmr = pmr->handle;
	out.block_size = 0x1000;
	out.vaddr = mt_pvr_offset_of(pmr->handle);
	return pvr_out(cmd, &out, sizeof(out));
}

static int pvr_cmd_handle_only(struct mt_pvr_file *file,
			       struct mt_pvr_cmd *cmd, u32 kind)
{
	struct mt_pvr_handle_out out = { 0 };
	struct mt_pvr_object *obj;

	obj = pvr_object_new(file, kind);
	if (!obj)
		return -ENOMEM;
	out.handle = obj->handle;
	return pvr_out(cmd, &out, sizeof(out));
}

static int pvr_cmd_hwperf(struct mt_pvr_file *file, struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_handle_out out = { 0 };

	/* The offline session passed with a zeroed block; do not invent
	 * counters we cannot back.
	 */
	return pvr_out(cmd, &out, sizeof(out));
}

static int pvr_bridge_dispatch(struct mt_pvr_file *file, u32 bridge,
			       u32 function, struct mt_pvr_cmd *cmd)
{
	if (!file->conn->srv_handle)
		return -ENOTCONN;
	switch (bridge) {
	case MT_PVR_BRIDGE_SRVCORE:
		switch (function) {
		case 0x0:
			return pvr_cmd_connect(file, cmd);
		case 0x1:			/* Disconnect */
		case 0x10:			/* ReleaseInfoPage */
			return 0;
		case 0x2:			/* AcquireGlobalEventObject */
		case 0x4:			/* EventObjectOpen */
			return pvr_cmd_event_handle(file, cmd);
		case 0x3:			/* ReleaseGlobalEventObject */
		case 0x5:			/* EventObjectWait */
		case 0x6:			/* EventObjectClose */
		case 0xa:			/* AlignmentCheck */
		case 0xc:			/* GetMultiCoreInfo */
		case 0xd:			/* EventObjectWaitTimeout */
			return 0;
		case 0xf:			/* AcquireInfoPage */
			return pvr_cmd_info_page(file, cmd);
		default:
			return -ENOTTY;
		}
	case MT_PVR_BRIDGE_SYNC:
		switch (function) {
		case 0x0:
			return pvr_cmd_sync_block(file, cmd);
		case 0x1:			/* FreeSyncPrimitiveBlock */
		case 0x7:			/* SyncAllocEvent */
			return 0;
		default:
			return -ENOTTY;
		}
	case MT_PVR_BRIDGE_MM:
		switch (function) {
		case 0x3:			/* PmrMakeLocalImportHandle */
		case 0x6:			/* PmrLocalImportPmr */
			return pvr_cmd_pmr_import(file, cmd);
		case 0x7:			/* PmrUnrefPmr */
		case 0x10:			/* DevmemIntCtxDestroy */
		case 0x12:			/* DevmemIntHeapDestroy */
			return 0;
		case 0x9:			/* PhysMemNewRamBackedPmr */
			return pvr_cmd_pmr_alloc(file, cmd);
		case 0xf:			/* DevmemIntCtxCreate */
			return pvr_cmd_ctx_create(file, cmd);
		case 0x11:			/* DevmemIntHeapCreate */
		case 0x13:			/* DevmemIntMapPmr */
			return pvr_cmd_pmr_map(file, cmd);
		case 0x15:			/* DevmemIntReserveRange */
			return pvr_cmd_pmr_reserve(file, cmd);
		case 0x1e:			/* HeapCfgHeapCount */
			return pvr_cmd_heap_count(file, cmd);
		case 0x20:			/* HeapCfgHeapDetails */
			return pvr_cmd_heap_details(file, cmd);
		default:
			return -ENOTTY;
		}
	case MT_PVR_BRIDGE_RGXTA3D:
		/* RGXCreateRenderContext: the driver allocates a dozen PMRs
		 * around this call and only needs a nonzero context back.
		 */
		if (function == 0x8)
			return pvr_cmd_handle_only(file, cmd,
						    MT_PVR_KIND_CONTEXT);
		return -ENOTTY;
	case MT_PVR_BRIDGE_RGXHWPERF:
		if (function == 0x4)		/* RGXAcquireHWPerfSetting */
			return pvr_cmd_hwperf(file, cmd);
		return -ENOTTY;
	default:
		return -ENOTTY;
	}
}

static int pvr_ioctl_bridge(struct drm_device *drm, void *raw,
			    struct drm_file *drm_file)
{
	struct mt_pvr_file *file = drm_file->driver_priv;
	struct mt_pvr_cmd cmd;
	int ret;

	(void)drm;
	if (!file)
		return -ENODEV;
	if (copy_from_user(&cmd, raw, sizeof(cmd)))
		return -EFAULT;
	mutex_lock(&file->lock);
	ret = pvr_bridge_dispatch(file, cmd.bridge_id, cmd.function_id, &cmd);
	mutex_unlock(&file->lock);
	return ret;
}

static int pvr_ioctl_init(struct drm_device *drm, void *raw,
			  struct drm_file *drm_file)
{
	struct mt_pvr_file *file = drm_file->driver_priv;
	u32 init_module;

	(void)drm;
	if (!file)
		return -ENODEV;
	if (copy_from_user(&init_module, raw, sizeof(init_module)))
		return -EFAULT;
	/* 1 selects the generic connection, 2 the device connection the render
	 * path needs (bA10).
	 */
	if (init_module != 1 && init_module != 2)
		return -EINVAL;
	mutex_lock(&file->lock);
	file->init_module = init_module;
	mt_pvr_conn_init(file->conn, (u32)task_pid_nr(current), init_module);
	file->conn->features = (u64)(uintptr_t)file->features;
	file->conn->info_page = (u64)(uintptr_t)file->info_page;
	file->conn->hwperf_um = (u64)(uintptr_t)file->conn;
	mutex_unlock(&file->lock);
	return 0;
}

static const struct drm_ioctl_desc pvr_ioctls[] = {
	DRM_IOCTL_DEF_DRV(PVR_BRIDGE, pvr_ioctl_bridge, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(PVR_INIT, pvr_ioctl_init, DRM_RENDER_ALLOW),
};

static const struct file_operations pvr_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.compat_ioctl = drm_compat_ioctl,
};

static const struct drm_driver pvr_driver = {
	.driver_features = DRIVER_RENDER | DRIVER_SYNCOBJ,
	.open = pvr_open, .postclose = pvr_postclose,
	.ioctls = pvr_ioctls, .num_ioctls = ARRAY_SIZE(pvr_ioctls),
	.fops = &pvr_fops,
	.name = MT_PVR_DRV_NAME,
	.desc = "MT vGPU PVR Services bridge for the legacy MASA user-mode driver",
	.major = 0, .minor = 1, .patchlevel = 0,
};

static int __init pvr_start(void)
{
	struct pci_dev *pdev;
	struct drm_device *drm;
	int ret;

	/* Attach the node to the GPU without claiming it: the main module keeps
	 * the driver binding, and stage 1 only needs a parent for the node.
	 */
	pdev = pci_get_domain_bus_and_slot(0, 0, MT_PVR_PCI_DEVFN);
	if (!pdev)
		return -ENODEV;
	if (pdev->vendor != 0x1ed5 || pdev->device != 0x0222) {
		pci_dev_put(pdev);
		return -ENODEV;
	}
	drm = drm_dev_alloc(&pvr_driver, &pdev->dev);
	if (IS_ERR(drm)) {
		ret = PTR_ERR(drm);
		pci_dev_put(pdev);
		return ret;
	}
	ret = drm_dev_register(drm, 0);
	if (ret) {
		drm_dev_put(drm);
		pci_dev_put(pdev);
		return ret;
	}
	pci_dev_put(pdev);
	pvr_drm = drm;
	WRITE_ONCE(pvr_ready, true);
	pr_info("mt_pvr_bridge: registered '%s' node, bridge stage 1: main module %s\n",
		MT_PVR_DRV_NAME,
		pvr_device_owned_by_main() ? "still owns the device (no binding)"
					   : "not bound");
	return 0;
}

static void __exit pvr_stop(void)
{
	WRITE_ONCE(pvr_ready, false);
	if (pvr_drm) {
		drm_dev_unregister(pvr_drm);
		drm_dev_put(pvr_drm);
		pvr_drm = NULL;
	}
	pr_info("mt_pvr_bridge: unloaded cleanly\n");
}

module_init(pvr_start);
module_exit(pvr_stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Minimal PVR Services bridge for the legacy MASA UMD");
