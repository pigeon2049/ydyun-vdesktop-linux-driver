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
#include <linux/anon_inodes.h>
#include <linux/fdtable.h>
#include <linux/ioctl.h>
#include <linux/poll.h>
#include <linux/kref.h>
#include <linux/mm.h>
#include <linux/pgtable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include "../mt_pvr_device.h"
#include "../mt_pvr_queue.h"
#include "../mt_pvr_session.h"
#include "../mt_mmu.h"
#include "../mt_guest_device.h"
#include <linux/dma-mapping.h>

/* The driver hard-codes these two numbers, and the vendor declares them the
 * same way in inc/pvr/include/pvr_drm.h:
 *
 *   DRM_PVR_SRVKM_CMD  0  ->  _IOWR('d', 0x40, struct drm_pvr_srvkm_cmd)  = 0xc0206440
 *   DRM_PVR_SRVKM_INIT 5  ->  _IOW ('d', 0x45, struct drm_pvr_srvkm_init_data) = 0x40046445
 *
 * INIT's size field is load-bearing, not decoration. _IOC_SIZE(0x40046445) is
 * 4 because the payload is a __u32, and drm_ioctl() copy_from_user()s exactly
 * that many bytes into the kernel buffer it hands the driver. Declared as plain
 * _IO the size field is 0, nothing is copied, and drm_pvr_srvkm_init() would
 * read uninitialised stack. Do not "simplify" this back to _IO.
 */
struct mt_pvr_init_data {
	u32 init_module;
};

struct mt_pvr_sync_rename_data {
	char name[32];
};

/* NOTE (r134): this writes a kernel-side block the vendor UMD never reads;
 * the real gate is drm_major below. Kept for the offline model.
 * features+0x54 advertisement. 0 (default) keeps the validated legacy sync
 * allocation path; >= 2 lets the UMD reach DDK2 (r78). Experiment only; read
 * once per open.
 */
static unsigned int drm_major;
module_param(drm_major, uint, 0400);
MODULE_PARM_DESC(drm_major, "DRM version_major reported to the UMD (0 default; 2 selects DDK2 path, r134)");

static unsigned int ddk_feature_set;
module_param(ddk_feature_set, uint, 0400);
MODULE_PARM_DESC(ddk_feature_set, "features+0x54 DDK feature set (0=legacy path)");

#define DRM_IOCTL_PVR_BRIDGE _IOWR('d', 0x40, struct mt_pvr_cmd)
#define DRM_IOCTL_PVR_INIT _IOW('d', 0x45, struct mt_pvr_init_data)
#define DRM_IOCTL_PVR_SYNC_RENAME _IOW('d', 0x41, struct mt_pvr_sync_rename_data)

/* These numbers are wire contract, so pin them rather than trusting the spelling
 * above to keep matching the vendor UMD.
 */
static_assert(DRM_IOCTL_PVR_BRIDGE == 0xc0206440, "bridge ioctl number drifted");
static_assert(DRM_IOCTL_PVR_INIT == 0x40046445, "init ioctl number drifted");
static_assert(DRM_IOCTL_PVR_SYNC_RENAME == 0x40206441, "sync rename ioctl number drifted");
static_assert(_IOC_SIZE(DRM_IOCTL_PVR_BRIDGE) == sizeof(struct mt_pvr_cmd),
	      "bridge payload must be copied by drm_ioctl");
static_assert(_IOC_SIZE(DRM_IOCTL_PVR_INIT) == 4,
	      "init payload must be 4 bytes or the argument never reaches us");
static_assert(_IOC_SIZE(DRM_IOCTL_PVR_SYNC_RENAME) ==
	      sizeof(struct mt_pvr_sync_rename_data),
	      "sync rename payload must be copied by drm_ioctl");

#define MT_PVR_DRV_NAME "pvr"

/* Ceiling for a single mmap of a bridge PMR. Stage-one PMRs are the info page
 * and sync blocks, both small; this exists so a malformed offset cannot ask
 * remap_vmalloc_range() to walk an arbitrary span.
 */
#define MT_PVR_MAX_MAP_BYTES (16U << 20)

/* Bridge groups we serve. Anything else is rejected instead of guessed at. */
#define MT_PVR_BRIDGE_SRVCORE 0x1U
#define MT_PVR_BRIDGE_SYNC 0x2U
#define MT_PVR_BRIDGE_MM 0x6U
#define MT_PVR_BRIDGE_RGXCOMPUTE 0x81U
#define MT_PVR_BRIDGE_RGXTA3D 0x82U
#define MT_PVR_BRIDGE_RGXHWPERF 0x86U

#define MT_PVR_BRIDGE_RGXTDM 0x89U
#define MT_PVR_BRIDGE_RGXKICKSYNC 0x88U

/* The one device the main module drives. */
#define MT_PVR_PCI_DEVFN PCI_DEVFN(14, 0)

enum mt_pvr_kind {
	MT_PVR_KIND_HEAP,
	MT_PVR_KIND_EVENT,
	MT_PVR_KIND_SYNC,
	MT_PVR_KIND_CONTEXT,
	MT_PVR_KIND_RESERVATION,
	MT_PVR_KIND_COMPUTE,
	MT_PVR_KIND_ZSBUFFER,
	/* A kick-sync context is a CONTEXT-shaped object but must never be
	 * mistaken for a render context: ctx_create() reuses the first object
	 * of its kind, so sharing the kind would alias the two.
	 */
	MT_PVR_KIND_KICKSYNC,
};

struct mt_pvr_pmr {
	struct list_head link;
	u64 handle;
	u64 bytes;
	void *host;		/* system memory until page tables exist */
	/* S4-3 system-page handoff. dma_addrs owns DMA API mappings; gpu_pages
	 * contains addresses translated for GPU PTEs. The optional gpu_bo is a
	 * CPU-only page-table planner object until an execution path is wired.
	 */
	struct mt_pvr_dma_page *dma_addrs;
	u64 *gpu_pages;
	u32 dma_npages;
	/* CPU-only BO facade consumed by mt_gpu_vm_bind_many. */
	struct mt_bo gpu_bo;
	bool gpu_bo_ready;
	/* Keep the exact DMA device used for map until the matching unmap. The
	 * PCI reference keeps the struct device alive if the driver is detached.
	 */
	struct pci_dev *dma_pdev;
	/* Owner ref from try_module_get() at register time, balanced by
	 * exactly one module_put() in pvr_pmr_dma_release(). Releasing
	 * against the CURRENT owner instead would imbalance a changed
	 * module; this pointer cannot dangle because the ref itself pins
	 * the module against unload.
	 */
	struct module *dma_owner;
	u32 log2_page_size;
	/* Allocation flags from 0x6:0x9 (PVRSRV_MEMALLOCFLAG bits). Recorded
	 * for the future translator's PTE policy; Stage 1 ignores them.
	 */
	u32 alloc_flags;
	/* Live DevmemIntMapPMR count, and the reservation the current mapping
	 * was programmed into. The OUT mapping value stays the PMR handle (the
	 * UMD passes it back to UnmapPMR), so this is bookkeeping only -- but
	 * it is what lets unreserve refuse while mappings are live instead of
	 * silently dropping a range the page tables still reference.
	 */
	u32 mapped;
	u64 mapped_reservation;
	/*
	 * Live references. The PMR's own presence on file->pmrs counts as one,
	 * so a freshly created PMR starts at 1 and only the list owner can free
	 * it. Anything that uses the pointer outside file->lock must take its own
	 * reference first -- see pvr_mmap(), which is the only such path today.
	 */
	u32 refcount;
	/* Arena slot when backing comes from the file arena (0 = private
	 * vzalloc fallback). host always points at the first byte either way,
	 * so mmap/DMA/plan paths are unchanged.
	 */
	u32 arena_offset;
	u32 arena_pages;
	/* Owning file for arena segment reclaim. Set once at creation under
	 * file->lock; the arena outlives every PMR slot carved from it.
	 */
	struct mt_pvr_file *file;
};

/* One free run inside the file arena, in pages. Sorted by offset so
 * neighbors merge on free.
 */
struct mt_pvr_arena_seg {
	struct list_head link;
	u32 offset;
	u32 pages;
};

/* Single arena per file: the largest live ladder needs ~370 KiB (12 PMRs
 * plus info/sync blocks); 2 MiB leaves headroom for further contexts while
 * staying a cheap vzalloc. Larger requests bypass the arena entirely.
 */
#define MT_PVR_ARENA_BYTES (2U << 20)
#define MT_PVR_ARENA_PAGES (MT_PVR_ARENA_BYTES >> PAGE_SHIFT)

struct mt_pvr_object {
	struct list_head link;
	u64 handle;
	u32 kind;
	/* Payload by kind. RESERVATION carries the VA range the UMD reserved;
	 * everything else leaves these zero. The range is what a future GPU
	 * page-table bind will program; recording it now (with overlap checks)
	 * is what makes that bind possible later without changing the wire.
	 */
	u64 arg0;
	u64 arg1;
};

/* One PVR MapPMR range. The wire ledger remains byte-exact; aligned entries
 * additionally feed the per-file CPU-only mt_gpu_vm plan. Unaligned entries
 * degrade from the plan without changing the Stage-1 UMD result.
 */
struct mt_pvr_binding {
	struct list_head link;
	u64 va;
	u64 bytes;
	u64 pmr;
	u64 reservation;
	u32 gpu_bytes;
	int gpu_result;
	bool gpu_bound;
	/* Cover set for the plan binding: first VA page + page count. The
	 * aligned single-range path covers exactly bytes>>12 pages; the
	 * unaligned path covers [va&~4095, va+bytes) rounded up. Unbind walks
	 * exactly this set.
	 */
	u64 gpu_first;
	u32 gpu_npages;
	/* Map flags from 0x6:0x13 (same MEMALLOCFLAG domain). Live rung8
	 * values echo the PMR alloc flags: 0x333 (GPU+CPU R/W, GPU
	 * incoherent), 0x1233 (plus CPU coherent, the two big heap PMRs),
	 * 0x303 (GPU-only R/W). All observed are GPU readable AND writable,
	 * so the CPU-only plan's DEFAULT mapping stays consistent; a future
	 * translator must derive PTE read-only/coherent bits from these.
	 */
	u32 map_flags;
};

/* Ledger cap: thousands of mappings would already have exhausted the UMD's
 * own arenas long before this. Unbounded growth on a confused caller is a
 * leak, not a feature.
 */
#define MT_PVR_MAX_BINDINGS 512U

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
	struct list_head bindings;
	/* Unpublished, CPU-only page-table plan; never uploaded or executed here. */
	struct mt_gpu_vm gpu_vm;
	struct mt_bo gpu_tables;
	void *gpu_vm_storage;
	bool gpu_vm_ready;
	/* Per-file PMR backing arena (r55 answer to byte-tight PMRs). PMR
	 * bytes live at arena_base + arena_offset so VA-neighbor ranges can
	 * share physical pages once the plan binds per-page cover sets.
	 * Lazy: files that never allocate PMRs pay nothing. Fallback to a
	 * private vzalloc preserves exact old behavior when the arena cannot
	 * fit a request; the close summary reports whether it ever fired.
	 */
	void *arena_base;
	struct list_head arena_free;
	u32 arena_high_water;
	u32 arena_fallbacks;
	/* File-level arena facade for cover-page plan bindings. page_pa holds
	 * the translated GPU PA of every arena page (filled at DMA-register
	 * time, idempotent); the BO itself carries no allocation.
	 */
	struct mt_bo arena_bo;
	bool arena_bo_ready;
	u64 *arena_gpu_pages;
	char sync_timeline[32];
	u32 init_module;
};

static struct drm_device *pvr_drm;
static bool pvr_ready;

/* Declared here because pvr_file_release() below drops the PMRs' list
 * references, which are handed back through this.
 */
static void pvr_pmr_unref(struct mt_pvr_pmr *pmr);
static void pvr_pmr_dma_release(struct mt_pvr_pmr *pmr);
static int pvr_pmr_put(struct mt_pvr_file *file, u64 handle);
static struct mt_pvr_object *pvr_reservation_find(struct mt_pvr_file *file,
						  u64 handle);

#define MT_PVR_VM_TABLE_PAGES 32U
#define MT_PVR_VM_TABLE_BYTES (MT_PVR_VM_TABLE_PAGES * PAGE_SIZE)
#define MT_PVR_VM_ROOT_PA ((1ULL << MT_GPU_VA_BITS) - MT_PVR_VM_TABLE_BYTES)

/* This store is a CPU-side planning domain only. No allocator or MMIO
 * callback can be reached through these BOs; free is intentionally a no-op.
 */
static void pvr_gpu_plan_bo_free(void *store,
				 const struct mt_bo_backing *backing)
{
	(void)store;
	(void)backing;
}

static const struct mt_bo_ops pvr_gpu_plan_bo_ops = {
	.free = pvr_gpu_plan_bo_free,
};

/* Translated GPU PA per arena page, backing the file-level arena facade
 * BO. Filled at DMA-register time (translation is idempotent); read by
 * cover-page plan bindings. Allocated on demand like the VM itself.
 */
static int pvr_arena_pages_ensure(struct mt_pvr_file *file)
{
	if (file->arena_gpu_pages)
		return 0;
	file->arena_gpu_pages = kcalloc(MT_PVR_ARENA_PAGES,
					sizeof(*file->arena_gpu_pages),
					GFP_KERNEL);
	return file->arena_gpu_pages ? 0 : -ENOMEM;
}

static int pvr_gpu_vm_ensure(struct mt_pvr_file *file)
{
	void *storage;
	int ret;

	if (file->gpu_vm_ready)
		return 0;
	storage = kvzalloc(2 * (size_t)MT_PVR_VM_TABLE_BYTES, GFP_KERNEL);
	if (!storage)
		return -ENOMEM;
	file->gpu_vm_storage = storage;
	file->arena_gpu_pages = kcalloc(MT_PVR_ARENA_PAGES,
					sizeof(*file->arena_gpu_pages),
					GFP_KERNEL);
	if (!file->arena_gpu_pages) {
		kvfree(file->gpu_vm_storage);
		file->gpu_vm_storage = NULL;
		return -ENOMEM;
	}
	file->gpu_tables = (struct mt_bo){
		.backing = {.gpu_pa = MT_PVR_VM_ROOT_PA,
			.bytes = MT_PVR_VM_TABLE_BYTES},
		.ops = &pvr_gpu_plan_bo_ops,
		.store = file,
		.requested_bytes = MT_PVR_VM_TABLE_BYTES,
		.refs = 1,
	};
	ret = mt_gpu_vm_init(&file->gpu_vm, &file->gpu_tables, storage,
		(u8 *)storage + MT_PVR_VM_TABLE_BYTES, MT_PVR_VM_TABLE_BYTES);
	if (ret) {
		if (file->gpu_vm.tables)
			mt_bo_put(file->gpu_vm.tables);
		if (file->gpu_tables.refs)
			mt_bo_put(&file->gpu_tables);
		kfree(file->arena_gpu_pages);
		file->arena_gpu_pages = NULL;
		kvfree(file->gpu_vm_storage);
		file->gpu_vm_storage = NULL;
		memset(&file->gpu_vm, 0, sizeof(file->gpu_vm));
		return ret;
	}
	ret = mt_bo_put(&file->gpu_tables); /* leave only the VM's table reference */
	if (ret) {
		WARN_ON(mt_gpu_vm_fini(&file->gpu_vm));
		kfree(file->arena_gpu_pages);
		file->arena_gpu_pages = NULL;
		kvfree(file->gpu_vm_storage);
		file->gpu_vm_storage = NULL;
		return ret;
	}
	file->arena_bo = (struct mt_bo){
		.backing = {.gpu_pa = 0, .bytes = MT_PVR_ARENA_BYTES},
		.ops = &pvr_gpu_plan_bo_ops,
		.store = file,
		.requested_bytes = MT_PVR_ARENA_BYTES,
		.refs = 1,
		.page_pa = NULL,
	};
	ret = pvr_arena_pages_ensure(file);
	if (ret) {
		WARN_ON(mt_bo_put(&file->arena_bo));
		WARN_ON(mt_gpu_vm_fini(&file->gpu_vm));
		kvfree(file->gpu_vm_storage);
		file->gpu_vm_storage = NULL;
		return ret;
	}
	file->arena_bo.page_pa = file->arena_gpu_pages;
	file->arena_bo_ready = true;
	file->gpu_vm_ready = true;
	return 0;
}

static int pvr_gpu_bo_init(struct mt_pvr_file *file, struct mt_pvr_pmr *pmr)
{
	u64 bytes = (u64)pmr->dma_npages * PAGE_SIZE;

	if (pmr->gpu_bo_ready)
		return 0;
	if (!pmr->gpu_pages || !pmr->dma_npages || bytes > U32_MAX ||
	    bytes < pmr->bytes)
		return -ERANGE;
	pmr->gpu_bo = (struct mt_bo){
		.backing = {.handle = pmr, .gpu_pa = pmr->gpu_pages[0],
			.bytes = bytes},
		.ops = &pvr_gpu_plan_bo_ops,
		.store = file,
		.requested_bytes = pmr->bytes,
		.refs = 1,
		.page_pa = pmr->gpu_pages,
	};
	pmr->gpu_bo_ready = true;
	return 0;
}

/* Build an unpublished VM image for a PVR mapping.
 *
 * Arena-backed PMRs bind their per-page cover set against the file-level
 * arena facade: each cover page maps the arena page holding that page's
 * first PMR-valid byte (exact 1:1 for aligned ranges; the documented
 * first-byte approximation for unaligned prefixes/tails). A cover page
 * already live under another range refuses the whole bind (-EEXIST) rather
 * than aliasing two owners onto one PTE. Fallback (private vzalloc) PMRs
 * keep the aligned-only single-shape bind, expressed the same per-page way
 * against their own facade; unaligned fallbacks degrade with -EOPNOTSUPP.
 * The UMD wire result never changes either way.
 */
static int pvr_gpu_vm_bind(struct mt_pvr_file *file, struct mt_pvr_pmr *pmr,
			   struct mt_pvr_binding *binding)
{
	struct mt_pvr_object *reservation;
	struct mt_vm_binding *cover = NULL;
	struct mt_bo *bo;
	u64 first, last, pg;
	u32 npages, j;
	int ret;

	if (!pmr->dma_addrs || !pmr->gpu_pages || !pmr->bytes ||
	    pmr->bytes > U32_MAX)
		return -EOPNOTSUPP;
	reservation = pvr_reservation_find(file, binding->reservation);
	if (!reservation)
		return -ENOENT;
	if (pmr->bytes > reservation->arg1)
		return -ENOSPC;
	ret = pvr_gpu_vm_ensure(file);
	if (ret)
		return ret;
	if (!pmr->arena_pages) {
		if (!IS_ALIGNED(binding->va, PAGE_SIZE) ||
		    !IS_ALIGNED(pmr->bytes, PAGE_SIZE))
			return -EOPNOTSUPP;
		ret = pvr_gpu_bo_init(file, pmr);
		if (ret)
			return ret;
		bo = &pmr->gpu_bo;
	} else {
		bo = &file->arena_bo;
	}
	first = binding->va & ~4095ULL;
	/* bytes >= 1 and va+bytes <= 2^40 (reservation guarantee), so the
	 * subtraction cannot underflow and the span fits u32 pages.
	 */
	last = (binding->va + pmr->bytes - 1) & ~4095ULL;
	npages = (u32)((last - first) >> PAGE_SHIFT) + 1;
	if (npages > file->gpu_vm.max_ranges)
		return -ENOSPC;
	cover = kcalloc(npages, sizeof(*cover), GFP_KERNEL);
	if (!cover)
		return -ENOMEM;
	for (j = 0, pg = first; pg <= last; j++, pg += PAGE_SIZE) {
		u32 off;

		if (pmr->arena_pages) {
			u64 valid = pg > binding->va ? pg : binding->va;
			off = (pmr->arena_offset +
			       (u32)((valid - binding->va) >> PAGE_SHIFT)) << PAGE_SHIFT;
		} else {
			off = j << PAGE_SHIFT;
		}
		cover[j] = (struct mt_vm_binding){
			.bo = bo, .va = pg,
			.offset = off,
			.bytes = PAGE_SIZE, .flags = MT_GPU_MAP_DEFAULT,
		};
	}
	ret = mt_gpu_vm_bind_many(&file->gpu_vm, cover, npages);
	kfree(cover);
	binding->gpu_result = ret;
	if (ret)
		return ret;
	binding->gpu_first = first;
	binding->gpu_npages = npages;
	binding->gpu_bytes = pmr->bytes;
	binding->gpu_bound = true;
	pr_info("mt_pvr_bridge: CPU-only PVR VM plan root=%#llx va=%#llx bytes=%llu pages=%u pa=%#llx\n",
		(unsigned long long)file->gpu_tables.backing.gpu_pa,
		(unsigned long long)binding->va,
		(unsigned long long)pmr->bytes, npages,
		(unsigned long long)pmr->gpu_pages[0]);
	return 0;
}

static int pvr_gpu_vm_unbind(struct mt_pvr_file *file,
			     struct mt_pvr_binding *binding)
{
	u32 j;
	int ret;

	if (!binding->gpu_bound)
		return 0;
	if (!file->gpu_vm_ready)
		return -EUCLEAN;
	for (j = 0; j < binding->gpu_npages; j++) {
		ret = mt_gpu_vm_unbind(&file->gpu_vm,
				       binding->gpu_first + ((u64)j << PAGE_SHIFT),
				       PAGE_SIZE);
		if (ret)
			return ret;
	}
	binding->gpu_bound = false;
	return 0;
}

static int pvr_gpu_vm_destroy(struct mt_pvr_file *file)
{
	int ret;

	if (!file->gpu_vm_ready)
		return 0;
	ret = mt_gpu_vm_fini(&file->gpu_vm);
	if (ret)
		return ret;
	if (file->arena_bo_ready) {
		WARN_ON(mt_bo_put(&file->arena_bo));
		file->arena_bo_ready = false;
	}
	kfree(file->arena_gpu_pages);
	file->arena_gpu_pages = NULL;
	kvfree(file->gpu_vm_storage);
	file->gpu_vm_storage = NULL;
	file->gpu_vm_ready = false;
	return 0;
}

/* Session acquisition for DMA (S4-3 handoff, step 1).
 *
 * Returns the pinned mt_guest on a live trial, or NULL (degrade, never
 * error). Uses only primitives proven on this kernel: PCI lookup,
 * driver-name check, drvdata, try_module_get. Deliberately no symbol_get:
 * cross-module symbol resolution does not work here (empirically verified,
 * even for printk), so the design must not depend on it.
 *
 * Each successful acquire stores its owner ref in pmr->dma_owner, balanced
 * by exactly one module_put() in pvr_pmr_dma_release(). The module ref pins
 * against unload, NOT unbind -- see mt_pvr_session.h rule 2 for the
 * operational constraint this implies.
 */
static struct mt_guest *pvr_session_acquire(struct module **owner_out)
{
	struct pci_dev *pdev =
		pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(14, 0));
	struct module *owner = NULL;
	struct mt_guest *g = NULL;

	if (!pdev)
		return NULL;
	device_lock(&pdev->dev);
	if (!pdev->driver || strcmp(pdev->driver->name, "mt_guest_probe"))
		goto out;
	owner = pdev->driver->driver.owner;
	if (!owner || !try_module_get(owner)) {
		owner = NULL;
		goto out;
	}
	g = pci_get_drvdata(pdev);
	if (!g)
		goto out;
	mutex_lock(&g->trial_lock);
	if (!g->trial.pinned || !g->trial.connected) {
		mutex_unlock(&g->trial_lock);
		module_put(owner);
		owner = NULL;
		g = NULL;
		goto out;
	}
	mutex_unlock(&g->trial_lock);
out:
	if (!g && owner) {
		module_put(owner);
		owner = NULL;
	}
	if (owner_out)
		*owner_out = owner;
	device_unlock(&pdev->dev);
	pci_dev_put(pdev);
	return g;
}

/* Attempt DMA registration of a PMR through the live GPU session.
 *
 * S4-3 handoff, step 1 (bridge side). Returns 0 with dma_addrs filled, or a
 * negative errno with nothing changed. Callers treat ANY failure -- above
 * all -ENODEV (no session, dead session) -- as "stay on system memory",
 * never as a UMD-visible error. See mt_pvr_session.h rules.
 *
 * Locking: runs under file->lock like the rest of dispatch. The try_module
 * ref pins the session module against unload; unbind races stay governed by
 * the operational rule (bridge rmmod first), since the PCI core gives
 * remove() no veto.
 */
static int pvr_pmr_dma_register(struct mt_pvr_file *file,
				struct mt_pvr_pmr *pmr)
{
	struct mt_guest *g;
	struct mt_guest_device *d;
	struct pci_dev *pdev;
	struct page **pages;
	struct mt_system_address address;
	u32 i, npages;
	int ret;

	(void)file;
	(void)mt_fw_event_io_ops;
	if (pmr->dma_addrs)
		return 0;
	npages = mt_pvr_mmap_page_count(pmr->bytes, PAGE_SIZE);
	if (!npages)
		return -EINVAL;
	g = pvr_session_acquire(&pmr->dma_owner);
	if (!g)
		return -ENODEV;
	pdev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(14, 0));
	if (!pdev) {
		ret = -ENODEV;
		goto put_session;
	}
	device_lock(&pdev->dev);
	if (!pdev->driver || strcmp(pdev->driver->name, "mt_guest_probe") ||
	    pci_get_drvdata(pdev) != g) {
		ret = -ENODEV;
		goto unlock;
	}
	/* Serialize against trial_control while checking liveness and creating
	 * mappings. device_lock is always taken outermost (no path in this
	 * tree takes device_lock while holding trial_lock), so nesting
	 * trial_lock inside device_lock cannot deadlock against teardown.
	 */
	mutex_lock(&g->trial_lock);
	if (!g->trial.pinned || !g->trial.connected) {
		ret = -ENODEV;
		goto unlock_trial;
	}
	d = container_of(g, struct mt_guest_device, state);
	ret = mt_system_address_init(&address, (void *)g->info, PAGE_SIZE,
		d->runtime.windows, MT_GUEST_WINDOWS_BYTES,
		pci_resource_start(pdev, 4), pci_resource_len(pdev, 4));
	if (ret)
		goto unlock_trial;
	pages = kcalloc(npages, sizeof(*pages), GFP_KERNEL);
	if (!pages) {
		ret = -ENOMEM;
		goto unlock_trial;
	}
	for (i = 0; i < npages; i++) {
		pages[i] = vmalloc_to_page(pmr->host + i * PAGE_SIZE);
		if (!pages[i]) {
			ret = -ENOMEM;
			goto free_pages;
		}
	}
	pmr->dma_addrs = kcalloc(npages, sizeof(*pmr->dma_addrs), GFP_KERNEL);
	pmr->gpu_pages = kcalloc(npages, sizeof(*pmr->gpu_pages), GFP_KERNEL);
	if (!pmr->dma_addrs || !pmr->gpu_pages) {
		kfree(pmr->dma_addrs);
		kfree(pmr->gpu_pages);
		pmr->dma_addrs = NULL;
		pmr->gpu_pages = NULL;
		ret = -ENOMEM;
		goto free_pages;
	}
	ret = 0;
	ret = pvr_arena_pages_ensure(file);
	if (ret) {
		kfree(pmr->dma_addrs);
		kfree(pmr->gpu_pages);
		pmr->dma_addrs = NULL;
		pmr->gpu_pages = NULL;
		goto free_pages;
	}
	for (i = 0; i < npages; i++) {
		u64 gpu_pa;
		dma_addr_t addr;

		ret = mt_system_page_address(&address, page_to_phys(pages[i]),
					     &gpu_pa);
		if (ret)
			break;
		addr = dma_map_page(&pdev->dev, pages[i], 0, PAGE_SIZE,
				    DMA_BIDIRECTIONAL);

		if (dma_mapping_error(&pdev->dev, addr)) {
			ret = -EIO;
			break;
		}
		pmr->dma_addrs[i].dma_addr = addr;
		pmr->dma_addrs[i].gpu_pa = gpu_pa;
		pmr->gpu_pages[i] = gpu_pa;
		/* Feed the file-level arena facade. Translation is a pure
		 * function of the physical page, so rewriting an entry that
		 * a previous PMR already filled stores the same value.
		 * Guarded: fallback (private vzalloc) PMRs own no arena slot.
		 */
		if (pmr->arena_pages)
			file->arena_gpu_pages[pmr->arena_offset + i] = gpu_pa;
	}
	if (ret) {
		while (i--)
			dma_unmap_page(&pdev->dev, pmr->dma_addrs[i].dma_addr,
				       PAGE_SIZE, DMA_BIDIRECTIONAL);
		kfree(pmr->dma_addrs);
		kfree(pmr->gpu_pages);
		pmr->dma_addrs = NULL;
		pmr->gpu_pages = NULL;
		goto free_pages;
	}
	pmr->dma_npages = npages;
	pmr->dma_pdev = pdev;
	pr_info_once("mt_pvr_bridge: DMA domains: dma_iova=%#llx gpu_pa=%#llx pages=%u\n",
		(unsigned long long)pmr->dma_addrs[0].dma_addr,
		(unsigned long long)pmr->dma_addrs[0].gpu_pa, npages);
	ret = 0;
free_pages:
	kfree(pages);
unlock_trial:
	mutex_unlock(&g->trial_lock);
unlock:
	device_unlock(&pdev->dev);
	if (!ret)
		pdev = NULL; /* PMR owns the pci_dev reference until DMA release. */
put_session:
	if (pdev)
		pci_dev_put(pdev);
	if (ret && pmr->dma_owner) {
		module_put(pmr->dma_owner);
		pmr->dma_owner = NULL;
	}
	return ret;
}

/* Release a DMA registration. Safe on a never-registered PMR. Unmap against
 * the exact pci_dev reference retained at registration, not a new lookup
 * whose binding may have changed. This pins the device object, not its active
 * driver; the operational teardown order in mt_pvr_session.h still applies.
 */
static void pvr_pmr_dma_release(struct mt_pvr_pmr *pmr)
{
	struct pci_dev *pdev = pmr->dma_pdev;
	u32 i;

	/* Use the exact device object used for mapping, not a fresh lookup whose
	 * driver may have changed since registration. The PMR owns this reference.
	 */
	if (pmr->dma_addrs && pdev) {
		device_lock(&pdev->dev);
		for (i = 0; i < pmr->dma_npages; i++)
			dma_unmap_page(&pdev->dev, pmr->dma_addrs[i].dma_addr,
				       PAGE_SIZE, DMA_BIDIRECTIONAL);
		device_unlock(&pdev->dev);
		pci_dev_put(pdev);
	}
	pmr->dma_pdev = NULL;
	if (pmr->dma_owner) {
		module_put(pmr->dma_owner);
		pmr->dma_owner = NULL;
	}
	kfree(pmr->dma_addrs);
	pmr->dma_addrs = NULL;
	if (pmr->gpu_bo_ready) {
		WARN_ON(pmr->gpu_bo.refs != 1);
		WARN_ON(mt_bo_put(&pmr->gpu_bo));
		pmr->gpu_bo_ready = false;
	}
	kfree(pmr->gpu_pages);
	pmr->gpu_pages = NULL;
	pmr->dma_npages = 0;
}

static void pvr_file_release(struct kref *kref)
{
	struct mt_pvr_file *file = container_of(kref, struct mt_pvr_file, ref);
	struct mt_pvr_pmr *pmr, *tmp;
	struct mt_pvr_object *obj, *otmp;
	struct mt_pvr_binding *binding, *btmp;
	int ret;

	/* Drop the list's reference rather than freeing outright, so the
	 * refcount path stays uniform. Nothing can be holding another reference
	 * here: an in-flight mmap pins the file through filp, so this callback
	 * cannot run while one exists.
	 */
	list_for_each_entry_safe(binding, btmp, &file->bindings, link) {
		ret = pvr_gpu_vm_unbind(file, binding);
		if (WARN_ON(ret))
			return;
	}
	ret = pvr_gpu_vm_destroy(file);
	if (WARN_ON(ret))
		return;
	list_for_each_entry_safe(pmr, tmp, &file->pmrs, link) {
		list_del(&pmr->link);
		pvr_pmr_unref(pmr);
	}
	/* Every PMR slot is back by now. Drain the free list, then report
	 * whether any PMR ever bypassed the arena before freeing it.
	 */
	{
		struct mt_pvr_arena_seg *seg, *stmp;

		list_for_each_entry_safe(seg, stmp, &file->arena_free, link) {
			list_del(&seg->link);
			kfree(seg);
		}
	}
	if (file->arena_base)
		pr_info("mt_pvr_bridge: arena close: high_water=%u/%u pages fallbacks=%u\n",
			file->arena_high_water, MT_PVR_ARENA_PAGES,
			file->arena_fallbacks);
	vfree(file->arena_base);
	file->arena_base = NULL;
	list_for_each_entry_safe(obj, otmp, &file->objects, link) {
		list_del(&obj->link);
		kfree(obj);
	}
	{
		struct mt_pvr_binding *b, *btmp;

		list_for_each_entry_safe(b, btmp, &file->bindings, link) {
			list_del(&b->link);
			kfree(b);
		}
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
	struct mt_pvr_file *file;

	if (!READ_ONCE(pvr_ready)) {
		pr_info("mt_pvr_bridge: open refused, not ready\n");
		return -ENODEV;
	}
	file = kzalloc(sizeof(*file), GFP_KERNEL);
	if (!file) {
		pr_info("mt_pvr_bridge: open: allocation failed\n");
		return -ENOMEM;
	}
	kref_init(&file->ref);
	mutex_init(&file->lock);
	INIT_LIST_HEAD(&file->pmrs);
	INIT_LIST_HEAD(&file->objects);
	INIT_LIST_HEAD(&file->bindings);
	INIT_LIST_HEAD(&file->arena_free);
	mt_pvr_handles_init(&file->handles);
	mt_pvr_queue_init(&file->queue, MT_PVR_RING_ENTRIES);
	mt_pvr_rgx_app_heaps_init(&file->heaps);
	/* GetFeatures(conn) returns conn+0xa0+0x620, so the block the UMD reads
	 * starts 0x620 bytes into the allocation.
	 */
	file->features = kzalloc(MT_PVR_FEATURE_SKEW + sizeof(*file->features),
				 GFP_KERNEL);
	file->info_page = vzalloc(MT_PVR_INFO_BYTES);
	file->conn = kzalloc(sizeof(*file->conn), GFP_KERNEL);
	if (!file->features || !file->info_page || !file->conn) {
		kref_put(&file->ref, pvr_file_release);
		pr_info("mt_pvr_bridge: open: per-file buffers failed\n");
		return -ENOMEM;
	}
	mt_pvr_info_page_init(file->info_page, MT_PVR_INFO_BYTES);
	mt_pvr_features_init((struct mt_pvr_features *)
			     ((char *)file->features + MT_PVR_FEATURE_SKEW), 1);
	mt_pvr_features_set_ddk((struct mt_pvr_features *)
				((char *)file->features + MT_PVR_FEATURE_SKEW),
				ddk_feature_set);
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

/* File-arena allocator: first fit with neighbor coalescing, in pages.
 * Every mutation happens under file->lock (all dispatch paths) except
 * pvr_file_release, which runs single-threaded on the last kref after every
 * PMR is already freed -- so the free list needs no lock of its own.
 */
static int pvr_arena_ensure(struct mt_pvr_file *file)
{
	struct mt_pvr_arena_seg *seg;

	if (file->arena_base)
		return 0;
	file->arena_base = vzalloc(MT_PVR_ARENA_BYTES);
	if (!file->arena_base)
		return -ENOMEM;
	seg = kzalloc(sizeof(*seg), GFP_KERNEL);
	if (!seg) {
		vfree(file->arena_base);
		file->arena_base = NULL;
		return -ENOMEM;
	}
	seg->offset = 0;
	seg->pages = MT_PVR_ARENA_PAGES;
	list_add(&seg->link, &file->arena_free);
	return 0;
}

/* First fit, splitting the chosen run. The segment kzalloc happens before
 * the list is touched, so -ENOMEM leaves the free list unchanged.
 */
static int pvr_arena_alloc(struct mt_pvr_file *file, u32 npages,
			   u32 *offset_out)
{
	struct mt_pvr_arena_seg *seg, *rest;
	u32 end;

	if (!npages || npages > MT_PVR_ARENA_PAGES)
		return -ENOSPC;
	list_for_each_entry(seg, &file->arena_free, link) {
		if (seg->pages < npages)
			continue;
		if (seg->pages == npages) {
			list_del(&seg->link);
			*offset_out = seg->offset;
			kfree(seg);
		} else {
			rest = kzalloc(sizeof(*rest), GFP_KERNEL);
			if (!rest)
				return -ENOMEM;
			*offset_out = seg->offset;
			rest->offset = seg->offset + npages;
			rest->pages = seg->pages - npages;
			list_replace(&seg->link, &rest->link);
			kfree(seg);
		}
		end = *offset_out + npages;
		if (end > file->arena_high_water)
			file->arena_high_water = end;
		return 0;
	}
	return -ENOSPC;
}

/* Return a run, merging with neighbors. Overlapping or out-of-range returns
 * can only come from a caller bug; leak the run rather than corrupt the
 * list -- the arena dies with the file anyway.
 */
static void pvr_arena_free(struct mt_pvr_file *file, u32 offset, u32 pages)
{
	struct mt_pvr_arena_seg *seg, *prev = NULL, *next = NULL;
	struct mt_pvr_arena_seg *new;

	if (!pages || pages > MT_PVR_ARENA_PAGES ||
	    offset > MT_PVR_ARENA_PAGES - pages)
		return;
	list_for_each_entry(seg, &file->arena_free, link) {
		if (seg->offset < offset + pages &&
		    offset < seg->offset + seg->pages)
			return;
		if (seg->offset + seg->pages == offset)
			prev = seg;
		else if (seg->offset == offset + pages)
			next = seg;
	}
	if (prev && next) {
		prev->pages += pages + next->pages;
		list_del(&next->link);
		kfree(next);
		return;
	}
	if (prev) {
		prev->pages += pages;
		return;
	}
	if (next) {
		next->offset = offset;
		next->pages += pages;
		return;
	}
	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return;
	new->offset = offset;
	new->pages = pages;
	list_for_each_entry(seg, &file->arena_free, link) {
		if (seg->offset > offset) {
			list_add_tail(&new->link, &seg->link);
			return;
		}
	}
	list_add_tail(&new->link, &file->arena_free);
}

static struct mt_pvr_pmr *pvr_pmr_new(struct mt_pvr_file *file, u64 bytes,
				     u32 log2_page_size)
{
	struct mt_pvr_pmr *pmr;
	u64 need = bytes ? bytes : 1;
	unsigned long want = mt_pvr_mmap_page_count((unsigned long)need,
						    PAGE_SIZE);
	u32 offset = 0;

	pmr = kzalloc(sizeof(*pmr), GFP_KERNEL);
	if (!pmr)
		return NULL;
	/* Prefer the file arena so VA-neighbor PMRs can share physical pages
	 * once the plan binds per-page cover sets. Zero the slot: reused runs
	 * still hold the previous owner's bytes, unlike fresh vzalloc.
	 * Fall back to a private vzalloc with identical semantics when the
	 * request cannot fit the arena or it is missing/full; the close
	 * summary reports whether that ever fired. The want check runs on the
	 * full-precision count so a giant request can never truncate into a
	 * small arena slot.
	 */
	if (want && want <= MT_PVR_ARENA_PAGES &&
	    !pvr_arena_ensure(file) &&
	    !pvr_arena_alloc(file, (u32)want, &offset)) {
		pmr->host = (u8 *)file->arena_base + ((u64)offset << PAGE_SHIFT);
		pmr->arena_offset = offset;
		pmr->arena_pages = (u32)want;
		memset(pmr->host, 0, want << PAGE_SHIFT);
	} else {
		file->arena_fallbacks++;
		pmr->host = vzalloc(bytes ? bytes : 1);
		if (!pmr->host) {
			kfree(pmr);
			return NULL;
		}
	}
	if (mt_pvr_handles_alloc(&file->handles, &pmr->handle)) {
		if (pmr->arena_pages)
			pvr_arena_free(file, pmr->arena_offset, pmr->arena_pages);
		else
			vfree(pmr->host);
		kfree(pmr);
		return NULL;
	}
	pmr->bytes = bytes;
	pmr->file = file;
	pmr->log2_page_size = log2_page_size;
	pmr->refcount = 1;	/* held by the list itself */
	list_add_tail(&pmr->link, &file->pmrs);
	return pmr;
}

/* Drop one reference and free when the last one goes.
 *
 * Every caller holds file->lock (all dispatch paths) except pvr_file_release,
 * which runs single-threaded on the last kref after every PMR is already
 * freed -- so arena segment reclaim inside the free path is always safe.
 * The caller must own a reference. pvr_pmr_put() below is the list-owner side
 * and must be called *with* file->lock held.
 */
static void pvr_pmr_unref(struct mt_pvr_pmr *pmr)
{
	if (!pmr)
		return;
	WARN_ON_ONCE(pmr->refcount == 0);
	if (--pmr->refcount)
		return;
	pvr_pmr_dma_release(pmr);
	if (pmr->arena_pages)
		pvr_arena_free(pmr->file, pmr->arena_offset, pmr->arena_pages);
	else
		vfree(pmr->host);
	kfree(pmr);
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
	/* This is an IMG_HANDLE, i.e. the handle itself, NOT the mmap offset.
	 *
	 * PVRSRV_BRIDGE_OUT_ACQUIREINFOPAGE in the vendor's
	 * generated/common_srvcore_bridge.h declares `IMG_HANDLE hPMR`, and
	 * the UMD applies the "<< 12" shift itself when it mmaps. Returning
	 * the pre-shifted offset here looked plausible -- bA15 had measured 28
	 * of 28 mmaps landing on handle << 12 -- but it breaks the *next*
	 * command: the UMD feeds the returned value straight back in as
	 * PmrLocalImportPmr's hExtImportHandle, so a shifted handle can never
	 * match a PMR and the import fails with -ENOENT.
	 */
	out.handle = pmr->handle;
	return pvr_out(cmd, &out, sizeof(out));
}

static int pvr_cmd_heap_count(struct mt_pvr_file *file, struct mt_pvr_cmd *cmd)
{
	/* eError first, then the count -- see mt_pvr_heap_count_out. Writing the
	 * count at offset 0 made the UMD read it as an error code and then
	 * cache "no heaps" at device-connect time, so every later heap lookup
	 * failed and RGXCreateDeviceMemContext gave up before allocating.
	 */
	struct mt_pvr_heap_count_out out = { 0 };

	out.num_heaps = mt_pvr_heaps_count(&file->heaps);
	return pvr_out(cmd, &out, sizeof(out));
}

/* 0x6:0x7 MM:PmrUnrefPmr -- hand a PMR back.
 *
 * This was sharing a case with DevmemIntHeapDestroy, which searches for
 * MT_PVR_KIND_HEAP objects only. A PMR is not a heap, so the lookup always
 * missed and the driver answered -ENOENT to a perfectly valid unref.
 */
static int pvr_cmd_pmr_unref(struct mt_pvr_file *file,
			     struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_hwperf_release_in in;
	struct mt_pvr_hwperf_release_out out = { 0 };
	int ret;

	/* Same shape as MUSAReleaseHWPerfSettings: a single widened handle. */
	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	ret = pvr_pmr_put(file, in.pmr);
	if (ret)
		return ret;
	return pvr_out(cmd, &out, sizeof(out));
}

/* 0x6:0x10 MM:DevmemIntCtxDestroy -- release the device-memory context.
 *
 * This was also sharing a case with DevmemIntHeapDestroy, so it looked for
 * MT_PVR_KIND_HEAP and returned -ENOENT for the MT_PVR_KIND_CONTEXT that
 * DevmemIntCtxCreate had just published. The UMD reads that -ENOENT as a
 * failed teardown and walks its own cleanup path twice.
 *
 * Like pvr_cmd_heap_destroy(), this runs inside pvr_bridge_dispatch() and must
 * not take file->lock again.
 */
static int pvr_cmd_ctx_destroy(struct mt_pvr_file *file,
			       struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_heap_destroy_in in;
	struct mt_pvr_heap_destroy_out out = { 0 };
	struct mt_pvr_object *obj;
	int ret;

	/* Both handles are a single widened MT_HANDLE on the wire. */
	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	list_for_each_entry(obj, &file->objects, link) {
		if (obj->handle == in.devmem_heap &&
		    obj->kind == MT_PVR_KIND_CONTEXT) {
			list_del(&obj->link);
			kfree(obj);
			return pvr_out(cmd, &out, sizeof(out));
		}
	}
	return -ENOENT;
}

/* 0x6:0x12 MM:DevmemIntHeapDestroy -- release the object Create handed out.
 *
 * This was an empty stub. The UMD therefore never saw its heap go away and
 * freed it again itself, which is where "double free or corruption (fasttop)"
 * came from. Freeing it here, exactly once, is also just correct: a handle the
 * driver issued must be retirable by the handle the driver was given.
 *
 * No locking here: pvr_ioctl_bridge() already holds file->lock across the whole
 * dispatch, and it is a plain mutex, so taking it again self-deadlocks. An
 * earlier version of this function did exactly that and hung the UMD in
 * uninterruptible sleep inside pvr_bridge_dispatch, where it could not even be
 * killed.
 */
static int pvr_cmd_heap_destroy(struct mt_pvr_file *file,
				struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_heap_destroy_in in;
	struct mt_pvr_heap_destroy_out out = { 0 };
	struct mt_pvr_object *obj;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	list_for_each_entry(obj, &file->objects, link) {
		if (obj->handle == in.devmem_heap && obj->kind == MT_PVR_KIND_HEAP) {
			list_del(&obj->link);
			kfree(obj);
			return pvr_out(cmd, &out, sizeof(out));
		}
	}
	/* Refuse a handle we never issued, or one already destroyed, rather than
	 * silently succeeding.
	 */
	return -ENOENT;
}

/* Completion fence: always ready. See pvr_cmd_kicksync_submit().
 * An always-ready pollfd is a userspace busy-loop hazard if anyone polls it
 * with timeout zero (a desktop GPU thread spun ~1300s and died during this
 * bridge's residency; unattributed, no kernel-side fault found). Returning
 * ready is still required: the S4-1 UMD waits on this fence and blocking it
 * would hang the ladder. Revisit only with a real completion source.
 */
static __poll_t pvr_fence_poll(struct file *file, struct poll_table_struct *pt)
{
	return EPOLLIN | EPOLLOUT;
}

static const struct file_operations pvr_fence_fops = {
	.poll = pvr_fence_poll,
	.llseek = noop_llseek,
};

/* 0x88:0x2 RGXKickSync2, 0x88:0x3 RGXSetKickSyncContextProperty and
 * 0x88:0x4 RGXKickSync3 (TA submit).
 *
 * Accept-and-inspect: validate the wire sizes and the context handle, then
 * complete immediately with a signalled eventfd. This lets the UMD run its
 * full submit-then-wait state machine. It is NOT GPU execution -- there is no
 * firmware channel, no page tables and no doorbell behind this bridge, so
 * nothing here can or does touch hardware.
 *
 * No locking: pvr_bridge_dispatch() already holds file->lock, and taking it
 * again self-deadlocks (bA26). The 0x88:0x4 IN layout is mt_pvr_kicksync3_in
 * (5.2 map, r53); only the handle was read until the kick inventory below.
 */
/* Inspect-only kick inventory (translator steps T1+T2, r62/r63).
 *
 * Copies the UMD-side check/update offset/value/UFO arrays and resolves
 * each UFO handle against this file's PMRs and objects, then logs one
 * inventory line. PURELY observational: dispatch runs in the calling
 * process's context so copy_from_user can reach these pointers (as a real
 * server does), but every failure -- absurd counts, unreadable memory,
 * unknown handles -- degrades to plain accept. The fence + OUT path below
 * is untouched, so the wire result is identical on all paths.
 */
#define MT_PVR_KICK_SYNC_MAX 64U

static int pvr_kick_ufo_known(struct mt_pvr_file *file, u64 handle)
{
	struct mt_pvr_pmr *pmr;
	struct mt_pvr_object *obj;

	list_for_each_entry(pmr, &file->pmrs, link)
		if (pmr->handle == handle)
			return 1;
	list_for_each_entry(obj, &file->objects, link)
		if (obj->handle == handle)
			return 1;
	return 0;
}

static void pvr_kick_inspect(struct mt_pvr_file *file,
			     const struct mt_pvr_kicksync3_in *in)
{
	u32 ncheck = in->client_check_count, nupdate = in->client_update_count;
	u32 *check_off = NULL, *check_val = NULL;
	u32 *update_off = NULL, *update_val = NULL;
	u64 *check_ufo = NULL, *update_ufo = NULL;
	u32 i, known = 0, total = 0;

	if (!ncheck && !nupdate)
		return;
	if (ncheck > MT_PVR_KICK_SYNC_MAX ||
	    nupdate > MT_PVR_KICK_SYNC_MAX) {
		pr_info("mt_pvr_bridge: kick sync counts outside inspect cap: check=%u update=%u\n",
			ncheck, nupdate);
		return;
	}
	check_off = kcalloc(ncheck ? ncheck : 1, sizeof(*check_off),
			    GFP_KERNEL);
	check_val = kcalloc(ncheck ? ncheck : 1, sizeof(*check_val),
			    GFP_KERNEL);
	check_ufo = kcalloc(ncheck ? ncheck : 1, sizeof(*check_ufo),
			    GFP_KERNEL);
	update_off = kcalloc(nupdate ? nupdate : 1, sizeof(*update_off),
			     GFP_KERNEL);
	update_val = kcalloc(nupdate ? nupdate : 1, sizeof(*update_val),
			     GFP_KERNEL);
	update_ufo = kcalloc(nupdate ? nupdate : 1, sizeof(*update_ufo),
			     GFP_KERNEL);
	if ((ncheck && (!check_off || !check_val || !check_ufo)) ||
	    (nupdate && (!update_off || !update_val || !update_ufo)))
		goto out;
	if ((ncheck &&
	     copy_from_user(check_off, u64_to_user_ptr(in->check_devvar_offset),
			    (size_t)ncheck * sizeof(*check_off))) ||
	    (ncheck &&
	     copy_from_user(check_val, u64_to_user_ptr(in->check_value),
			    (size_t)ncheck * sizeof(*check_val))) ||
	    (ncheck &&
	     copy_from_user(check_ufo, u64_to_user_ptr(in->check_ufo_block),
			    (size_t)ncheck * sizeof(*check_ufo))) ||
	    (nupdate &&
	     copy_from_user(update_off, u64_to_user_ptr(in->update_devvar_offset),
			    (size_t)nupdate * sizeof(*update_off))) ||
	    (nupdate &&
	     copy_from_user(update_val, u64_to_user_ptr(in->update_value),
			    (size_t)nupdate * sizeof(*update_val))) ||
	    (nupdate &&
	     copy_from_user(update_ufo, u64_to_user_ptr(in->update_ufo_block),
			    (size_t)nupdate * sizeof(*update_ufo)))) {
		pr_info("mt_pvr_bridge: kick sync arrays unreadable: check=%u update=%u\n",
			ncheck, nupdate);
		goto out;
	}
	for (i = 0; i < ncheck; i++) {
		total++;
		known += pvr_kick_ufo_known(file, check_ufo[i]);
	}
	for (i = 0; i < nupdate; i++) {
		total++;
		known += pvr_kick_ufo_known(file, update_ufo[i]);
	}
	pr_info("mt_pvr_bridge: kick sync inventory: check=%u update=%u ufo_known=%u/%u check_fd=%d timeline_fd=%d extref=%u\n",
		ncheck, nupdate, known, total, (int)in->check_fence_fd,
		(int)in->timeline_fence_fd, in->ext_job_ref);
out:
	kfree(check_off);
	kfree(check_val);
	kfree(check_ufo);
	kfree(update_off);
	kfree(update_val);
	kfree(update_ufo);
}

static int pvr_cmd_kicksync_submit(struct mt_pvr_file *file,
				   struct mt_pvr_cmd *cmd, u32 function)
{
	struct mt_pvr_kicksync2_in in2;
	struct mt_pvr_kicksync_prop_in in_prop;
	struct mt_pvr_kicksync3_in in3;
	struct mt_pvr_kicksync2_out out2 = { 0 };
	struct mt_pvr_kicksync_prop_out out_prop = { 0 };
	struct mt_pvr_kicksync3_out out3 = { 0 };
	struct mt_pvr_object *obj;
	int fence_fd;
	u64 handle;
	int ret;

	if (function == 0x2) {
		ret = pvr_in(cmd, &in2, sizeof(in2));
		if (ret)
			return ret;
		handle = in2.kicksync_context;
	} else if (function == 0x3) {
		ret = pvr_in(cmd, &in_prop, sizeof(in_prop));
		if (ret)
			return ret;
		handle = in_prop.kicksync_context;
	} else {
		ret = pvr_in(cmd, &in3, sizeof(in3));
		if (ret)
			return ret;
		handle = in3.kicksync_context;
	}
	list_for_each_entry(obj, &file->objects, link) {
		if (obj->handle == handle && obj->kind == MT_PVR_KIND_KICKSYNC)
			break;
	}
	if (&obj->link == &file->objects)
		return -ENOENT;
	/* A property query has no fence to complete. */
	if (function == 0x3)
		return pvr_out(cmd, &out_prop, sizeof(out_prop));
	if (function == 0x4)
		pvr_kick_inspect(file, &in3);
	/* A fence that is already complete: poll/select on it returns at once.
	 * Bridge-stage completion only -- the GPU did nothing, because there is
	 * no channel by which this bridge could ask it to.
	 */
	fence_fd = anon_inode_getfd("pvr-fence", &pvr_fence_fops, NULL,
				    O_RDWR | O_CLOEXEC);
	if (fence_fd < 0)
		return fence_fd;
	if (function == 0x2) {
		out2.update_fence_fd = fence_fd;
		ret = pvr_out(cmd, &out2, sizeof(out2));
	} else {
		out3.update_fence_fd = fence_fd;
		ret = pvr_out(cmd, &out3, sizeof(out3));
	}
	/* The fd is live in the caller's table now; if the OUT write failed
	 * the number never reached userspace, so drop our reference.
	 */
	if (ret)
		close_fd(fence_fd);
	return ret;
}

/* 0x82:0x2 RGXCreateZSBuffer and 0x82:0x3 RGXDestroyZSBuffer.
 *
 * Object lifecycle only: create mints a per-file handle for the PMR +
 * reservation pair the UMD already allocated and mapped, destroy retires it.
 * The UMD-side ZSBuffer object (with its mutex and mapping state) lives
 * entirely in userspace; the bridge only tracks the kernel handle.
 */
static int pvr_cmd_zs_create(struct mt_pvr_file *file,
			     struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_zs_create_in in;
	struct mt_pvr_zs_create_out out = { 0 };
	struct mt_pvr_object *obj;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	obj = pvr_object_new(file, MT_PVR_KIND_ZSBUFFER);
	if (!obj)
		return -ENOMEM;
	out.zs_buffer_km = obj->handle;
	return pvr_out(cmd, &out, sizeof(out));
}

static int pvr_cmd_zs_destroy(struct mt_pvr_file *file,
			      struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_zs_destroy_in in;
	struct mt_pvr_zs_destroy_out out = { 0 };
	struct mt_pvr_object *obj;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	list_for_each_entry(obj, &file->objects, link) {
		if (obj->handle == in.zs_buffer &&
		    obj->kind == MT_PVR_KIND_ZSBUFFER) {
			list_del(&obj->link);
			kfree(obj);
			return pvr_out(cmd, &out, sizeof(out));
		}
	}
	return -ENOENT;
}

/* 0x81:0x0 RGXCreateComputeContext and 0x81:0x1 RGXDestroyComputeContext.
 *
 * Object lifecycle only: create mints a per-file compute-context handle,
 * destroy retires it. The UMD-side framework/static blobs are inputs only.
 */
static int pvr_cmd_compute_create(struct mt_pvr_file *file,
				  struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_compute_create_in in;
	struct mt_pvr_compute_create_out out = { 0 };
	struct mt_pvr_object *obj;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	obj = pvr_object_new(file, MT_PVR_KIND_COMPUTE);
	if (!obj)
		return -ENOMEM;
	out.compute_context = obj->handle;
	return pvr_out(cmd, &out, sizeof(out));
}

static int pvr_cmd_compute_destroy(struct mt_pvr_file *file,
				   struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_compute_destroy_in in;
	struct mt_pvr_compute_destroy_out out = { 0 };
	struct mt_pvr_object *obj;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	list_for_each_entry(obj, &file->objects, link) {
		if (obj->handle == in.compute_context &&
		    obj->kind == MT_PVR_KIND_COMPUTE) {
			list_del(&obj->link);
			kfree(obj);
			return pvr_out(cmd, &out, sizeof(out));
		}
	}
	return -ENOENT;
}

/* 0x88:0x0 RGXCreateKickSyncContext and 0x88:0x1 RGXDestroyKickSyncContext.
 *
 * Object lifecycle only: create mints a per-file context handle, destroy
 * retires it. No fence is waited on and no kick is submitted here.
 */
static int pvr_cmd_kicksync_create(struct mt_pvr_file *file,
				   struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_kicksync_create_in in;
	struct mt_pvr_kicksync_create_out out = { 0 };
	struct mt_pvr_object *obj;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	obj = pvr_object_new(file, MT_PVR_KIND_KICKSYNC);
	if (!obj)
		return -ENOMEM;
	out.kicksync_context = obj->handle;
	return pvr_out(cmd, &out, sizeof(out));
}

static int pvr_cmd_kicksync_destroy(struct mt_pvr_file *file,
				    struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_kicksync_destroy_in in;
	struct mt_pvr_kicksync_destroy_out out = { 0 };
	struct mt_pvr_object *obj;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	list_for_each_entry(obj, &file->objects, link) {
		if (obj->handle == in.kicksync_context &&
		    obj->kind == MT_PVR_KIND_KICKSYNC) {
			list_del(&obj->link);
			kfree(obj);
			return pvr_out(cmd, &out, sizeof(out));
		}
	}
	return -ENOENT;
}

/* 0x6:0x14 MM:DevmemIntUnmapPMR and 0x6:0x16 MM:DevmemIntUnreserveRange.
 *
 * The teardown counterparts of 0x6:0x13 and 0x6:0x15. Both take a single
 * widened handle and expect only eError back.
 *
 * They were falling through to -ENOTTY. The UMD issues one of each per mapping
 * it drops, so during RGXCreateRenderContext this was refused eight times over
 * and the first refusal came back as 38 = MTSRV_ERROR_IOCTL_CALL_FAILED.
 */
static int pvr_cmd_unmap_pmr(struct mt_pvr_file *file,
			     struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_unmap_pmr_in in;
	struct mt_pvr_unmap_out out = { 0 };
	struct mt_pvr_pmr *pmr;
	struct mt_pvr_binding *b, *btmp, *binding = NULL;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	/* The mapping value the bridge hands out is the PMR handle, so this
	 * resolves the same way. Unmapping a PMR that was never mapped (or
	 * already fully unmapped) is a UMD bug, not a no-op.
	 */
	pmr = pvr_pmr_find(file, in.mapping);
	if (!pmr || !pmr->mapped)
		return -ENOENT;
	list_for_each_entry(b, &file->bindings, link)
		if (b->pmr == pmr->handle) {
			binding = b;
			break;
		}
	if (!binding)
		return -EUCLEAN;
	ret = pvr_gpu_vm_unbind(file, binding);
	if (ret)
		return ret;
	if (!--pmr->mapped) {
		pmr->mapped_reservation = 0;
		list_for_each_entry_safe(b, btmp, &file->bindings, link) {
			if (b->pmr == pmr->handle) {
				list_del(&b->link);
				kfree(b);
			}
		}
	}
	return pvr_out(cmd, &out, sizeof(out));
}

static int pvr_cmd_unreserve_range(struct mt_pvr_file *file,
				   struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_unreserve_in in;
	struct mt_pvr_unmap_out out = { 0 };
	struct mt_pvr_object *obj;
	struct mt_pvr_pmr *pmr;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	obj = pvr_reservation_find(file, in.reservation);
	if (!obj)
		return -ENOENT;
	/* A range the page tables still reference must not disappear first.
	 * The UMD's order is unmap-then-unreserve, so this only fires on a
	 * real lifecycle violation.
	 */
	list_for_each_entry(pmr, &file->pmrs, link) {
		if (pmr->mapped && pmr->mapped_reservation == obj->handle)
			return -EBUSY;
	}
	list_del(&obj->link);
	kfree(obj);
	return pvr_out(cmd, &out, sizeof(out));
}

/* 0x6:0x27 MM:MTGPUUpdateOOMStats.
 *
 * Out-of-memory accounting only: the UMD reports a pid and a stat type, and
 * expects nothing back but eError. There is no memory to reclaim here, so the
 * input is validated and the call succeeds with a zeroed eError.
 *
 * This was falling through to -ENOTTY. The UMD issued it while creating a
 * render context, treated that as fatal, and returned error 1.
 */
static int pvr_cmd_oom_stats(struct mt_pvr_file *file,
			     struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_oom_stats_in in;
	struct mt_pvr_oom_stats_out out = { 0 };
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	return pvr_out(cmd, &out, sizeof(out));
}

/* 0x6:0x11 MM:DevmemIntHeapCreate.
 *
 * Register one heap inside an already-created device-memory context. The UMD
 * issues this right after DevmemIntCtxCreate while it walks the heap table
 * (it follows HeapCfgHeapCount/HeapCfgHeapDetails, so it only gets here once
 * those report real heaps).
 *
 * It was previously routed to the PMR-map handler, which parsed a different
 * 28-byte struct and answered -EINVAL.
 */
static int pvr_cmd_heap_create(struct mt_pvr_file *file,
			       struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_heap_create_in in;
	struct mt_pvr_heap_create_out out = { 0 };
	struct mt_pvr_object *heap;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	/* The heap must be one the config table handed out; anything else means
	 * the UMD is working from a base we never published.
	 */
	if (!mt_pvr_heaps_have_base(&file->heaps, in.heap_base_addr))
		return -EINVAL;
	heap = pvr_object_new(file, MT_PVR_KIND_HEAP);
	if (!heap)
		return -ENOMEM;
	out.devmem_heap_ptr = heap->handle;
	return pvr_out(cmd, &out, sizeof(out));
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
	/* Index the table by ui32HeapIndex, NOT ui32HeapConfigIndex.
	 *
	 * Measured against the real 5.2 UMD (11 consecutive calls):
	 *
	 *   cfg_index=0 heap_index=0
	 *   cfg_index=0 heap_index=1
	 *   ...
	 *   cfg_index=0 heap_index=10
	 *
	 * ui32HeapConfigIndex selects a *heap configuration*; there is only one,
	 * so it is always zero. ui32HeapIndex is the entry within it. Reading
	 * the config index made every call describe heap 0, so the UMD cached
	 * eleven copies of the same name and base and its own
	 * MTSRVFindHeapByName("PDS Code and Data") could never match. It then
	 * bailed out of RGXCreateDeviceMemContext and ran its error-cleanup
	 * path, which is where "double free or corruption" came from -- the
	 * double free was a symptom three layers downstream.
	 */
	index = in.heap_index;
	if (index >= file->heaps.count)
		return -EINVAL;
	entry = &file->heaps.entries[index];
	out.base = entry->base;
	out.length = entry->size;
	out.reserved_length = entry->reserved_size;
	out.log2_data_page_size = entry->log2_data_page_size;
	out.log2_import_alignment = entry->log2_import_alignment;
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

	/* The driver refcounts contexts per connection, so a second create must
	 * return the same object rather than a new one.
	 */
	ctx = pvr_object_of_kind(file, MT_PVR_KIND_CONTEXT);
	if (!ctx) {
		ctx = pvr_object_new(file, MT_PVR_KIND_CONTEXT);
		if (!ctx)
			return -ENOMEM;
		/* Heaps are NOT pre-created here. The UMD creates each one
		 * explicitly with DevmemIntHeapCreate (0x6:0x11), which returns
		 * the handle it is later given back in DevmemIntHeapDestroy
		 * (0x6:0x12). Minting a heap object per config entry produced
		 * eleven handles the UMD never saw, so the eleven destroys
		 * could not match them and the mismatch surfaced as a userspace
		 * double free.
		 */
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
	pmr->alloc_flags = in.flags;
	out.pmr = pmr->handle;
	out.out_flags = in.flags;
	out.is_system_mem = 1;
	return pvr_out(cmd, &out, sizeof(out));
}

/* 0x6:0x3 MM:PmrMakeLocalImportHandle -- export a PMR to this file.
 *
 * The handle stays in the same per-file handle space, so the exported handle
 * is the PMR's own handle. A missing PMR is -ENOENT, not a size error.
 */
static int pvr_cmd_pmr_make_import(struct mt_pvr_file *file,
				   struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_make_import_in in;
	struct mt_pvr_make_import_out out = { 0 };
	struct mt_pvr_pmr *pmr;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	pmr = pvr_pmr_find(file, in.buffer);
	if (!pmr)
		return -ENOENT;
	out.ext_mem = pmr->handle;
	return pvr_out(cmd, &out, sizeof(out));
}

static int pvr_cmd_pmr_unmake_import(struct mt_pvr_file *file,
				     struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_unmake_import_in in;
	struct mt_pvr_unmake_import_out out = { 0 };
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	if (!pvr_pmr_find(file, in.ext_mem))
		return -ENOENT;
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
	/* PVRSRV_BRIDGE_OUT_PMRLOCALIMPORTPMR declares uiAlign/uiSize/hPMR;
	 * hPMR is a handle like the hExtHandle it came from. The "<< 12" shift
	 * belongs to the mmap path only.
	 */
	out.pmr = pmr->handle;
	return pvr_out(cmd, &out, sizeof(out));
}

static int pvr_cmd_pmr_map(struct mt_pvr_file *file, struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_map_in in;
	struct mt_pvr_map_out out = { 0 };
	struct mt_pvr_pmr *pmr;
	struct mt_pvr_object *res;
	struct mt_pvr_binding *binding;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	pmr = pvr_pmr_find(file, in.pmr);
	if (!pmr)
		return -ENOENT;
	/* The range being programmed must exist and must fit the PMR. The OUT
	 * mapping value stays the PMR handle -- the UMD passes it back to
	 * UnmapPMR -- so this validation changes nothing on the wire.
	 */
	{
		struct mt_pvr_binding *b;
		u32 count = 0;

		res = pvr_reservation_find(file, in.reservation);

		if (!res)
			return -ENOENT;
		if (pmr->bytes > res->arg1)
			return -ENOSPC;
		/* One PMR programs one range at a time. A second live map of
		 * the same PMR would double-program its VA in a future page
		 * table; the measured ladder never does this.
		 */
		if (pmr->mapped)
			return -EBUSY;
		list_for_each_entry(b, &file->bindings, link) {
			if (++count >= MT_PVR_MAX_BINDINGS)
				return -ENOSPC;
		}
	}
	pmr->mapped++;
	pmr->mapped_reservation = in.reservation;
	res = pvr_reservation_find(file, in.reservation);
	binding = kzalloc(sizeof(*binding), GFP_KERNEL);
	if (!binding) {
		pmr->mapped--;
		pmr->mapped_reservation = 0;
		return -ENOMEM;
	}
	binding->va = res->arg0;
	binding->bytes = pmr->bytes;
	binding->pmr = pmr->handle;
	binding->reservation = in.reservation;
	binding->map_flags = in.map_flags;
	list_add_tail(&binding->link, &file->bindings);
	/* Opportunistic DMA registration. Any failure (in particular -ENODEV
	 * while no live session is bound) keeps system-memory semantics;
	 * the OUT value and return code are unchanged either way. If registration
	 * succeeds, build an unpublished CPU-only GPU page-table plan; unsupported
	 * alignment also degrades without changing the UMD wire result.
	 */
	ret = pvr_pmr_dma_register(file, pmr);
	binding->gpu_result = ret ? ret : pvr_gpu_vm_bind(file, pmr, binding);
	out.mapping = pmr->handle;
	return pvr_out(cmd, &out, sizeof(out));
}

/* Find a reservation by handle. Returns NULL for unknown handles and for
 * objects of any other kind: a mapping handle or a heap handle is not a
 * reservation, even though all handles share one space.
 */
static struct mt_pvr_object *pvr_reservation_find(struct mt_pvr_file *file,
						  u64 handle)
{
	struct mt_pvr_object *obj;

	list_for_each_entry(obj, &file->objects, link) {
		if (obj->handle == handle && obj->kind == MT_PVR_KIND_RESERVATION)
			return obj;
	}
	return NULL;
}

static int pvr_cmd_pmr_reserve(struct mt_pvr_file *file, struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_reserve_in in;
	struct mt_pvr_reserve_out out = { 0 };
	struct mt_pvr_object *obj, *other;
	u64 end;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	/* A reservation names the VA range a later MapPMR will program. Zero
	 * length reserves nothing; overflow must not wrap past the VA space.
	 * The address is deliberately NOT required to be page-aligned: the UMD
	 * packs sub-ranges byte-tight (measured: 0x8000010000+0x253 followed
	 * by 0x8000010253). Page alignment only matters when a future GPU
	 * page-table bind programs the range, which will round down there.
	 */
	if (!in.length ||
	    in.length > (1ULL << MT_GPU_VA_BITS) - in.address)
		return -EINVAL;
	end = in.address + in.length;
	/* Ranges on one file must not overlap: two live reservations over the
	 * same VA would program the same page-table entries twice.
	 */
	list_for_each_entry(other, &file->objects, link) {
		if (other->kind != MT_PVR_KIND_RESERVATION)
			continue;
		if (in.address < other->arg0 + other->arg1 &&
		    other->arg0 < end)
			return -EEXIST;
	}
	obj = pvr_object_new(file, MT_PVR_KIND_RESERVATION);
	if (!obj)
		return -ENOMEM;
	obj->arg0 = in.address;
	obj->arg1 = in.length;
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
	/* PVRSRV_BRIDGE_OUT_ALLOCSYNCPRIMITIVEBLOCK ends in
	 * ui32SyncPrimVAddr, a 32-bit virtual address -- not a handle. Since
	 * no stage-one work touches the sync arena, the CPU mapping of the
	 * PMR stands in for the GPU VA. Feeding a shifted handle (handle<<12)
	 * in here also overflows the field once handles climb past 0xffff.
	 */
	out.vaddr = (u32)(uintptr_t)pmr->host;
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

/* Hand a handle back: the teardown counterpart of pvr_cmd_handle_only().
 *
 * Called with file->lock already held by pvr_bridge_dispatch(), so it must not
 * take it again.
 */
static int pvr_cmd_handle_release(struct mt_pvr_file *file,
				  struct mt_pvr_cmd *cmd, u32 kind)
{
	struct mt_pvr_heap_destroy_in in;
	struct mt_pvr_unmap_out out = { 0 };
	struct mt_pvr_object *obj;
	int ret;

	/* A single widened MT_HANDLE, as in the 5.2 destroy structs. */
	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	list_for_each_entry(obj, &file->objects, link) {
		if (obj->handle == in.devmem_heap && obj->kind == kind) {
			list_del(&obj->link);
			kfree(obj);
			return pvr_out(cmd, &out, sizeof(out));
		}
	}
	return -ENOENT;
}

/* Drop a PMR from the file's table and free it. Returns -ENOENT if the handle
 * is unknown, so a double release is visible instead of silently accepted.
 */
/* Release the PMR's own list reference.
 *
 * The PMR may still be alive if something took a reference of its own -- most
 * importantly an in-flight mmap(), which has to keep using pmr->host after
 * dropping file->lock. Only unlinking is unconditional; the memory goes away
 * once the last user is done with it.
 *
 * Called with file->lock held (from pvr_bridge_dispatch()).
 */
static int pvr_pmr_put(struct mt_pvr_file *file, u64 handle)
{
	struct mt_pvr_pmr *pmr;

	pmr = pvr_pmr_find(file, handle);
	if (!pmr)
		return -ENOENT;
	/* PVR VM bindings hold a reference to this PMR's GPU-PA page list. */
	if (pmr->mapped)
		return -EBUSY;
	list_del(&pmr->link);
	pvr_pmr_unref(pmr);
	return 0;
}

/* 0x86:0x4 MUSAAcquireHWPerfSettings: hand back a real PMR.
 *
 * The offline session passed with a zeroed block, but that was only true
 * because the shim zeroed it. Against the real UMD a zero hPMR is fatal: the
 * very next command is MM:PmrLocalImportPmr with that handle, which found no
 * PMR and returned -ENOENT. MTGPU_BRIDGE_OUT_MUSAACQUIREHWPERFSETTING declares
 * hPMR, so this must allocate one like any other PMR-returning command.
 *
 * The block holds counters we cannot back, so it is zeroed and read-only in
 * practice; the UMD only needs the handle to import and map it.
 */
static int pvr_cmd_hwperf(struct mt_pvr_file *file, struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_handle_out out = { 0 };
	struct mt_pvr_pmr *pmr;

	pmr = pvr_pmr_new(file, 0x1000, 12);
	if (!pmr)
		return -ENOMEM;
	out.handle = pmr->handle;
	return pvr_out(cmd, &out, sizeof(out));
}

/* 0x86:0x5 MUSAReleaseHWPerfSettings: takes the handle back. */
static int pvr_cmd_hwperf_release(struct mt_pvr_file *file,
				  struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_hwperf_release_in in;
	struct mt_pvr_hwperf_release_out out = { 0 };
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	ret = pvr_pmr_put(file, in.pmr);
	if (ret)
		return ret;
	return pvr_out(cmd, &out, sizeof(out));
}

/* 0x89:0x5 RGXTDMGetSharedMemory and 0x89:0x6 RGXTDMReleaseSharedMemory.
 *
 * Transfer (2D/blit) shared memory for RGXTDMCreateStaticMem (r87): the UMD
 * passes no input and stores the two returned u64s at client+0x30/+0x38 for
 * TQPMR_MapMem / TQPMR_MapUSCMem. Both aliases point at ONE real 8 KiB
 * arena PMR (see mt_pvr_wire.h spike note); release retires it once via the
 * normal PMR path, so a second release honestly reports -ENOENT instead of
 * double-freeing. eError rides last in this family ({u64, u64, u32}).
 */
static int pvr_cmd_tdm_shmem(struct mt_pvr_file *file,
			     struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_tdm_shmem_out out = { 0 };
	struct mt_pvr_pmr *pmr;

	pmr = pvr_pmr_new(file, 0x2000, 12);
	if (!pmr)
		return -ENOMEM;
	out.ptr1 = pmr->handle;
	out.ptr2 = pmr->handle;
	out.error = 0;
	return pvr_out(cmd, &out, sizeof(out));
}

static int pvr_cmd_tdm_release(struct mt_pvr_file *file,
			       struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_tdm_release_in in;
	struct mt_pvr_tdm_release_out out = { 0 };
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	ret = pvr_pmr_put(file, in.handle);
	if (ret)
		return ret;
	out.error = 0;
	return pvr_out(cmd, &out, sizeof(out));
}

/* Succeed at a command whose only observable output is eError (and, for some,
 * a little zeroed data), by actually zeroing the caller's OUT buffer.
 *
 * A bare "return 0" is NOT equivalent. Every one of these out structs starts
 * with eError, and the UMD reads it out of its own buffer: PVRSRVConnect()
 * returns the value BridgeAlignmentCheck put there, so leaving the buffer
 * untouched makes the UMD read whatever was already in that memory and report
 * a bogus error. That is exactly how PVRSRVConnect came to return 37 while
 * every ioctl had returned 0.
 *
 * The whole declared out_size is zeroed because the 5.2 wire structs are wider
 * than the older headers in-tree describe (see mt_pvr_wire.h), so writing only
 * the field we recognise could still leave a tail the UMD reads. The cap keeps
 * a corrupt out_size from becoming a large copy.
 */
#define MT_PVR_STUB_OUT_MAX 64U

static int pvr_stub_ok(struct mt_pvr_cmd *cmd)
{
	u8 zeros[MT_PVR_STUB_OUT_MAX];
	u32 bytes = cmd->out_size;

	memset(zeros, 0, sizeof(zeros));
	if (!bytes || !cmd->out_ptr)
		return 0;
	if (bytes > sizeof(zeros))
		bytes = sizeof(zeros);
	return pvr_out(cmd, zeros, bytes);
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
			return pvr_stub_ok(cmd);
		case 0x2:			/* AcquireGlobalEventObject */
		case 0x4:			/* EventObjectOpen */
			return pvr_cmd_event_handle(file, cmd);
		case 0x3:			/* ReleaseGlobalEventObject */
		case 0x5:			/* EventObjectWait */
		case 0x6:			/* EventObjectClose */
		case 0xa:			/* AlignmentCheck */
		case 0xc:			/* GetMultiCoreInfo */
		case 0xd:			/* EventObjectWaitTimeout */
			return pvr_stub_ok(cmd);
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
			return pvr_stub_ok(cmd);
		default:
			return -ENOTTY;
		}
	case MT_PVR_BRIDGE_MM:
		switch (function) {
		case 0x3:			/* PmrMakeLocalImportHandle */
			return pvr_cmd_pmr_make_import(file, cmd);
		case 0x4:			/* PmrUnmakeLocalImportHandle */
			return pvr_cmd_pmr_unmake_import(file, cmd);
		case 0x6:			/* PmrLocalImportPmr */
			return pvr_cmd_pmr_import(file, cmd);
		case 0x7:			/* PmrUnrefPmr */
			return pvr_cmd_pmr_unref(file, cmd);
		case 0x10:			/* DevmemIntCtxDestroy */
			return pvr_cmd_ctx_destroy(file, cmd);
		case 0x12:			/* DevmemIntHeapDestroy */
			return pvr_cmd_heap_destroy(file, cmd);
		case 0x9:			/* PhysMemNewRamBackedPmr */
			return pvr_cmd_pmr_alloc(file, cmd);
		case 0xf:			/* DevmemIntCtxCreate */
			return pvr_cmd_ctx_create(file, cmd);
		case 0x11:			/* DevmemIntHeapCreate */
			return pvr_cmd_heap_create(file, cmd);
		case 0x13:			/* DevmemIntMapPmr */
			return pvr_cmd_pmr_map(file, cmd);
		case 0x14:			/* DevmemIntUnmapPMR */
			return pvr_cmd_unmap_pmr(file, cmd);
		case 0x15:			/* DevmemIntReserveRange */
			return pvr_cmd_pmr_reserve(file, cmd);
		case 0x16:			/* DevmemIntUnreserveRange */
			return pvr_cmd_unreserve_range(file, cmd);
		case 0x1e:			/* HeapCfgHeapCount */
			return pvr_cmd_heap_count(file, cmd);
		case 0x20:			/* HeapCfgHeapDetails */
			return pvr_cmd_heap_details(file, cmd);
		case 0x27:			/* MTGPUUpdateOOMStats */
			return pvr_cmd_oom_stats(file, cmd);
		default:
			return -ENOTTY;
		}
	case MT_PVR_BRIDGE_RGXCOMPUTE:
		switch (function) {
		case 0x0:			/* RGXCreateComputeContext */
			return pvr_cmd_compute_create(file, cmd);
		case 0x1:			/* RGXDestroyComputeContext */
			return pvr_cmd_compute_destroy(file, cmd);
		default:
			/* 0x81:0x5 RGXKICKSYNC2 and friends submit real work;
			 * refusing them is the S4 boundary, not a gap.
			 */
			return -ENOTTY;
		}
	case MT_PVR_BRIDGE_RGXTA3D:
		switch (function) {
		case 0x2:			/* RGXCreateZSBuffer */
			return pvr_cmd_zs_create(file, cmd);
		case 0x3:			/* RGXDestroyZSBuffer */
			return pvr_cmd_zs_destroy(file, cmd);
		case 0x8:			/* RGXCreateRenderContext */
			return pvr_cmd_handle_only(file, cmd,
						    MT_PVR_KIND_CONTEXT);
		case 0x9:			/* RGXDestroyRenderContext */
			/* The UMD always tears the context down, even when
			 * creation itself failed partway, so refusing this
			 * with -ENOTTY leaves the teardown incomplete.
			 */
			return pvr_cmd_handle_release(file, cmd,
						      MT_PVR_KIND_CONTEXT);
		default:
			return -ENOTTY;
		}
	case MT_PVR_BRIDGE_RGXKICKSYNC:
		switch (function) {
		case 0x0:			/* RGXCreateKickSyncContext */
			return pvr_cmd_kicksync_create(file, cmd);
		case 0x1:			/* RGXDestroyKickSyncContext */
			return pvr_cmd_kicksync_destroy(file, cmd);
		case 0x2:			/* RGXKickSync2 */
		case 0x3:			/* RGXSetKickSyncContextProperty */
		case 0x4:			/* RGXKickSync3 (TA submit) */
			return pvr_cmd_kicksync_submit(file, cmd, function);
		default:
			return -ENOTTY;
		}
	case MT_PVR_BRIDGE_RGXHWPERF:
		switch (function) {
		case 0x4:		/* MUSA:MUSAAcquireHWPerfSettings */
			return pvr_cmd_hwperf(file, cmd);
		case 0x5:		/* MUSA:MUSAReleaseHWPerfSettings */
			return pvr_cmd_hwperf_release(file, cmd);
		default:
			return -ENOTTY;
		}
	case MT_PVR_BRIDGE_RGXTDM:
		switch (function) {
		case 0x5:		/* RGXTDMGetSharedMemory */
			return pvr_cmd_tdm_shmem(file, cmd);
		case 0x6:		/* RGXTDMReleaseSharedMemory */
			return pvr_cmd_tdm_release(file, cmd);
		default:
			return -ENOTTY;
		}
	default:
		return -ENOTTY;
	}
}

/* drm_ioctl() has already copied the packet into kernel memory by the time it
 * reaches us: kdata is a stack buffer, and it copy_from_user()s _IOC_SIZE(cmd)
 * bytes into it (drm_ioctl.c, "Do not trust userspace, use our own
 * definition"). The vendor's own handler does the same thing --
 * PVRSRV_BridgeDispatchKM() casts arg straight to struct drm_pvr_srvkm_cmd
 * without a second copy. Only the in_ptr/out_ptr inside the packet remain
 * user pointers, and pvr_in()/pvr_out() are the ones that must fault-check
 * them.
 *
 * Calling copy_from_user() on raw here always returned -EFAULT, because raw is
 * a kernel address: it looked like a bad pointer, not like a bad argument.
 */
static int pvr_ioctl_bridge(struct drm_device *drm, void *raw,
			    struct drm_file *drm_file)
{
	struct mt_pvr_file *file = drm_file->driver_priv;
	struct mt_pvr_cmd cmd;
	int ret;

	(void)drm;
	if (!file)
		return -ENODEV;
	cmd = *(struct mt_pvr_cmd *)raw;
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
	init_module = *(u32 *)raw;
	/* PVR_SRVKM_SERVICES_INIT / PVR_SRVKM_SYNC_INIT in the vendor's
	 * pvr_drm.h: 1 selects the generic connection, 2 the device connection
	 * the render path needs (bA10).
	 */
	if (init_module != 1 && init_module != 2) {
		pr_info("mt_pvr_bridge: init_module=%u is not 1 or 2\n",
			init_module);
		return -EINVAL;
	}
	mutex_lock(&file->lock);
	file->init_module = init_module;
	mt_pvr_conn_init(file->conn, (u32)task_pid_nr(current), init_module);
	file->conn->features = (u64)(uintptr_t)file->features;
	file->conn->info_page = (u64)(uintptr_t)file->info_page;
	file->conn->hwperf_um = (u64)(uintptr_t)file->conn;
	mutex_unlock(&file->lock);
	return 0;
}

/* The render path opens a second node and names its sync timeline. The name is
 * only diagnostic at this stage, but it must be a valid string and it is
 * retained with the file so later fence work can use the right timeline.
 */
static int pvr_ioctl_sync_rename(struct drm_device *drm, void *raw,
				 struct drm_file *drm_file)
{
	struct mt_pvr_file *file = drm_file->driver_priv;
	struct mt_pvr_sync_rename_data *data = raw;

	(void)drm;
	if (!file)
		return -ENODEV;
	if (!memchr(data->name, '\0', sizeof(data->name)))
		return -EINVAL;
	mutex_lock(&file->lock);
	memcpy(file->sync_timeline, data->name, sizeof(file->sync_timeline));
	mutex_unlock(&file->lock);
	return 0;
}

static const struct drm_ioctl_desc pvr_ioctls[] = {
	DRM_IOCTL_DEF_DRV(PVR_BRIDGE, pvr_ioctl_bridge, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(PVR_INIT, pvr_ioctl_init, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(PVR_SYNC_RENAME, pvr_ioctl_sync_rename,
			  DRM_RENDER_ALLOW),
};

/* Map one of this file's PMRs. The UMD maps by DRM offset, and the offset is
 * the handle shifted left by a page (mt_pvr_handle_of); bA15 measured 28 of 28
 * calls agreeing on that, always 4 KiB aligned.
 *
 * The mapping is read/write. Stage 1 has no writable GPU memory behind these
 * pages, so a write lands in the PMR's own system-memory backing and is never
 * visible to the device -- acceptable now, and the reason the write flag is
 * not rejected.
 */
static int pvr_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *drm_file = filp->private_data;
	struct mt_pvr_file *file = drm_file ? drm_file->driver_priv : NULL;
	struct mt_pvr_pmr *pmr;
	u64 handle, offset;
	unsigned long length;
	int ret;

	if (!file)
		return -ENODEV;
	offset = vma->vm_pgoff << PAGE_SHIFT;
	length = vma->vm_end - vma->vm_start;
	if (mt_pvr_handle_of(offset, &handle)) {
		pr_info("mt_pvr_bridge: mmap bad offset 0x%llx (pgoff 0x%llx)\n",
			offset, (unsigned long long)vma->vm_pgoff);
		return -EINVAL;
	}
	if (length > MT_PVR_MAX_MAP_BYTES)
		return -EINVAL;
	/* Take a reference while still under the lock, and give it up at the end.
	 *
	 * pvr_pmr_put() runs from a bridge command on this same fd and frees the
	 * PMR, so between dropping file->lock and finishing the mapping the
	 * pointer would otherwise be dangling. It is a live use-after-free: the
	 * reads below can touch freed memory, and vmalloc_to_page() on a stale
	 * host pointer feeds an arbitrary PFN to remap_pfn_range().
	 *
	 * No re-locking here. pvr_bridge_dispatch() holds file->lock for the
	 * whole call, so taking it again self-deadlocks on a plain mutex.
	 */
	mutex_lock(&file->lock);
	pmr = pvr_pmr_find(file, handle);
	if (pmr)
		pmr->refcount++;
	mutex_unlock(&file->lock);
	if (!pmr) {
		pr_info("mt_pvr_bridge: mmap no PMR for handle 0x%llx\n",
			handle);
		return -ENOENT;
	}
	/* From here on every exit has to drop the reference, so the checks below
	 * set ret and share one exit rather than returning directly.
	 */
	ret = 0;
	/* `offset` is the mapping's offset *into* the PMR, and the mapping starts
	 * from `host`, so bound the requested pages against the PMR's pages.
	 * Adding the offset here (as an earlier version did) rejects every
	 * full-PMR mapping, because offset is the handle shifted up by 12 and
	 * is far larger than the PMR. Comparing the already page-rounded VMA
	 * length against the exact PMR byte count would likewise reject every
	 * non-page-multiple PMR, even though vzalloc() backs its final page.
	 */
	if (!mt_pvr_mmap_fits(length, pmr->bytes, PAGE_SIZE)) {
		pr_info("mt_pvr_bridge: mmap len %lu does not fit pmr bytes %llu\n",
			length, pmr->bytes);
		ret = -EINVAL;
		goto out;
	}
	/* A PMR size is a byte count, while mmap works in pages. The UMD maps
	 * the exact allocated length, which is not always page-aligned. Round
	 * the mapping up: vzalloc() backs the whole final page, so no bytes
	 * outside the PMR are exposed.
	 */
	/* remap_vmalloc_range() cannot be used here: it requires the whole
	 * vmalloc area to match, and vzalloc() appends a guard page when the
	 * size is not a power of two, so it rejected every mapping with
	 * -EINVAL. remap_vmalloc_range_partial() takes an explicit size and
	 * does not care.
	 *
	 * No write protection is attempted: vm_flags is read-only on a live
	 * vma and drm_vma_flags_modify() is not available in this 6.12 header
	 * set. Stage-one PMRs are system memory, so a stray write lands in the
	 * PMR's own backing rather than anywhere the device can see. Revisit
	 * when real page tables exist and the two must agree.
	 */
	/* Map the PMR's system memory with remap_pfn_range().
	 *
	 * The two obvious alternatives are both unusable from a module on
	 * 6.12, which cost several rounds of guessing:
	 *
	 *   - remap_vmalloc_range() is exported, but it demands
	 *       if (!(area->flags & (VM_USERMAP | VM_DMA_COHERENT)))
	 *             return -EINVAL;
	 *     and vzalloc() sets neither flag. Setting VM_USERMAP needs
	 *     __vmalloc_node_range(), which modpost reports as undefined:
	 *     it is internal to mm/ and not exported.
	 *   - remap_vmalloc_range_partial() would accept an explicit size,
	 *     but it is unexported too.
	 *
	 * remap_pfn_range() is exported and imposes no flag requirement, so
	 * the PMR is described page by page. vzalloc() is already page
	 * backed, which is what makes this a remap rather than a copy.
	 */
	{
		unsigned long pages = mt_pvr_mmap_page_count(length, PAGE_SIZE);
		unsigned long i;
		struct page *page;

		if (!pages) {
			ret = -EINVAL;
			goto out;
		}
		page = vmalloc_to_page(pmr->host);
		for (i = 0; i < pages; i++) {
			/* PAGE_KERNEL is a *kernel* pgprot: its _PAGE_USER bit
			 * is clear, so the resulting PTE is not reachable from
			 * user space and the first read faults (observed as a
			 * segfault at info_page+0x48). Or in _PAGE_USER to make
			 * it a genuine userspace mapping.
			 */
			ret = remap_pfn_range(vma,
					vma->vm_start + (i << PAGE_SHIFT),
					page_to_pfn(page + i), PAGE_SIZE,
					__pgprot(pgprot_val(PAGE_KERNEL) |
						 _PAGE_USER));
			if (ret)
				break;
		}
	}
out:
	/* The mapping is built page by page out of pmr->host, so the PMR has to
	 * outlive this call even though nothing references it afterwards. Give
	 * the reference back under the file lock: the free path can return an
	 * arena segment, and the arena free list lives under this mutex. The
	 * entry path already dropped it, so taking it here never recurses.
	 */
	mutex_lock(&file->lock);
	pvr_pmr_unref(pmr);
	mutex_unlock(&file->lock);
	return ret;
}

/* DRM 6.12 refuses to open a node whose file operations do not declare
 * FOP_UNSIGNED_OFFSET: drm_open_helper() warns and returns -EINVAL, and the
 * driver callback is never reached (drm_file.c:312). Every open of this node
 * failed with EINVAL until this flag was set.
 */
static const struct file_operations pvr_fops = {
	.owner = THIS_MODULE,
	.fop_flags = FOP_UNSIGNED_OFFSET,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.compat_ioctl = drm_compat_ioctl,
	.mmap = pvr_mmap,
};

static struct drm_driver pvr_driver = {
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
	/* r134: the vendor UMD derives features+0x54 as (major == 2) + 1. */
	pvr_driver.major = drm_major;
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
	pvr_drm = drm;
	pci_dev_put(pdev);
	WRITE_ONCE(pvr_ready, true);
	pr_info("mt_pvr_bridge: registered '%s' node, bridge stage 1: "
		"main module %s\n", MT_PVR_DRV_NAME,
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
