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
#include "../mt_gfx_context.h"
#include "../mt_gfx_context_data.h"
#include "../mt_mmu.h"
#include "../mt_guest_device.h"
#include "../mt_render_context.h"
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/file.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/sync_file.h>
#include "../mt_translate_kick.h"
#include "../mt_addr_plan.h"
#include "../mt_gfx_packet_template.h"
#include "../mt_transfer_fill.h"
#include "../mt_tqx_fill.h"
#include "../mt_tqx_work.h"
#include "../mt_tqx_topology.h"
#include "../mt_tqx_fill_work.h"

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

/* Check-only kick translator (first frame, r113/r147). Off by default, which
 * preserves the validated accept-and-inspect path bit-for-bit. When on, a
 * 0x88:0x4 kick with any check/update counts is translated: checks are
 * waited in PMR host memory, an empty DM2 marker is submitted through the
 * live firmware session, updates are published on completion, and a fence
 * backed by real completion is returned instead of the always-ready eventfd.
 * Zero-count kicks stay on inspect (no reason to burn GPU time).
 */
static bool translate_kick;
module_param(translate_kick, bool, 0400);
MODULE_PARM_DESC(translate_kick, "translate check-only 0x88:0x4 kicks into real DM2 empty markers (default: accept-and-inspect)");

static unsigned int translate_wait_ms = 5000U;
module_param(translate_wait_ms, uint, 0400);
MODULE_PARM_DESC(translate_wait_ms, "UFO condition wait budget per translated kick, milliseconds");

/* Transfer translator dry-run (r181). Off by default, which preserves the
 * accept-and-log path bit-for-bit. When on, a SubmitTransfer3 additionally
 * resolves its pool, parses geometry/color and builds the TQX fill program
 * bytes the submission would emit -- then stops: no session objects, no
 * page-table changes, no submission, no fence. The program digest in dmesg
 * is the observation; live emission is a later round.
 */
static bool translate_transfer;
module_param(translate_transfer, bool, 0400);
MODULE_PARM_DESC(translate_transfer, "dry-run transfer translation: build (never submit) the TQX fill program for 0x89:0xa (default: observe only)");

/* TQX context bring-up (r182). Off by default. When on, the first
 * SubmitTransfer3 also builds the flavor-1 TQX context plus command/DMA/state
 * Bos inside prepare (before its seal); a session whose translator is already
 * sealed misses the window and fails loudly instead of half-working.
 */
static bool translate_tqx_ctx;
module_param(translate_tqx_ctx, bool, 0400);
MODULE_PARM_DESC(translate_tqx_ctx, "build the translator TQX context and Bos on first 0x89:0xa (default: off; no submission)");

static bool translate_tqx_fire;
module_param(translate_tqx_fire, bool, 0400);
MODULE_PARM_DESC(translate_tqx_fire, "live TQX fill fire for 0x89:0xa (default: observe only; submit, async scratch readback, no UMD pool writeback)");
static bool translate_submit3_bump;
module_param(translate_submit3_bump, bool, 0400);
MODULE_PARM_DESC(translate_submit3_bump, "write submit3 update values into their sync PMRs at observe time (default: off; instant completion, no execution)");
static bool translate_fire_to_dst;
module_param(translate_fire_to_dst, bool, 0400);
MODULE_PARM_DESC(translate_fire_to_dst, "copy each verified fire chunk into the UMD destination pool (default: off; scratch-only otherwise)");
static unsigned int translate_fire_color;
module_param(translate_fire_color, uint, 0400);
MODULE_PARM_DESC(translate_fire_color, "override the pool-parsed fill color (default: 0 = pool color; r308 color sweep)");

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

/* Bridge-side PMR allocation presets (r188): sync/UFO block, hwperf
 * settings handle, and TDM shared-memory pair. Sizes, not policies.
 */
#define MT_PVR_SYNC_BLOCK_BYTES 0x1000U
#define MT_PVR_HWPERF_PMR_BYTES 0x1000U
#define MT_PVR_TDM_SHMEM_BYTES 0x2000U

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
	MT_PVR_KIND_TDM_CONTEXT,
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
	 * SYNC carries its backing PMR handle (the waitable memory);
	 * everything else leaves these zero. The range is what a future GPU
	 * page-table bind will program; recording it now (with overlap checks)
	 * is what makes that bind possible later without changing the wire.
	 */
	u64 arg0;
	u64 arg1;
	/* R6 Route A (r387/r388): per-context real state. NULL = uninitialized
	 * or non-CONTEXT kind. kzalloc in pvr_object_new() zeroes it. */
	struct mt_pvr_render_context *render_ctx;
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
static int pvr_submit3_transfer_dry_run(struct mt_pvr_file *file,
					const struct mt_pvr_tdm_submit3_in *in,
					u64 ccb_pmr);
static int pvr_submit3_transfer_fire(struct mt_pvr_file *file,
				     struct mt_guest_device *d,
				     u64 ccb_pmr, u64 force_pmr);
static void pvr_translator_fire_work(struct work_struct *ws);

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
	ret = pvr_arena_pages_ensure(file);
	if (ret) {
		kvfree(file->gpu_vm_storage);
		file->gpu_vm_storage = NULL;
		return ret;
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
		kvfree(file->gpu_vm_storage);
		file->gpu_vm_storage = NULL;
		memset(&file->gpu_vm, 0, sizeof(file->gpu_vm));
		return ret;
	}
	ret = mt_bo_put(&file->gpu_tables); /* leave only the VM's table reference */
	if (ret) {
		WARN_ON(mt_gpu_vm_fini(&file->gpu_vm));
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
		pci_get_domain_bus_and_slot(0, 0, MT_PVR_PCI_DEVFN);
	struct module *owner = NULL;
	struct mt_guest *g = NULL;

	if (!pdev)
		return NULL;
	device_lock(&pdev->dev);
	if (!pdev->driver || strcmp(pdev->driver->name, MT_GUEST_DRIVER_NAME))
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
	pdev = pci_get_domain_bus_and_slot(0, 0, MT_PVR_PCI_DEVFN);
	if (!pdev) {
		ret = -ENODEV;
		goto put_session;
	}
	device_lock(&pdev->dev);
	if (!pdev->driver || strcmp(pdev->driver->name, MT_GUEST_DRIVER_NAME) ||
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


struct mt_bridge_ta_vm;

/* r390: forward decl for R6-3 destroy (defined after mt_render_context_vm_destroy). */
static void mt_render_context_destroy(struct mt_pvr_render_context *ctx);

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
	/* DMA registration can allocate the page-address table before the lazy
	 * VM is initialized. In that case destroy() is a no-op, so close owns the
	 * final cleanup of the still-unattached table.
	 */
	kfree(file->arena_gpu_pages);
	file->arena_gpu_pages = NULL;
	list_for_each_entry_safe(pmr, tmp, &file->pmrs, link) {
		list_del(&pmr->link);
		while (pmr->refcount > 1)
			pvr_pmr_unref(pmr);
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
		/* r390 R6-3/V4: Tear down real per-context state on file close. */
		if (obj->render_ctx) {
			mt_render_context_destroy(obj->render_ctx);
			kfree(obj->render_ctx);
		}
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
		!strcmp(pdev->driver->driver.name, MT_GUEST_DRIVER_NAME);
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

/* Find an object by handle and kind (r189: unifies the per-handler search
 * loops). NULL covers unknown handles and wrong-kind handles alike:
 * neither is retirable. Callers hold file->lock (all dispatch paths).
 */
static struct mt_pvr_object *pvr_object_find(struct mt_pvr_file *file,
					     u64 handle, u32 kind)
{
	struct mt_pvr_object *obj;

	list_for_each_entry(obj, &file->objects, link)
		if (obj->handle == handle && obj->kind == kind)
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

/* The target Rogue topology is one core (live probe/TQX observation r28).
 * Reporting zero through the generic stub makes the UMD request a zero-byte
 * TDM context-store allocation and abort before creating a CCB. */
static int pvr_cmd_multicore_info(struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_multicore_info_in in;
	struct mt_pvr_multicore_info_out out = { 0 };
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	out.caps = in.caps;
	out.num_cores = 1;
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
	obj = pvr_object_find(file, in.devmem_heap, MT_PVR_KIND_CONTEXT);
	if (!obj)
		return -ENOENT;
	list_del(&obj->link);
	kfree(obj);
	return pvr_out(cmd, &out, sizeof(out));
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
	obj = pvr_object_find(file, in.devmem_heap, MT_PVR_KIND_HEAP);
	if (!obj)
		return -ENOENT;
	list_del(&obj->link);
	kfree(obj);
	return pvr_out(cmd, &out, sizeof(out));
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

/* Check-only kick translator: empty-DM2-marker emission (r113/r147).
 *
 * Global (one DM context for the bridge, shared by all files under
 * translator_lock; UFO resolution stays per-file). Lifecycle: prepared lazily
 * on the first translated kick, torn down at module exit. The owner ref taken
 * at prepare pins the probe module while translator objects reference its
 * stores; teardown requires the session (bridge-rmmod-first discipline).
 *
 * Locking: dispatch holds file->lock; here translator_lock is taken, then
 * trial_lock only around store/submit operations. No path takes file->lock
 * under trial_lock (probe/live code never sees bridge files) and nothing
 * else takes translator_lock, so the order is deadlock-free. The UFO wait
 * holds file->lock only and sleeps in interruptible slices.
 * Translator scene addresses live in ../mt_addr_plan.h (r186).
 */

struct mt_pvr_translator {
	bool ready;
	struct module *owner;
	struct mt_guest *guest;
	struct mt_guest_device *dev;
	struct mt_vm_vram *space;
	struct mt_execution_process process;
	struct mt_execution_context context;
	struct mt_bo command;
	struct mt_bo rt;
	struct mt_bo ctx_bos[MT_GFX_CONTEXT_BO_COUNT];
	/* TQX flavor (r182): second context under the same process plus the
	 * command/DMA/state Bos a TQX fill submission needs. Built lazily by
	 * the bring-up below; torn down with everything else. No submission
	 * happens here.
	 */
	bool tqx_ready;
	struct mt_execution_context tqx_context;
	struct mt_bo tqx_cmd, tqx_dma, tqx_state;
	/* Scratch stream Bos for the slices prepare (r263): bound before
	 * the seal alongside the other TQX Bos (a sealed space refuses
	 * binds); released at teardown. Their VM bindings die with the
	 * space.
	 */
	struct mt_bo tqx_tmp_src, tqx_tmp_dst;
	/* Pool slices for the TQX context (r261): filled by a one-shot
	 * copy prepare during bring-up so a later fill submission finds
	 * its shader/PDS storage ready. Non-fatal: DM bring-up stays up
	 * when slices fail; fire checks this flag.
	 */
	bool tqx_slices_ready;
	/* Live-fire scratch (r267): pre-seal 256KB surface + serialized
	 * work fire (r290: prepare -> submit -> wait -> verify per chunk;
	 * pipelining all prepares self-blocks with -EBUSY, r283). One fire
	 * at a time (fire_running guards); the work touches
	 * translator-owned memory only, never file objects.
	 */
#define MT_TQX_FIRE_MAX_CHUNKS 64U
	struct mt_bo tqx_scratch;
	struct work_struct fire_work;
	u32 fire_nchunks;
	u32 fire_chunk_h;
	u32 fire_color;
	u32 fire_width;
	u32 fire_height;
	u32 fire_cores;
	u64 fire_seq;
	bool fire_running;
	bool fire_pending;
	bool fire_abort;
	/* Fire-into-destination (r300): UMD pool host recorded at schedule
	 * (file alive under file->lock); the work copies each verified
	 * chunk there. Single-threaded UMD blocked in our ioctl cannot
	 * close mid-flight; teardown cancels the work first.
	 */
	void *fire_dst_host;
	u64 fire_dst_span;
	bool fire_to_dst;
	int fire_result;
	u64 seq;
};

static DEFINE_MUTEX(translator_lock);
static struct mt_pvr_translator translator;

static int pvr_translator_bo_write(struct mt_guest_device *d,
				   struct mt_bo *bo, u64 off,
				   const void *src, u64 bytes);
static void pvr_translator_tqx_slices(struct mt_guest_device *d);

struct mt_pvr_ufo_cond {
	void *host;
	u32 offset;
	u32 expected;
	u64 gpu_pa;
	bool has_gpu_pa;
};

/* Range-check offset against one PMR and fill the wait condition.
 * PMR list membership already established by the caller. */
static int pvr_translator_pmr_cond(struct mt_pvr_pmr *pmr, u32 offset,
				   struct mt_pvr_ufo_cond *out)
{
	u64 idx;

	if (!pmr->host || !pmr->bytes)
		return -EOPNOTSUPP;
	if ((u64)offset + sizeof(u32) > pmr->bytes)
		return -ERANGE;
	out->host = pmr->host;
	out->offset = offset;
	out->gpu_pa = 0;
	out->has_gpu_pa = false;
	idx = (u64)offset >> PAGE_SHIFT;
	if (pmr->gpu_pages && idx < pmr->dma_npages)
		out->gpu_pa = pmr->gpu_pages[idx] +
			       ((u64)offset & (PAGE_SIZE - 1)),
		out->has_gpu_pa = true;
	return 0;
}

static int pvr_translator_resolve(struct mt_pvr_file *file, u64 handle,
				  u32 offset,
				  struct mt_pvr_ufo_cond *out)
{
	struct mt_pvr_pmr *pmr;
	struct mt_pvr_object *obj;

	/* A PMR handle resolves directly; a SYNC object handle follows its
	 * backing PMR link. Anything else has no CPU-visible memory to wait
	 * on. file->lock held. */

	list_for_each_entry(pmr, &file->pmrs, link) {
		if (pmr->handle == handle)
			return pvr_translator_pmr_cond(pmr, offset, out);
	}
	list_for_each_entry(obj, &file->objects, link) {
		if (obj->handle == handle && obj->kind == MT_PVR_KIND_SYNC &&
		    obj->arg0) {
			pmr = pvr_pmr_find(file, obj->arg0);
			if (pmr)
				return pvr_translator_pmr_cond(pmr, offset, out);
		}
	}
	return -EOPNOTSUPP;
}


/* Wait until every condition reads its expected value. file->lock held,
 * no other locks; interruptible slices so a stuck UMD can still be killed. */
static int pvr_translator_wait(struct mt_pvr_ufo_cond *conds, u32 n)
{
	unsigned long deadline = jiffies + msecs_to_jiffies(translate_wait_ms);
	u32 i;

	for (;;) {
		bool ok = true;

		for (i = 0; i < n; i++) {
			u32 v;

			memcpy(&v, (u8 *)conds[i].host + conds[i].offset,
			       sizeof(v));
			if (v != conds[i].expected) {
				ok = false;
				break;
			}
		}
		if (ok)
			return 0;
		if (time_after_eq(jiffies, deadline))
			return -ETIMEDOUT;
		if (msleep_interruptible(MT_TRANSLATE_WAIT_SLICE_MS))
			return -ERESTARTSYS;
		if (signal_pending(current))
			return -ERESTARTSYS;
	}
}

/* 0x2:0xa SYNC:SyncPrimCpuSignal (r222): write one u32 into a sync PMR.
 * IN = { sync handle, dword index, value }: identical 16-byte packing to
 * the 0x2:0x2 clearer wrapper, per the hash-verified 5.2.0 UMD setter path
 * (objdump: function 0xa) and the generated header
 * MTGPU_BRIDGE_IN_SYNCPRIMCPUSIGNAL. Handle resolution mirrors
 * pvr_translator_resolve (raw PMR handle, or a SYNC-kind object following
 * its backing PMR link); anything else has no CPU-visible memory and is
 * refused. The write lands in PMR host memory that translator waiters
 * poll, so a value preset here is visible to a later kick with no
 * wakeup needed.
 */
static int pvr_cmd_syncprim_set(struct mt_pvr_file *file,
				struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_syncprimset_in in;
	struct mt_pvr_syncprimset_out out = { 0 };
	struct mt_pvr_ufo_cond cond;
	u64 off;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	off = (u64)in.index * sizeof(u32);
	if (off > (u64)U32_MAX)
		return -ERANGE;
	ret = pvr_translator_resolve(file, in.sync, (u32)off, &cond);
	if (ret)
		return ret;
	memcpy((u8 *)cond.host + cond.offset, &in.value, sizeof(in.value));
	pr_info("mt_pvr_bridge: syncprimset: sync=%#llx off=%u val=%u\n",
		(unsigned long long)in.sync, (u32)off, in.value);
	return pvr_out(cmd, &out, sizeof(out));
}

/* 0x2:0xc SYNC:SyncPrimImportFD (r386).
 * IN = { u32 fd, u64 hSyncBlock, u32 offset, u64 hDevmemCtx } (24B);
 * OUT = { u64 value, u32 error } (12B). Wire layout from the hash-verified
 * KMD 5.2.0 generated header MTGPU_BRIDGE_IN/OUT_SYNCPRIMIMPORTFD
 * (reference/kmd-5.2.0-server-generated/common_sync_bridge.h:243/252),
 * confirmed against the UMD ZeusSyncPrimImportFD wrapper FUN_00139990
 * (decompiled.c:12418).
 *
 * Semantics: import a sync primitive from a Linux FD into the caller's
 * sync block at the given dword offset. The FD is produced by
 * SyncPrimExportFD (0x2:0xb, not yet implemented) for cross-process
 * sync prim sharing (e.g. UMD <-> compositor).
 *
 * Current implementation (offline, r386):
 * - Resolves hSyncBlock via pvr_translator_resolve (must be a real
 *   MT_PVR_KIND_SYNC object; -EOPNOTSUPP / -ERANGE otherwise).
 * - Validates the FD with fdget (must refer to an open file; -EBADF
 *   otherwise). The FD's sync-prim payload is NOT interpreted yet:
 *   resolving a sync_file / dma-buf / PVR-private export to a fence
 *   value is TO-VALIDATE (needs a real ExportFD producer + live UMD).
 * - Returns the current u32 value at the offset (zero-extended to u64)
 *   with eError = 0 (MTGPU_OK).
 *
 * Called with file->lock already held by pvr_bridge_dispatch(), so it
 * must not take it again.
 */
static int pvr_cmd_syncprim_importfd(struct mt_pvr_file *file,
				     struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_syncprimimportfd_in in;
	struct mt_pvr_syncprimimportfd_out out = { 0 };
	struct mt_pvr_ufo_cond cond;
	struct fd f;
	u32 val;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	ret = pvr_translator_resolve(file, in.sync_block, in.offset, &cond);
	if (ret)
		return ret;
	f = fdget(in.fd);
	if (fd_empty(f))
		return -EBADF;
	/* TO-VALIDATE (r386): interpret f.file as a sync-prim export
	 * (sync_file? dma-buf? PVR private?) and transfer its fence value
	 * into the destination. For now, log the import request. */
	pr_info("mt_pvr_bridge: syncprimimportfd: fd=%u sync=%#llx off=%u devmemctx=%#llx (FD payload TO-VALIDATE)\n",
		in.fd, (unsigned long long)in.sync_block, in.offset,
		(unsigned long long)in.devmem_ctx);
	fdput(f);
	memcpy(&val, (u8 *)cond.host + cond.offset, sizeof(val));
	out.value = val;
	out.error = 0;
	return pvr_out(cmd, &out, sizeof(out));
}

/* Drop translator objects. translator_lock held; takes trial_lock.
 * Partial-state safe: every destroy primitive rejects empty input. */
static void pvr_translator_teardown_locked(void)
{
	struct mt_guest_device *d = translator.dev;

	/* The fire work touches translator Bos; abort it, then stop it
	 * before tearing anything down (it takes no translator_lock, so
	 * no deadlock). The abort flag bounds cancel latency to one chunk.
	 */
	WRITE_ONCE(translator.fire_abort, true);
	cancel_work_sync(&translator.fire_work);
	WRITE_ONCE(translator.fire_running, false);
	WRITE_ONCE(translator.fire_pending, false);
	if (d && translator.context.process)
		WARN_ON(mt_execution_context_destroy(&translator.context));
	if (d && translator.tqx_context.process) {
		if (translator.tqx_slices_ready)
			WARN_ON(mt_tqx_context_pool_slices_release(
						&translator.tqx_context));
		WARN_ON(mt_execution_context_destroy(&translator.tqx_context));
	}
	if (d && translator.process.store)
		WARN_ON(mt_execution_process_destroy(&translator.process));
	if (translator.command.refs)
		WARN_ON(mt_bo_put(&translator.command));
	if (translator.rt.refs)
		WARN_ON(mt_bo_put(&translator.rt));
	if (translator.tqx_cmd.refs)
		WARN_ON(mt_bo_put(&translator.tqx_cmd));
	if (translator.tqx_dma.refs)
		WARN_ON(mt_bo_put(&translator.tqx_dma));
	if (translator.tqx_state.refs)
		WARN_ON(mt_bo_put(&translator.tqx_state));
	if (translator.tqx_tmp_dst.refs)
		WARN_ON(mt_bo_put(&translator.tqx_tmp_dst));
	if (translator.tqx_tmp_src.refs)
		WARN_ON(mt_bo_put(&translator.tqx_tmp_src));
	if (translator.tqx_scratch.refs)
		WARN_ON(mt_bo_put(&translator.tqx_scratch));
	{
		u32 i;

		for (i = 0; i < MT_GFX_CONTEXT_BO_COUNT; i++)
			if (translator.ctx_bos[i].refs)
				WARN_ON(mt_bo_put(&translator.ctx_bos[i]));
	}
	if (d && translator.space)
		WARN_ON(d->address_spaces.ops->destroy(translator.space));
	if (translator.owner)
		module_put(translator.owner);
	memset(&translator, 0, sizeof(translator));
}

/* Build the DM context once. translator_lock held; takes trial_lock. */
static int pvr_translator_prepare_locked(void)
{
	struct module *owner = NULL;
	struct mt_guest *g;
	struct mt_guest_device *d;
	u32 off, chunk;
	int ret;
	int fail_at = 0;

	if (translator.ready)
		return 0;
	/* Fire work must be cancellable from the first teardown, including
	 * failed-prepare cleanup (r276: cancelling a never-INITed work
	 * warns). INIT is idempotent; failure paths below rely on it.
	 */
	INIT_WORK(&translator.fire_work, pvr_translator_fire_work);
	g = pvr_session_acquire(&owner);
	if (!g)
		return -ENODEV;
	d = container_of(g, struct mt_guest_device, state);
	mutex_lock(&g->trial_lock);
	ret = d->address_spaces.ops->create(&d->address_spaces,
					    MT_TRANSLATE_SPACE_PAGES,
					    &translator.space);
	if (ret)
		{ fail_at = __LINE__; goto out; }
	pr_info("mt_pvr_bridge: translator space tables gpu_pa=%#llx capacity=%u\n",
		(unsigned long long)translator.space->tables.backing.gpu_pa,
		translator.space->vm.capacity);
	ret = d->address_spaces.ops->bind_boot_shared(translator.space,
						       &d->gem.profile);
	pr_info("mt_pvr_bridge: bind_boot_shared ret=%d\n", ret);
	if (ret)
		{ fail_at = __LINE__; goto out; }
	ret = mt_bo_create(&translator.command, d->buffers.ops, &d->buffers,
			   MT_TRANSLATE_CMD_BYTES, PAGE_SIZE);
	if (ret)
		{ fail_at = __LINE__; goto out; }
	ret = d->address_spaces.ops->bind(translator.space, &translator.command,
					  MT_TRANSLATE_CMD_VA, 0,
					  MT_TRANSLATE_CMD_BYTES,
					  MT_GPU_MAP_DEFAULT);
	if (ret)
		{ fail_at = __LINE__; goto out; }
	ret = mt_bo_create(&translator.rt, d->buffers.ops, &d->buffers,
			   MT_TRANSLATE_RT_BYTES, PAGE_SIZE);
	if (ret)
		{ fail_at = __LINE__; goto out; }
	ret = d->address_spaces.ops->bind(translator.space, &translator.rt,
					  MT_TRANSLATE_RT_VA, 0,
					  MT_TRANSLATE_RT_BYTES,
					  MT_GPU_MAP_DEFAULT);
	if (ret)
		{ fail_at = __LINE__; goto out; }
	/* Context switch (CSW) state, mirroring live_3d_drm's prepare: the raw
	 * template's CSW pointer names VAs valid only in another space, so the
	 * firmware would fault following it. Build the CSW for this space's
	 * context BOs and patch the packet's CSW pointer + words. */
	{
		struct mt_gfx_context_bo_addresses csw_addrs;
		u8 csw[MT_GFX_CONTEXT_CSW_BYTES];
		u64 csw_va = MT_TRANSLATE_CMD_VA + 0x58ULL;
		u32 i;

		for (i = 0; i < MT_GFX_CONTEXT_BO_COUNT; i++) {
			u32 bytes = mt_gfx_context_bo_specs[i].bytes;
			u32 alloc_size = PAGE_ALIGN(bytes);
			u64 bva = MT_CTX_BO_BASE_VA + i * MT_CTX_BO_STRIDE;

			csw_addrs.va[i] = bva;
			ret = mt_bo_create(&translator.ctx_bos[i],
					   d->buffers.ops, &d->buffers,
					   alloc_size, PAGE_SIZE);
			if (ret)
				{ fail_at = __LINE__; goto out; }
			ret = pvr_translator_bo_write(d,
						      &translator.ctx_bos[i], 0,
						      mt_gfx_bo_init_metas[i].data,
						      bytes);
			if (ret)
				{ fail_at = __LINE__; goto out; }
			ret = d->address_spaces.ops->bind(translator.space,
							  &translator.ctx_bos[i],
							  bva, 0, alloc_size,
							  MT_GPU_MAP_DEFAULT);
			if (ret)
				{ fail_at = __LINE__; goto out; }
		}
		ret = mt_gfx_context_build_csw(csw, sizeof(csw), &csw_addrs);
		if (ret)
			{ fail_at = __LINE__; goto out; }
		ret = pvr_translator_bo_write(d, &translator.command, 0x10,
					      &csw_va, 8);
		if (ret)
			{ fail_at = __LINE__; goto out; }
		ret = pvr_translator_bo_write(d, &translator.command, 0x58,
					      csw, sizeof(csw));
		if (ret)
			{ fail_at = __LINE__; goto out; }
	}
	/* TQX flavor (r182, opt-in): flavor-1 context under the same process
	 * plus command/DMA/state Bos at live_3d's VAs, bound before the seal
	 * below (a sealed space refuses binds and re-upload). Gated so the
	 * validated DM-only prepare stays bit-identical when off.
	 */
	if (translate_tqx_ctx) {
		static const u64 tqx_va[3] = { MT_TQX_CMD_VA, MT_TQX_DMA_VA,
					       MT_TQX_STATE_VA };
		static const u32 tqx_bytes[3] = { MT_TQX_CMD_BO_BYTES,
						  MT_TQX_DMA_BO_BYTES,
						  MT_TQX_STATE_BO_BYTES };
		struct mt_bo *tqx_bo[3] = {
			&translator.tqx_cmd, &translator.tqx_dma,
			&translator.tqx_state,
		};
		u32 k;

		/* NOTE: the flavor-1 context itself is created after the
		 * process exists (below, next to the DM context); only the
		 * Bos bind here, before the seal.
		 */
		for (k = 0; k < 3; k++) {
			ret = mt_bo_create(tqx_bo[k], d->buffers.ops,
					   &d->buffers, tqx_bytes[k], PAGE_SIZE);
			if (ret) {
				pr_info("mt_pvr_bridge: tqx bring-up: alloc %u: %d\n",
					k, ret);
				{ fail_at = __LINE__; goto out; }
			}
			ret = d->address_spaces.ops->bind(translator.space,
							  tqx_bo[k], tqx_va[k],
							  0, tqx_bytes[k],
							  MT_GPU_MAP_DEFAULT);
			if (ret) {
				pr_info("mt_pvr_bridge: tqx bring-up: bind %u: %d\n",
					k, ret);
				{ fail_at = __LINE__; goto out; }
			}
		}
		/* Scratch stream Bos for the slices prepare below (r263):
		 * must bind before the seal like the rest.
		 */
		ret = mt_bo_create(&translator.tqx_tmp_src, d->buffers.ops,
				   &d->buffers, MT_TQX_STREAM_SLOT_BYTES,
				   PAGE_SIZE);
		if (ret) {
			pr_info("mt_pvr_bridge: tqx bring-up: tmp alloc: %d\n",
				ret);
			{ fail_at = __LINE__; goto out; }
		}
		ret = mt_bo_create(&translator.tqx_tmp_dst, d->buffers.ops,
				   &d->buffers, MT_TQX_STREAM_SLOT_BYTES,
				   PAGE_SIZE);
		if (ret) {
			pr_info("mt_pvr_bridge: tqx bring-up: tmp alloc: %d\n",
				ret);
			{ fail_at = __LINE__; goto out; }
		}
		ret = d->address_spaces.ops->bind(translator.space,
						  &translator.tqx_tmp_src,
						  MT_TQX_STREAM_SRC_VA, 0,
						  MT_TQX_STREAM_SLOT_BYTES,
						  MT_GPU_MAP_DEFAULT);
		if (ret) {
			pr_info("mt_pvr_bridge: tqx bring-up: tmp bind: %d\n",
				ret);
			{ fail_at = __LINE__; goto out; }
		}
		ret = d->address_spaces.ops->bind(translator.space,
						  &translator.tqx_tmp_dst,
						  MT_TQX_STREAM_DST_VA, 0,
						  MT_TQX_STREAM_SLOT_BYTES,
						  MT_GPU_MAP_DEFAULT);
		if (ret) {
			pr_info("mt_pvr_bridge: tqx bring-up: tmp bind: %d\n",
				ret);
			{ fail_at = __LINE__; goto out; }
		}
		/* Live-fire scratch (r267): pre-seal surface for fills;
		 * async readback never touches file objects.
		 */
		ret = mt_bo_create(&translator.tqx_scratch, d->buffers.ops,
				   &d->buffers, MT_TQX_SCRATCH_BYTES,
				   PAGE_SIZE);
		if (ret) {
			pr_info("mt_pvr_bridge: tqx bring-up: scratch alloc: %d\n",
				ret);
			{ fail_at = __LINE__; goto out; }
		}
		ret = d->address_spaces.ops->bind(translator.space,
						  &translator.tqx_scratch,
						  MT_TQX_SCRATCH_VA, 0,
						  MT_TQX_SCRATCH_BYTES,
						  MT_GPU_MAP_DEFAULT);
		if (ret) {
			pr_info("mt_pvr_bridge: tqx bring-up: scratch bind: %d\n",
				ret);
			{ fail_at = __LINE__; goto out; }
		}
	}
	for (off = 0; off < MT_GFX_LINUX_PACKET_BYTES;) {
		chunk = MT_GFX_LINUX_PACKET_BYTES - off;
		if (chunk > PAGE_SIZE)
			chunk = PAGE_SIZE;
		ret = pvr_translator_bo_write(d, &translator.command, off,
						  mt_gfx_linux_packet_template + off,
						  chunk);
		if (ret)
			{ fail_at = __LINE__; goto out; }
		off += chunk;
	}
	{
		u64 va = MT_TRANSLATE_RT_VA;
		u64 stride = MT_TRANSLATE_RT_STRIDE;
		u64 extent = MT_TRANSLATE_RT_EXTENT;

		ret = pvr_translator_bo_write(d, &translator.command,
					      MT_TRANSLATE_RT_OFF_VA,
					      &va, 8);
		if (ret)
			{ fail_at = __LINE__; goto out; }
		ret = pvr_translator_bo_write(d, &translator.command,
					      MT_TRANSLATE_RT_OFF_STRIDE,
					      &stride, 8);
		if (ret)
			{ fail_at = __LINE__; goto out; }
		ret = pvr_translator_bo_write(d, &translator.command,
					      MT_TRANSLATE_RT_OFF_EXTENT,
					      &extent, 8);
		if (ret)
			{ fail_at = __LINE__; goto out; }
		ret = pvr_translator_bo_write(d, &translator.command,
					      MT_TRANSLATE_RT_OFF_DIRECT,
					      &va, 8);
		if (ret)
			{ fail_at = __LINE__; goto out; }
	}
	ret = d->address_spaces.ops->upload(translator.space);
	if (ret)
		{ fail_at = __LINE__; goto out; }
	ret = d->address_spaces.ops->seal(translator.space);
	if (ret)
		{ fail_at = __LINE__; goto out; }
	ret = mt_execution_process_create(&d->execution, &translator.process,
					  &translator.space->vm,
					  task_tgid_nr(current));
	if (ret)
		{ fail_at = __LINE__; goto out; }
	ret = mt_execution_context_create(&translator.context,
					  &translator.process, 5, 0);
	if (ret)
		{ fail_at = __LINE__; goto out; }
	/* TQX flavor (r182): the process exists only here, so the flavor-1
	 * context could never be created in the pre-seal block above (that
	 * was the -22: !p->store). Bos are already bound; completing here.
	 *
	 * Locking (r265): the slices prepare takes buffers->lock, which
	 * must never nest inside trial_lock (r263 AB-BA deadlock: the
	 * holder waits on trial_lock). Drop trial_lock across slices;
	 * translator_lock (held by all prepare callers) keeps a second
	 * prepare out. Slices stay non-fatal to DM bring-up.
	 */
	if (translate_tqx_ctx) {
		pr_info("mt_pvr_bridge: tqx bring-up: enter ctx block\n");
		ret = mt_execution_context_create(&translator.tqx_context,
						  &translator.process, 1, 0);
		if (ret) {
			pr_info("mt_pvr_bridge: tqx bring-up: context: %d\n",
				ret);
			{ fail_at = __LINE__; goto out; }
		}
		pr_info("mt_pvr_bridge: tqx bring-up: before slices\n");
		mutex_unlock(&g->trial_lock);
		pvr_translator_tqx_slices(d);
		mutex_lock(&g->trial_lock);
		pr_info("mt_pvr_bridge: tqx bring-up: after slices\n");
		translator.tqx_ready = true;
	}
	translator.owner = owner;
	translator.guest = g;
	translator.dev = d;
	translator.seq = 0;
	translator.fire_abort = false;
	translator.ready = true;
	mutex_unlock(&g->trial_lock);
	return 0;
out:
	if (ret)
		pr_info("mt_pvr_bridge: translator prepare failed at line %d: %d\n",
			fail_at, ret);
	pvr_translator_teardown_locked();
	mutex_unlock(&g->trial_lock);
	module_put(owner);
	return ret;
}

/* BO byte write for translator-owned objects (r147).
 *
 * Cannot use mt_bo_vram_write(): its ops-identity gate compares against the
 * CALLER module's copy of mt_bo_vram_ops, but that table is a static const
 * in a shared header, so the probe and the bridge each own a distinct copy
 * (live dmesg: ops=[mt_guest_probe] vs &mt_bo_vram_ops=[mt_pvr_bridge]).
 * Mirrors live_3d_drm's transfer(): validate against the owning store's ops
 * pointer (single copy, no duplication issue), then cpu_begin/end -- whose
 * map call dispatches through bo->ops to the correct copy. trial_lock held.
 */
static int pvr_translator_bo_write(struct mt_guest_device *d,
				   struct mt_bo *bo, u64 off,
				   const void *src, u64 bytes)
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

/* Mirror of the write path for the TQX upload ops' readback: verify what
 * was written by reading it back through the same CPU mapping.
 */
static int pvr_translator_bo_read(struct mt_guest_device *d,
				  struct mt_bo *bo, u64 off,
				  void *dst, u64 bytes)
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
		memcpy(dst, (u8 *)mapping + off, bytes);
	else
		memcpy_fromio(dst, (void __iomem *)mapping + off, bytes);
	return mt_bo_cpu_end(bo);
}

/* Upload-device for the TQX slice prepare below. Valid only inside
 * pvr_translator_tqx_slices (translator_lock held, no concurrency).
 */
static struct mt_guest_device *pvr_translator_upload_dev;

static int pvr_translator_upload_write(struct mt_bo *bo, u64 off,
				       const void *src, u64 bytes)
{
	struct mt_guest_device *d = READ_ONCE(pvr_translator_upload_dev);

	if (!d)
		return -ENODEV;
	return pvr_translator_bo_write(d, bo, off, src, bytes);
}

static int pvr_translator_upload_read(struct mt_bo *bo, u64 off,
				      void *dst, u64 bytes)
{
	struct mt_guest_device *d = READ_ONCE(pvr_translator_upload_dev);

	if (!d)
		return -ENODEV;
	return pvr_translator_bo_read(d, bo, off, dst, bytes);
}

/* Fill the TQX context's pool slices with a one-shot copy prepare (r261).
 * Non-fatal: DM bring-up stays up when slices fail; fire checks the flag.
 * translator_lock held; takes buffers->lock (trial_lock -> buffers.lock;
 * no reverse path exists, same as live_3d). Temporary stream Bos are put
 * after prepare; their VM bindings die with the space at teardown.
 */
static void pvr_translator_tqx_slices(struct mt_guest_device *d)
{
	static const struct mt_tqx_upload_ops upload = {
		.write = pvr_translator_upload_write,
		.read = pvr_translator_upload_read,
	};
	struct mt_tqx_work work = { 0 };
	struct mt_tqx_submission_workspace *workspace;
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
		pr_info("mt_pvr_bridge: tqx slices: topology %d cores=%u\n",
			ret, cores);
		return;
	}
	workspace = kvzalloc(sizeof(*workspace), GFP_KERNEL);
	if (!workspace) {
		pr_info("mt_pvr_bridge: tqx slices: workspace -ENOMEM\n");
		return;
	}
	bos[0] = &translator.tqx_cmd;
	bos[1] = &translator.tqx_tmp_src;
	bos[2] = &translator.tqx_tmp_dst;
	bos[3] = &translator.tqx_dma;
	bos[4] = &translator.tqx_state;
	WRITE_ONCE(pvr_translator_upload_dev, d);
	mutex_lock(d->shared_boot.buffers->lock);
	ret = mt_tqx_work_prepare_from_pools(&work, &d->shared_boot,
			workspace, &upload, &d->gem.profile, cores,
			&translator.tqx_context, bos, &input);
	mutex_unlock(d->shared_boot.buffers->lock);
	WRITE_ONCE(pvr_translator_upload_dev, NULL);
	if (!ret)
		ret = mt_tqx_work_cancel(&work);
	kvfree(workspace);
	if (!ret) {
		translator.tqx_slices_ready = true;
		pr_info("mt_pvr_bridge: tqx slices: ready cores=%u\n", cores);
	} else {
		pr_info("mt_pvr_bridge: tqx slices: prepare %d\n", ret);
	}
}

/* Submit one empty marker tagged with seq. translator_lock and trial_lock
 * held. Returns a held fence reference for the caller; the caller MUST drop
 * all session locks before waiting on it, because completion events are
 * drained under trial_lock (same discipline as live_3d_drm: unlock, then
 * wait). Waiting while holding trial_lock starves the drain and always
 * times out (r147).
 */
static int pvr_translator_submit_locked(struct mt_guest_device *d, u64 tag,
					 struct dma_fence **out)
{
	struct mt_execution_request req = {
		.command_va = MT_TRANSLATE_CMD_VA,
		.bytes = MT_GFX_LINUX_PACKET_BYTES,
		.type = 3,
		.submit_flags = 0,
	};
	struct dma_fence *fence = NULL;
	int ret;

	ret = pvr_translator_bo_write(d, &translator.command,
				      MT_TRANSLATE_TAG_OFFSET,
				      &tag, MT_TRANSLATE_TAG_BYTES);
	if (ret)
		return ret;
	d->markers.ready = true;
	d->markers.work_ready = true;
	ret = d->markers.ops->submit_context(&d->markers, &translator.context,
					     &translator.command, &req, &fence);
	d->markers.work_ready = false;
	d->markers.ready = false;
	if (ret)
		return ret;
	*out = fence;
	return 0;
}

/* Wait for a submitted translator fence. No session locks held. */
static int pvr_translator_wait_fence(struct dma_fence *fence)
{
	long waited = dma_fence_wait_timeout(fence, false,
					msecs_to_jiffies(MT_TRANSLATE_FENCE_WAIT_MS));
	int ret = waited > 0 ? dma_fence_get_status(fence) :
		(waited < 0 ? (int)waited : -ETIMEDOUT);

	return ret == 1 ? 0 : (ret ? ret : -EIO);
}

/* Translate one check-only kick (ncheck in 1..64, update == 0) into a real
 * empty-marker submission. file->lock held. Returns 0 with OUT written, or a
 * negative errno; never silently falls back to accept. */
static int pvr_translate_kick(struct mt_pvr_file *file,
			      struct mt_pvr_cmd *cmd,
			      const struct mt_pvr_kicksync3_in *in)
{
	u32 ncheck = in->client_check_count;
	u32 nupdate = in->client_update_count;
	u32 *offs = NULL, *vals = NULL;
	u64 *ufos = NULL;
	struct mt_pvr_ufo_cond *conds = NULL;
	u32 *uoffs = NULL, *uvals = NULL;
	u64 *uufos = NULL;
	struct mt_pvr_ufo_cond *uconds = NULL;
	struct mt_guest_device *d;
	struct mt_pvr_kicksync3_out out3 = { 0 };
	struct dma_fence *fence = NULL;
	struct sync_file *sync_file = NULL;
	int fd = -1, ret;
	u32 i;
	u64 tag;
	u64 fence_seqno;

	if (ncheck > MT_PVR_KICK_SYNC_MAX ||
	    nupdate > MT_PVR_KICK_SYNC_MAX)
		return -EOPNOTSUPP;
	offs = kcalloc(ncheck ? ncheck : 1, sizeof(*offs), GFP_KERNEL);
	vals = kcalloc(ncheck ? ncheck : 1, sizeof(*vals), GFP_KERNEL);
	ufos = kcalloc(ncheck ? ncheck : 1, sizeof(*ufos), GFP_KERNEL);
	conds = kcalloc(ncheck ? ncheck : 1, sizeof(*conds), GFP_KERNEL);
	uoffs = kcalloc(nupdate ? nupdate : 1, sizeof(*uoffs), GFP_KERNEL);
	uvals = kcalloc(nupdate ? nupdate : 1, sizeof(*uvals), GFP_KERNEL);
	uufos = kcalloc(nupdate ? nupdate : 1, sizeof(*uufos), GFP_KERNEL);
	uconds = kcalloc(nupdate ? nupdate : 1, sizeof(*uconds), GFP_KERNEL);
	if ((ncheck && (!offs || !vals || !ufos || !conds)) ||
	    (nupdate && (!uoffs || !uvals || !uufos || !uconds))) {
		ret = -ENOMEM;
		goto free;
	}
	if ((ncheck &&
	     (copy_from_user(offs, u64_to_user_ptr(in->check_devvar_offset),
			     (size_t)ncheck * sizeof(*offs)) ||
	      copy_from_user(vals, u64_to_user_ptr(in->check_value),
			     (size_t)ncheck * sizeof(*vals)) ||
	      copy_from_user(ufos, u64_to_user_ptr(in->check_ufo_block),
			     (size_t)ncheck * sizeof(*ufos)))) ||
	    (nupdate &&
	     (copy_from_user(uoffs, u64_to_user_ptr(in->update_devvar_offset),
			     (size_t)nupdate * sizeof(*uoffs)) ||
	      copy_from_user(uvals, u64_to_user_ptr(in->update_value),
			     (size_t)nupdate * sizeof(*uvals)) ||
	      copy_from_user(uufos, u64_to_user_ptr(in->update_ufo_block),
			     (size_t)nupdate * sizeof(*uufos))))) {
		ret = -EFAULT;
		goto free;
	}
	for (i = 0; i < ncheck; i++) {
		ret = pvr_translator_resolve(file, ufos[i], offs[i], &conds[i]);
		if (ret)
			goto free;
		conds[i].expected = vals[i];
	}
	for (i = 0; i < nupdate; i++) {
		ret = pvr_translator_resolve(file, uufos[i], uoffs[i],
					     &uconds[i]);
		if (ret)
			goto free;
		/* The update value is applied below, after the marker
		 * completes; record it in the condition slot now. */
		uconds[i].expected = uvals[i];
	}
	if (ncheck)
		ret = pvr_translator_wait(conds, ncheck);
	if (ret)
		goto free;
	mutex_lock(&translator_lock);
	ret = pvr_translator_prepare_locked();
	if (ret) {
		mutex_unlock(&translator_lock);
		goto free;
	}
	d = translator.dev;
	mutex_lock(&d->state.trial_lock);
	tag = ++translator.seq;
	ret = pvr_translator_submit_locked(d, tag, &fence);
	mutex_unlock(&d->state.trial_lock);
	mutex_unlock(&translator_lock);
	if (ret)
		goto free;
	/* Session locks are dropped: completion events drain under trial_lock. */
	ret = pvr_translator_wait_fence(fence);
	if (ret) {
		dma_fence_put(fence);
		goto free;
	}
	/* The kick completed: publish update values so later waiters observe
	 * them. file->lock is still held by dispatch; plain CPU writes. */
	for (i = 0; i < nupdate; i++)
		memcpy((u8 *)uconds[i].host + uconds[i].offset,
		       &uconds[i].expected, sizeof(u32));
	sync_file = sync_file_create(fence);
	if (!sync_file) {
		dma_fence_put(fence);
		ret = -ENOMEM;
		goto free;
	}
	/* The sync_file holds its own fence reference from here on; drop the
	 * submit caller reference (r147: leaking it pins one probe ref and
	 * the whole marker per translated kick). */
	fence_seqno = fence->seqno;
	dma_fence_put(fence);
	fence = NULL;
	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		fput(sync_file->file);
		ret = fd;
		goto free;
	}
	fd_install(fd, sync_file->file);
	out3.error = 0;
	out3.update_fence_fd = fd;
	ret = pvr_out(cmd, &out3, sizeof(out3));
	if (ret)
		close_fd(fd);
	else
		pr_info("mt_pvr_bridge: translated kick: check=%u update=%u tag=%llu fence=%llu\n",
			ncheck, nupdate, tag, fence_seqno);
free:
	kfree(offs);
	kfree(vals);
	kfree(ufos);
	kfree(conds);
	kfree(uoffs);
	kfree(uvals);
	kfree(uufos);
	kfree(uconds);
	return ret;
}

/* Best-effort translator teardown at module exit. The bridge-rmmod-first
 * discipline guarantees the probe session still exists; anything else leaks
 * the translator objects with a warning instead of touching dead stores. */
static void pvr_translator_exit(void)
{
	struct module *owner = NULL;
	struct mt_guest *g;

	mutex_lock(&translator_lock);
	if (!translator.ready) {
		mutex_unlock(&translator_lock);
		return;
	}
	g = pvr_session_acquire(&owner);
	if (!g || g != translator.guest) {
		pr_warn("mt_pvr_bridge: translator teardown without live session; objects retained\n");
		if (owner)
			module_put(owner);
		mutex_unlock(&translator_lock);
		return;
	}
	mutex_lock(&g->trial_lock);
	pvr_translator_teardown_locked();
	mutex_unlock(&g->trial_lock);
	module_put(owner);
	mutex_unlock(&translator_lock);
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

	if (function == MT_PVR_FN_RGXKICKSYNC2) {
		ret = pvr_in(cmd, &in2, sizeof(in2));
		if (ret)
			return ret;
		handle = in2.kicksync_context;
	} else if (function == MT_PVR_FN_RGXSETKICKSYNCCONTEXTPROPERTY) {
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
	obj = pvr_object_find(file, handle, MT_PVR_KIND_KICKSYNC);
	if (!obj)
		return -ENOENT;
	/* A property query has no fence to complete. */
	if (function == MT_PVR_FN_RGXSETKICKSYNCCONTEXTPROPERTY)
		return pvr_out(cmd, &out_prop, sizeof(out_prop));
	if (function == MT_PVR_FN_RGXKICKSYNC3) {
		if (translate_kick) {
			if (in3.client_check_count || in3.client_update_count)
				return pvr_translate_kick(file, cmd, &in3);
		}
		pvr_kick_inspect(file, &in3);
	}
	/* A fence that is already complete: poll/select on it returns at once.
	 * Bridge-stage completion only -- the GPU did nothing, because there is
	 * no channel by which this bridge could ask it to.
	 */
	fence_fd = anon_inode_getfd("pvr-fence", &pvr_fence_fops, NULL,
				    O_RDWR | O_CLOEXEC);
	if (fence_fd < 0)
		return fence_fd;
	if (function == MT_PVR_FN_RGXKICKSYNC2) {
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
	obj = pvr_object_find(file, in.zs_buffer, MT_PVR_KIND_ZSBUFFER);
	if (!obj)
		return -ENOENT;
	list_del(&obj->link);
	kfree(obj);
	return pvr_out(cmd, &out, sizeof(out));
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
	obj = pvr_object_find(file, in.compute_context, MT_PVR_KIND_COMPUTE);
	if (!obj)
		return -ENOENT;
	list_del(&obj->link);
	kfree(obj);
	return pvr_out(cmd, &out, sizeof(out));
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

/* 0x88:0x5 BridgeRGXCreateKickSyncContext2 (DDK2 CCB create, r141/r143).
 * 8-byte IN, 12-byte OUT {handle, error}. Same object model as legacy
 * 0x88:0x0: mint a KIND_KICKSYNC object; the IN payload stays opaque.
 *
 * Called with file->lock already held by pvr_bridge_dispatch(), so it must not
 * take it again.
 */
static int pvr_cmd_kicksyncctx2_create(struct mt_pvr_file *file,
				       struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_kicksyncctx2_create_in in;
	struct mt_pvr_kicksyncctx2_create_out out = { 0 };
	struct mt_pvr_object *obj;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	obj = pvr_object_new(file, MT_PVR_KIND_KICKSYNC);
	if (!obj)
		return -ENOMEM;
	out.kicksync_context = obj->handle;
	(void)in;
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
	obj = pvr_object_find(file, in.kicksync_context, MT_PVR_KIND_KICKSYNC);
	if (!obj)
		return -ENOENT;
	list_del(&obj->link);
	kfree(obj);
	return pvr_out(cmd, &out, sizeof(out));
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
	if (pmr->refcount == U32_MAX)
		return -EOVERFLOW;
	pmr->refcount++;
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
	/* res is still valid: file->lock never drops across this path and
	 * nothing above mutates the object list, so no second lookup. */
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
	return pvr_object_find(file, handle, MT_PVR_KIND_RESERVATION);
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
	pmr = pvr_pmr_new(file, MT_PVR_SYNC_BLOCK_BYTES, 12);
	if (!pmr) {
		list_del(&obj->link);
		kfree(obj);
		return -ENOMEM;
	}
	obj->arg0 = pmr->handle;
	out.sync_handle = obj->handle;
	out.sync_pmr = pmr->handle;
	out.block_size = MT_PVR_SYNC_BLOCK_BYTES;
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

/* 0x82:0x12 BridgeRGXCreateRenderContext2 (DDK2 render create, r141).
 * 12-byte IN, 12-byte OUT {handle, error}. Same object model as legacy
 * 0x82:0x8: mint a KIND_CONTEXT object; the IN payload stays opaque to the
 * bridge (legacy ignores its own IN the same way).
 *
 * Called with file->lock already held by pvr_bridge_dispatch(), so it must not
 * take it again.
 */
/* r389: forward decl (defined after mt_bridge_ta_vm, needs full struct). */
static int mt_render_context_create(struct mt_pvr_file *file,
				    struct mt_pvr_render_context *ctx);

static int pvr_cmd_render2_create(struct mt_pvr_file *file,
				  struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_render2_create_in in;
	struct mt_pvr_render2_create_out out = { 0 };
	struct mt_pvr_object *obj;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	obj = pvr_object_new(file, MT_PVR_KIND_CONTEXT);
	if (!obj)
		return -ENOMEM;
	/* r389 R6-2: Allocate per-context real state (Route A). */
	obj->render_ctx = kzalloc(sizeof(*obj->render_ctx), GFP_KERNEL);
	if (!obj->render_ctx) {
		list_del(&obj->link);
		kfree(obj);
		return -ENOMEM;
	}
	/* IN fields (priv_data, priority) preserved for future use. */
	(void)in;
	ret = mt_render_context_create(file, obj->render_ctx);
	if (ret) {
		pr_err("mt_pvr_bridge: r389: render context create failed: %d\n",
		       ret);
		kfree(obj->render_ctx);
		obj->render_ctx = NULL;
		list_del(&obj->link);
		kfree(obj);
		return ret;
	}
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
	obj = pvr_object_find(file, in.devmem_heap, kind);
	if (!obj)
		return -ENOENT;
	list_del(&obj->link);
	/* r390 R6-3: Tear down real per-context state in reverse order.
	 * Legacy token objects have render_ctx == NULL and skip this. */
	if (kind == MT_PVR_KIND_CONTEXT && obj->render_ctx) {
		mt_render_context_destroy(obj->render_ctx);
		kfree(obj->render_ctx);
		obj->render_ctx = NULL;
	}
	kfree(obj);
	return pvr_out(cmd, &out, sizeof(out));
}

/* Drop one PMR reference and unlink it once that was the final reference.
 * UMD local imports share the PMR handle and each unrefs it, so keep it
 * discoverable until all imported and mmap references have gone away.
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
	if (pmr->refcount == 1)
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

	pmr = pvr_pmr_new(file, MT_PVR_HWPERF_PMR_BYTES, 12);
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
 * TQPMR_MapMem / TQPMR_MapUSCMem. Live r150 evidence shows the UMD imports,
 * unrefs, and releases these as distinct CLI and USC PMRs, so they need
 * separate handles and lifetimes. eError rides last ({u64, u64, u32}).
 */
static int pvr_cmd_tdm_shmem(struct mt_pvr_file *file,
			     struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_tdm_shmem_out out = { 0 };
	struct mt_pvr_pmr *cli_pmr, *usc_pmr;

	cli_pmr = pvr_pmr_new(file, MT_PVR_TDM_SHMEM_BYTES, 12);
	if (!cli_pmr)
		return -ENOMEM;
	usc_pmr = pvr_pmr_new(file, MT_PVR_TDM_SHMEM_BYTES, 12);
	if (!usc_pmr) {
		pvr_pmr_put(file, cli_pmr->handle);
		return -ENOMEM;
	}
	out.ptr1 = cli_pmr->handle;
	out.ptr2 = usc_pmr->handle;
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

/* 0x89:0x8/0x9 DDK2 transfer-context lifecycle (r150).
 * The handle is a per-file bookkeeping token only. This does not allocate a
 * firmware context or submit work; SubmitTransfer3 (0x89:0xa) is accept-and-log
 * only (pvr_cmd_tdm_submit3_observe, r174): it reports the named CCB window
 * and returns 0 without executing anything. */
static int pvr_cmd_tdm_context2_create(struct mt_pvr_file *file,
				      struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_tdm_context2_create_in in;
	struct mt_pvr_tdm_context2_create_out out = { 0 };
	struct mt_pvr_object *obj;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	obj = pvr_object_new(file, MT_PVR_KIND_TDM_CONTEXT);
	if (!obj)
		return -ENOMEM;
	obj->arg0 = in.device_mem_context;
	obj->arg1 = in.context_type;
	out.transfer_context = obj->handle;
	return pvr_out(cmd, &out, sizeof(out));
}

static int pvr_cmd_tdm_context2_destroy(struct mt_pvr_file *file,
					struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_tdm_context2_destroy_in in;
	struct mt_pvr_tdm_context2_destroy_out out = { 0 };
	struct mt_pvr_object *obj;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	obj = pvr_object_find(file, in.transfer_context, MT_PVR_KIND_TDM_CONTEXT);
	if (!obj)
		return -ENOENT;
	list_del(&obj->link);
	kfree(obj);
	return pvr_out(cmd, &out, sizeof(out));
}

/* SubmitTransfer3 update writeback (r297): apply the UMD's own update
 * values into their sync PMRs at observe time (instant completion, no
 * execution). Mirrors the translator kick writeback (resolve verbatim
 * byte offsets via pvr_translator_resolve, then memcpy) and the 0x2:0xa
 * true-write: file->lock held, plain CPU writes, loud failures (a
 * half-written update set would hang the UMD waiter with a success
 * return, so resolve ALL before writing ANY). Offsets follow the only
 * live-proven convention (translator kick path, r212/r227: verbatim
 * bytes); the live window arbitrates, dword-scaling is the documented
 * fallback. Gated by translate_submit3_bump (default off).
 */
#define MT_PVR_SUBMIT3_UPDATE_MAX 32U

static int pvr_submit3_bump_updates(struct mt_pvr_file *file,
				    const struct mt_pvr_tdm_submit3_in *in)
{
	u32 n = in->update_count;
	u32 *handles = NULL, *offsets = NULL, *values = NULL;
	struct mt_pvr_ufo_cond *conds = NULL;
	u32 i;
	int ret = 0;

	if (!n)
		return 0;
	if (n > MT_PVR_SUBMIT3_UPDATE_MAX)
		return -E2BIG;
	handles = kcalloc(n, sizeof(*handles), GFP_KERNEL);
	offsets = kcalloc(n, sizeof(*offsets), GFP_KERNEL);
	values = kcalloc(n, sizeof(*values), GFP_KERNEL);
	conds = kcalloc(n, sizeof(*conds), GFP_KERNEL);
	if (!handles || !offsets || !values || !conds) {
		ret = -ENOMEM;
		goto free;
	}
	if (copy_from_user(handles, u64_to_user_ptr(in->update_handles),
			   (size_t)n * sizeof(*handles)) ||
	    copy_from_user(offsets, u64_to_user_ptr(in->update_offsets),
			   (size_t)n * sizeof(*offsets)) ||
	    copy_from_user(values, u64_to_user_ptr(in->update_values),
			   (size_t)n * sizeof(*values))) {
		ret = -EFAULT;
		goto free;
	}
	for (i = 0; i < n; i++) {
		if (!handles[i])
			continue;
		ret = pvr_translator_resolve(file, handles[i], offsets[i],
					     &conds[i]);
		if (ret) {
			pr_info("mt_pvr_bridge: submit3 bump: entry %u sync=%#x off=%u: %d\n",
				i, handles[i], offsets[i], ret);
			goto free;
		}
	}
	for (i = 0; i < n; i++) {
		if (!handles[i])
			continue;
		memcpy((u8 *)conds[i].host + conds[i].offset,
		       &values[i], sizeof(u32));
	}
	pr_info("mt_pvr_bridge: submit3 bump: update=%u first_sync=%#x first_off=%u first_val=%u\n",
		n, handles[0], offsets[0], values[0]);
free:
	kfree(handles);
	kfree(offsets);
	kfree(values);
	kfree(conds);
	return ret;
}

/* CCB VA-reference census (r306): the real CCB carries no TQX
 * destination block, but it may reference surfaces by VA. Collect
 * distinct u64 values in GPU-VA range and map each against pool
 * bindings; the log names every surface the CCB touches (self-refs
 * included, which calibrates the mechanism).
 */
#define MT_PVR_CCBREF_MAX 16U

static void pvr_ccb_va_census(struct mt_pvr_file *file, const u8 *win,
			      u32 bytes)
{
	u64 seen[MT_PVR_CCBREF_MAX];
	u32 nseen = 0, i;
	struct mt_pvr_binding *b;

	for (i = 0; i + 8 <= bytes; i += 4) {
		u64 v, k;
		bool dup = false;

		memcpy(&v, win + i, sizeof(v));
		if (v < MT_TQX_CMD_VA || v >= (1ULL << MT_GPU_VA_BITS))
			continue;
		for (k = 0; k < nseen; k++) {
			if (seen[k] == v) {
				dup = true;
				break;
			}
		}
		if (dup || nseen >= MT_PVR_CCBREF_MAX)
			continue;
		seen[nseen++] = v;
		list_for_each_entry(b, &file->bindings, link) {
			struct mt_pvr_object *res =
				pvr_reservation_find(file, b->reservation);

			if (!res || res->arg0 > v ||
			    v - res->arg0 > res->arg1)
				continue;
			pr_info("mt_pvr_bridge: submit3 ccbref: va=%#llx res=%#llx pmr=%#llx\n",
				(unsigned long long)v,
				(unsigned long long)b->reservation,
				(unsigned long long)b->pmr);
			break;
		}
	}
	pr_info("mt_pvr_bridge: submit3 ccbref: total=%u\n", nseen);
}

/* CCB magic census (r305): the real UMD CCB carries no anchored
 * 4B-destination block (ccbdst: none), so report where the known TQX
 * command magics actually sit. One line per magic, first four hit
 * offsets; the real layout is then decidable from dmesg alone.
 */
static void pvr_ccb_magic_census(const u8 *win, u32 bytes)
{
	static const u32 magics[] = { 0x40000005U, 0x2da100U, 0xb8000000U,
				      0x08000001U, 0x25U, 0x2dU };
	u32 m, i;

	for (m = 0; m < sizeof(magics) / sizeof(magics[0]); m++) {
		u32 hits[4] = { 0 };
		u32 total = 0;

		for (i = 0; i + 4 <= bytes; i += 4) {
			u32 w;

			memcpy(&w, win + i, sizeof(w));
			if (w != magics[m])
				continue;
			if (total < 4)
				hits[total] = i;
			total++;
		}
		pr_info("mt_pvr_bridge: submit3 ccbmagic: w=%#x hits=%u @%u,%u,%u,%u\n",
			magics[m], total, hits[0], hits[1], hits[2],
			hits[3]);
	}
}

/* 0x89:0xa RGXTDMSubmitTransfer3 accept-and-log (r174).
 *
 * Accepts the submission (OUT error 0) and reports the CCB window it names,
 * without executing anything: no firmware channel, no page-table upload, no
 * fence, and no nested-pointer reads (check/update/PMR-sync arrays stay
 * untouched). Lets the real UMD walk past SubmitTransfer3 so its generated
 * CCB bytes become observable; the window digest below is the observation.
 * Anything outside a mapped reservation answers -EINVAL with no logging.
 *
 * Called with file->lock already held by pvr_bridge_dispatch(), so it must
 * not take it again.
 */
#define MT_PVR_SUBMIT3_LOG_MAX (1U << 20)

static int pvr_cmd_tdm_submit3_observe(struct mt_pvr_file *file,
				       struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_tdm_submit3_in in;
	struct mt_pvr_tdm_submit3_out out = { 0 };
	struct mt_pvr_object *obj;
	struct mt_pvr_binding *binding = NULL, *b;
	struct mt_pvr_pmr *pmr = NULL;
	u64 end, off, i, nonzero = 0, first = 0;
	u64 hash = 1469598103934665603ULL;
	u64 ccbdst_pmr = 0;
	u8 head[64];
	u32 head_len = 0;
	bool have_ctx = false;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	list_for_each_entry(obj, &file->objects, link) {
		if (obj->handle == in.transfer_context &&
		    obj->kind == MT_PVR_KIND_TDM_CONTEXT) {
			have_ctx = true;
			break;
		}
	}
	if (!have_ctx)
		return -ENOENT;
	if (!in.ccb_bytes || in.ccb_bytes > MT_PVR_SUBMIT3_LOG_MAX ||
	    in.ccb_data + in.ccb_bytes < in.ccb_data)
		return -EINVAL;
	end = in.ccb_data + in.ccb_bytes;
	list_for_each_entry(b, &file->bindings, link) {
		struct mt_pvr_object *res =
			pvr_reservation_find(file, b->reservation);

		if (!res || res->arg0 > in.ccb_data)
			continue;
		if (in.ccb_data - res->arg0 > res->arg1)
			continue;
		if (end - res->arg0 > res->arg1)
			continue;
		binding = b;
		break;
	}
	if (!binding)
		return -EINVAL;
	pmr = pvr_pmr_find(file, binding->pmr);
	if (!pmr || !pmr->host || !pmr->bytes)
		return -EINVAL;
	if (binding->va > in.ccb_data)
		return -EINVAL;
	off = in.ccb_data - binding->va;
	if (off > pmr->bytes || in.ccb_bytes > pmr->bytes - off)
		return -EINVAL;
	for (i = 0; i < in.ccb_bytes; i++) {
		u8 byte = ((u8 *)pmr->host)[off + i];

		hash ^= byte;
		hash *= 1099511628211ULL;
		if (!byte)
			continue;
		if (!nonzero)
			first = i;
		nonzero++;
		if (head_len < sizeof(head))
			head[head_len++] = byte;
	}
	pr_info("mt_pvr_bridge: submit3 observe: va=%#llx bytes=%u res=%#llx pmr=%#llx nonzero=%llu first=%#llx fnv=%#llx head=%*ph\n",
		(unsigned long long)in.ccb_data, in.ccb_bytes,
		(unsigned long long)binding->reservation,
		(unsigned long long)binding->pmr,
		(unsigned long long)nonzero, (unsigned long long)first,
		(unsigned long long)hash, head_len, head);
	pvr_ccb_magic_census((const u8 *)pmr->host + off, in.ccb_bytes);
	pvr_ccb_va_census(file, (const u8 *)pmr->host + off, in.ccb_bytes);

/* CCB destination scan (r304): the UMD-named destination VA,
	 * matched against pool bindings. Deterministic attribution for
	 * fire; the best-heuristic below stays for dry-run comparability.
	 */
	{
		u64 dst_va = 0;
		struct mt_pvr_binding *db;
		u64 dst_pmr = 0;

		if (!mt_ccb_find_dst_va((const u8 *)pmr->host + off,
					in.ccb_bytes, &dst_va)) {
			list_for_each_entry(db, &file->bindings, link) {
				struct mt_pvr_object *res =
					pvr_reservation_find(file,
							     db->reservation);

				if (!res || res->arg0 > dst_va)
					continue;
				if (dst_va - res->arg0 > res->arg1)
					continue;
				dst_pmr = db->pmr;
				break;
			}
		}
		if (dst_pmr)
			pr_info("mt_pvr_bridge: submit3 ccbdst: va=%#llx pmr=%#llx\n",
				(unsigned long long)dst_va,
				(unsigned long long)dst_pmr);
		else
			pr_info("mt_pvr_bridge: submit3 ccbdst: none\n");
		ccbdst_pmr = dst_pmr;
	}
	if (translate_transfer) {
		ret = pvr_submit3_transfer_dry_run(file, &in, binding->pmr);
		if (ret) {
			pr_info("mt_pvr_bridge: submit3 dry-run refused: %d\n",
				ret);
			return ret;
		}
	}
	if (translate_tqx_ctx) {
		mutex_lock(&translator_lock);
		if (!translator.ready) {
			ret = pvr_translator_prepare_locked();
			if (ret) {
				mutex_unlock(&translator_lock);
				pr_info("mt_pvr_bridge: submit3 tqx-ctx: prepare failed: %d\n",
					ret);
				return ret;
			}
		}
		if (!translator.tqx_ready) {
			mutex_unlock(&translator_lock);
			pr_info("mt_pvr_bridge: submit3 tqx-ctx: sealedmiss\n");
			return -EOPNOTSUPP;
		}
		pr_info("mt_pvr_bridge: submit3 tqx-ctx: ready\n");
		mutex_unlock(&translator_lock);
	}
	if (translate_tqx_fire) {
		struct mt_guest_device *d;
		struct module *owner = NULL;
		struct mt_guest *g;

		mutex_lock(&translator_lock);
		if (!translator.ready || !translator.tqx_ready) {
			mutex_unlock(&translator_lock);
			pr_info("mt_pvr_bridge: submit3 fire: not ready\n");
			return -EOPNOTSUPP;
		}
		g = pvr_session_acquire(&owner);
		if (!g || g != translator.guest) {
			if (owner)
				module_put(owner);
			mutex_unlock(&translator_lock);
			return -ENODEV;
		}
		d = container_of(g, struct mt_guest_device, state);
		ret = pvr_submit3_transfer_fire(file, d, binding->pmr,
						ccbdst_pmr);
		module_put(owner);
		mutex_unlock(&translator_lock);
		if (ret) {
			pr_info("mt_pvr_bridge: submit3 fire refused: %d\n",
				ret);
			return ret;
		}
	}
	if (translate_submit3_bump) {
		/* Order matters (r300): the UMD proceeds as soon as its
		 * syncs are bumped, so the bump must wait for a scheduled
		 * fire to finish landing pixels first. Bounded (60s),
		 * interruptible slices so a stuck fire cannot wedge the
		 * ioctl; translator_lock is NOT held here (only file->lock,
		 * like pvr_translator_wait).
		 */
		if (READ_ONCE(translator.fire_running)) {
			unsigned long deadline =
				jiffies + msecs_to_jiffies(60000);

			for (;;) {
				if (!READ_ONCE(translator.fire_running))
					break;
				if (time_after_eq(jiffies, deadline)) {
					pr_info("mt_pvr_bridge: submit3 bump: fire wait timeout\n");
					return -ETIMEDOUT;
				}
				if (msleep_interruptible(
						MT_TRANSLATE_WAIT_SLICE_MS))
					return -ERESTARTSYS;
				if (signal_pending(current))
					return -ERESTARTSYS;
			}
			ret = READ_ONCE(translator.fire_result);
			if (ret) {
				pr_info("mt_pvr_bridge: submit3 bump: fire failed: %d\n",
					ret);
				return ret;
			}
		}
		ret = pvr_submit3_bump_updates(file, &in);
		if (ret) {
			pr_info("mt_pvr_bridge: submit3 bump refused: %d\n",
				ret);
			return ret;
		}
	}
	return pvr_out(cmd, &out, sizeof(out));
}

/* 0x82:0xC MUSAKickGFX2 (r356): observer placeholder, explicitly NOT
 * executing. Unlike the 0x82:0x14 accept-and-log observer above (r215),
 * this handler returns -ENOTTY after recording the decoded header fields:
 * MUSAKICKGFX2 is an S4 real TA/3D/PR submission, and returning 0 would
 * falsely tell the UMD the kick succeeded. STATUS.md forbids substituting
 * execution with accept-and-log; this placeholder only observes so a live
 * run can capture the wire image (r358), and execution stays unimplemented
 * until the DDK2 render backend exists (r359/r360). No userspace pointer
 * is dereferenced here -- only scalar header fields are logged.
 */
/* ---- r367: 0x82:0xC real TA dispatch (R2b) ---- */
/* ---- r370: production TA completion path (R2b) ----
 * The frozen probe's mt_runtime_event() routes DM3 events to the generic
 * mt_marker_complete(), which rejects the TA completion code 0x100
 * (MT_FW_TA_COMPLETE_CODE) via mt_fw_event_matches(). Without a production
 * consumer, TA markers hang forever (r368: wire 6 was pending ~940s with
 * zero completion events; the marker framework has no timeout).
 *
 * The bridge therefore polls for the 0x100 completion after submitting a
 * TA marker and retires it via mt_marker_complete_ta() -- the TA-aware
 * path (r366). Pattern proven by the r366 verifier's ta_wait_complete().
 *
 * Locking: caller must hold g->trial_lock (== s->lock). Holding it blocks
 * the probe's poller for the poll duration; the firmware produces the
 * completion event independently, so this cannot deadlock. The consumed
 * event is left in the ring: once s->count[DM3]==0, the probe's drain
 * stages it harmlessly in t->events (no queue poisoning, r368).
 */
#define PVR_TA_COMPLETE_TIMEOUT_MS 2000

static int pvr_ta_wait_complete(struct mt_guest *g, struct mt_marker_store *s,
				u32 wire_id)
{
	struct mt_fw_queue_io *q = s->queue;
	const u32 base = MT_FW_DM_TA * MT_FW_DM_BYTES;
	const u32 cursor = base + MT_FW_CURSOR_OFFSET + 32;
	unsigned long deadline =
		jiffies + msecs_to_jiffies(PVR_TA_COMPLETE_TIMEOUT_MS);
	u32 head, tail, i;

	lockdep_assert_held(&g->trial_lock);
	while (time_before(jiffies, deadline)) {
		head = mt_fw_event_io_ops.read32(q, cursor);
		tail = mt_fw_event_io_ops.read32(q, cursor + 8);
		if (head >= 64 || tail >= 64)
			return -EIO;
		for (i = 0; i < 64; i++) {
			u32 idx = (tail + i) & 63;
			struct mt_fw_event ev;
			int rc;

			if (idx == head)
				break;
			mt_fw_event_io_ops.copy_from(q, &ev,
				base + MT_FW_EVENT_OFFSET +
					idx * MT_FW_EVENT_BYTES,
				sizeof(ev));
			if (ev.words[1] == MT_FW_TA_COMPLETE_CODE &&
			    ev.words[2] == wire_id) {
				rc = mt_marker_complete_ta(s, MT_FW_DM_TA,
							   &ev);
				return rc ? rc : 0;
			}
		}
		usleep_range(100, 200);
	}
	return -ETIMEDOUT;
}

/* Drop a TA marker whose completion never arrived. Signals its fence with
 * an error so check_fence waiters do not hang forever (r368: wire 6).
 * Caller must hold g->trial_lock.
 */
static void pvr_ta_abandon(struct mt_guest *g, struct mt_marker_store *s,
			   u32 wire_id)
{
	struct mt_marker_fence *m, *tmp;

	lockdep_assert_held(&g->trial_lock);
	list_for_each_entry_safe(m, tmp, &s->pending[MT_FW_DM_TA], link) {
		if (m->wire_id != wire_id)
			continue;
		list_del(&m->link);
		s->count[MT_FW_DM_TA]--;
		s->total--;
		dma_fence_set_error(&m->fence, -ETIMEDOUT);
		dma_fence_signal(&m->fence);
		dma_fence_put(&m->fence);
		pr_warn("mt_pvr_bridge: TA wire=%u completion timeout, abandoned\n",
			wire_id);
		return;
	}
	pr_warn("mt_pvr_bridge: TA wire=%u not found for abandon\n", wire_id);
}
/* MUSAKickGFX2 real dispatch: decode IN (keeping the r356 decode log),
 * map to TA submit params (D5), and submit via the bridge's exported
 * submit_ta_work op. Replaces the r356 observer's -ENOTTY.
 *
 * D5: kick_ta=1 dispatches to DM3; kick_pr=1 stays TO-VALIDATE -- the op
 * honestly returns -EOPNOTSUPP and the occurrence is logged, never faked;
 * kick_3d has no execution path this round (-EOPNOTSUPP).
 * D8: userspace pointers are captured at decode time (pvr_in); the submit
 * path only sees kernel-side copies.
 *
 * Locking: runs under file->lock like the rest of dispatch. trial_lock is
 * taken only around the store checks; it is released before the op call
 * (the op manages s->lock internally and the caller must not hold it).
 * Never touches mt_guest_probe (session acquired read-only via
 * pvr_session_acquire). */
struct mt_bridge_ta_vm {
	struct mt_gpu_vm vm;
	struct mt_bo tables;	/* Synthetic; page_pa==NULL per init. */
	void *pt_pages;
	void *image;
	void *scratch;
};

/* r389: Per-context VM with d->buffers-backed page tables (R6 Route A).
 * The page-table BO is allocated from d->buffers so that mt_gpu_vm_bind_many()
 * accepts real BOs (store+ops must match: bo->store == vm->tables->store).
 * The caller must keep pt_bo alive (stored in ctx) until VM destroy.
 * (r398: the R5 per-file VM helper was removed; this is the only VM creator.)
 */
/* 64KB page tables: comfortably holds 11 BO ranges + headroom (r389 V1). */
#define MT_RENDER_CTX_PT_BYTES (64U * 1024U)

static struct mt_bridge_ta_vm *mt_render_context_vm_create(struct mt_guest_device *d,
							   struct mt_bo *pt_bo)
{
	struct mt_bridge_ta_vm *tvm;
	void *image, *scratch;
	int ret;

	tvm = kzalloc(sizeof(*tvm), GFP_KERNEL);
	if (!tvm)
		return NULL;

	/* Page-table BO from d->buffers (matches the 11 BOs' store/ops). */
	ret = mt_bo_create(pt_bo, d->buffers.ops, &d->buffers,
			   MT_RENDER_CTX_PT_BYTES, PAGE_SIZE);
	if (ret)
		goto fail_tvm;

	image = kvzalloc(MT_RENDER_CTX_PT_BYTES, GFP_KERNEL);
	if (!image)
		goto fail_pt;
	scratch = kvzalloc(MT_RENDER_CTX_PT_BYTES, GFP_KERNEL);
	if (!scratch)
		goto fail_image;

	/* Reuse the tvm struct layout: pt_pages/image/scratch/tables/vm.
	 * We store pt_bo separately (caller-owned); tvm->tables is unused. */
	tvm->image = image;
	tvm->scratch = scratch;
	/* Stash pt_bo pointer in pt_pages field (void *). */
	tvm->pt_pages = (void *)pt_bo;

	ret = mt_gpu_vm_init(&tvm->vm, pt_bo, image, scratch,
			     MT_RENDER_CTX_PT_BYTES);
	if (ret)
		goto fail_scratch;

	/* VM holds a reference via mt_bo_get; caller keeps pt_bo for cleanup. */
	return tvm;

fail_scratch:
	kvfree(scratch);
fail_image:
	kvfree(image);
fail_pt:
	mt_bo_put(pt_bo);
fail_tvm:
	kfree(tvm);
	return NULL;
}

static void mt_render_context_vm_destroy(struct mt_bridge_ta_vm *tvm)
{
	struct mt_bo *pt_bo;
	int ret;
	if (!tvm)
		return;
	pt_bo = (struct mt_bo *)tvm->pt_pages;
	ret = mt_gpu_vm_fini(&tvm->vm);
	/* r390: fini must succeed here (exec destroyed first, owners==0).
	 * A failure means a refcount bug; flag it loudly. */
	if (WARN_ON(ret))
		pr_warn("mt_pvr_bridge: r390: per-context VM fini failed: %d\n",
			  ret);
	kvfree(tvm->scratch);
	kvfree(tvm->image);
	/* Release the caller's pt_bo reference (VM's ref already dropped by fini). */
	if (pt_bo)
		mt_bo_put(pt_bo);
	kfree(tvm);
}

/* r390: R6-3 Destroy realization (Route A per-context).
 * Tears down a render context in strict reverse order of creation:
 *   exec context -> exec process -> 11 BOs (put; VM fini drops its refs)
 *   -> per-context VM destroy (fini + pt_bo put).
 * Safe on partially-initialized ctx (resources_ready=false): the
 * exec_ready/bos_ready/vm-NULL guards skip whatever was never built.
 * All teardown state is cleared, so a second call is a no-op.
 * Caller (pvr_cmd_handle_release) holds file->lock; this takes no locks.
 */
static void mt_render_context_destroy(struct mt_pvr_render_context *ctx)
{
	u32 i;

	if (!ctx)
		return;

	/* 1. Exec context, then process. Process destroy requires
	 * contexts==0 and decrements vm->owners, so it must precede
	 * VM fini (which returns -EBUSY while owners>0). */
	/* r397 Phase 1: destroy TA context first (order vs 3D is irrelevant;
	 * both only touch process.contexts). */
	if (ctx->exec_ta_ready) {
		if (WARN_ON(mt_execution_context_destroy(&ctx->exec_ctx_ta)))
			pr_warn("mt_pvr_bridge: r397: TA exec context destroy failed\n");
		ctx->exec_ta_ready = false;
	}
	if (ctx->exec_ready) {
		if (WARN_ON(mt_execution_context_destroy(&ctx->exec_ctx_3d)))
			pr_warn("mt_pvr_bridge: r390: exec context destroy failed\n");
		if (WARN_ON(mt_execution_process_destroy(&ctx->process)))
			pr_warn("mt_pvr_bridge: r390: exec process destroy failed\n");
		ctx->exec_ready = false;
	}

	/* 2. BOs: drop our refs. The VM holds one ref per binding;
	 * mt_gpu_vm_fini() (inside vm_destroy below) drops those. */
	for (i = 0; i < MT_GFX_CONTEXT_BO_COUNT; i++) {
		if (ctx->bos_ready[i]) {
			mt_bo_put(&ctx->bos[i]);
			ctx->bos_ready[i] = false;
			ctx->vas[i] = 0;
		}
	}

	/* r416: T2 render target (12th BO). */
	if (ctx->target_ready) {
		mt_bo_put(&ctx->target_bo);
		ctx->target_ready = false;
		ctx->target_va = 0;
	}

	/* 3. Per-context VM (fini tears down bindings, puts tables BO). */
	if (ctx->vm) {
		mt_render_context_vm_destroy(ctx->vm);
		ctx->vm = NULL;
		ctx->vm_base_va = 0;
	}

	ctx->resources_ready = false;
}

/* r389: R6-2 Create realization (Route A per-context).
 * Allocates 11 BOs (mt_gfx_context_bo_specs, 86,300B), writes initial data,
 * binds to per-context VM, builds CSW, creates exec process/context.
 * Any failure rolls back in reverse order; resources_ready only on full success.
 */
#define MT_RENDER_CONTEXT_VA_BASE 0x70000000ULL

static int mt_render_context_create(struct mt_pvr_file *file,
				    struct mt_pvr_render_context *ctx)
{
	struct mt_guest *g;
	struct mt_guest_device *d;
	struct module *owner = NULL;
	struct mt_bridge_ta_vm *tvm;
	u32 i;
	int ret;

	g = pvr_session_acquire(&owner);
	if (!g)
		return -ENODEV;
	d = container_of(g, struct mt_guest_device, state);

	/* 1. Per-context VM with d->buffers-backed page tables (r389). */
	tvm = mt_render_context_vm_create(d, &ctx->pt_bo);
	if (!tvm) {
		ret = -ENOMEM;
		goto out_release;
	}
	ctx->vm = tvm;
	ctx->vm_base_va = MT_RENDER_CONTEXT_VA_BASE;
	pr_info("mt_pvr_bridge: r389: render ctx VM created, base_va=%#llx\n",
		(unsigned long long)ctx->vm_base_va);

	/* 2-4. Allocate 11 BOs, write initial data, bind to VM. */
	for (i = 0; i < MT_GFX_CONTEXT_BO_COUNT; i++) {
		u32 bytes = mt_gfx_context_bo_specs[i].bytes;
		u32 alloc_size = PAGE_ALIGN(bytes);
		u64 va = ctx->vm_base_va + (u64)i * MT_RENDER_CONTEXT_VA_STRIDE;
		struct mt_vm_binding binding;

		ret = mt_bo_create(&ctx->bos[i], d->buffers.ops, &d->buffers,
				   alloc_size, PAGE_SIZE);
		if (ret) {
			pr_err("mt_pvr_bridge: r389: BO %u create failed: %d\n",
			       i, ret);
			goto out_rollback;
		}

		ret = pvr_translator_bo_write(d, &ctx->bos[i], 0,
					      mt_gfx_bo_init_metas[i].data,
					      bytes);
		if (ret) {
			pr_err("mt_pvr_bridge: r389: BO %u write failed: %d\n",
			       i, ret);
			mt_bo_put(&ctx->bos[i]);
			goto out_rollback;
		}

		binding.bo = &ctx->bos[i];
		binding.va = va;
		binding.offset = 0;
		binding.bytes = alloc_size;
		binding.flags = MT_GPU_MAP_DEFAULT;
		ret = mt_gpu_vm_bind_many(&tvm->vm, &binding, 1);
		if (ret) {
			pr_err("mt_pvr_bridge: r389: BO %u bind failed: %d\n",
			       i, ret);
			mt_bo_put(&ctx->bos[i]);
			goto out_rollback;
		}
		ctx->vas[i] = va;
		ctx->bos_ready[i] = true;
		pr_info("mt_pvr_bridge: r389: BO %u bound va=%#llx bytes=%u\n",
			i, (unsigned long long)va, alloc_size);
	}

	/* r416: T2 render target (12th BO). Bound here — before exec process
	 * creation, while the VM still accepts binds (active_uses==0). */
	{
		u64 tva = ctx->vm_base_va +
			(u64)MT_T2_TARGET_BO_SLOT * MT_RENDER_CONTEXT_VA_STRIDE;
		struct mt_vm_binding tbinding;

		ret = mt_bo_create(&ctx->target_bo, d->buffers.ops, &d->buffers,
				     MT_T2_TARGET_BYTES, PAGE_SIZE);
		if (ret) {
			pr_err("mt_pvr_bridge: r416: target BO create failed: %d\n",
			       ret);
			goto out_rollback;
		}
		tbinding.bo = &ctx->target_bo;
		tbinding.va = tva;
		tbinding.offset = 0;
		tbinding.bytes = MT_T2_TARGET_BYTES;
		tbinding.flags = MT_GPU_MAP_DEFAULT;
		ret = mt_gpu_vm_bind_many(&tvm->vm, &tbinding, 1);
		if (ret) {
			pr_err("mt_pvr_bridge: r416: target BO bind failed: %d\n",
			       ret);
			mt_bo_put(&ctx->target_bo);
			goto out_rollback;
		}
		ctx->target_va = tva;
		ctx->target_ready = true;
		pr_info("mt_pvr_bridge: r416: target BO bound va=%#llx bytes=%u\n",
			(unsigned long long)tva, MT_T2_TARGET_BYTES);
	}

	/* 5. Build CSW from bound VAs. */
	{
		struct mt_gfx_context_bo_addresses addrs;
		for (i = 0; i < MT_GFX_CONTEXT_BO_COUNT; i++)
			addrs.va[i] = ctx->vas[i];
		ret = mt_gfx_context_build_csw(ctx->csw, sizeof(ctx->csw),
					       &addrs);
		if (ret) {
			pr_err("mt_pvr_bridge: r389: CSW build failed: %d\n",
			       ret);
			goto out_rollback;
		}
		pr_info("mt_pvr_bridge: r389: CSW built\n");
	}

	/* 6-7. Execution process/context (node_type=5 -> DM2). */
	ret = mt_execution_process_create(&d->execution, &ctx->process,
					  &tvm->vm, task_tgid_nr(current));
	if (ret) {
		pr_err("mt_pvr_bridge: r389: exec process create failed: %d\n",
		       ret);
		goto out_rollback;
	}
	ret = mt_execution_context_create(&ctx->exec_ctx_3d, &ctx->process, 5, 0);
	if (ret) {
		pr_err("mt_pvr_bridge: r389: exec context create failed: %d\n",
		       ret);
		mt_execution_process_destroy(&ctx->process);
		goto out_rollback;
	}
	ctx->exec_ready = true;
	/* r397 Phase 1: TA execution context (node_type=2 -> DM3, r396 D1).
	 * Shares process/VM/11 BOs with the 3D context. Rollback via
	 * mt_render_context_destroy (single path, r390). */
	ret = mt_execution_context_create(&ctx->exec_ctx_ta, &ctx->process, 2, 0);
	if (ret) {
		pr_err("mt_pvr_bridge: r397: TA exec context create failed: %d\n",
		       ret);
		goto out_rollback;
	}
	ctx->exec_ta_ready = true;
	pr_info("mt_pvr_bridge: r397: exec process/contexts created (3D node_type=5, TA node_type=2)\n");

	/* 8. Full success. */
	ctx->resources_ready = true;
	pr_info("mt_pvr_bridge: r389: render context READY (11 BOs, CSW, exec)\n");
	ret = 0;
	goto out_release;

out_rollback:
	/* r390: Reuse the R6-3 destroy path for rollback (same reverse order). */
	mt_render_context_destroy(ctx);
out_release:
	if (owner)
		module_put(owner);
	return ret;
}



/* r376/r383: R5 TA VM via bridge-side self-contained init.
 * Replaces r375's bridge-side manual VM assembly (caused oops).
 * Uses proper mt_gpu_vm_init() with synthetic page-table BO (no borrow),
 * following the proven 3D pattern (pvr_gpu_vm_ensure).
 * (r376's probe-side API removed in r383 as dead code; probe cannot be
 *  reloaded while trial is pinned.)
 *
 * MT_TA_VM_READY gate stays CLOSED: mapping validated (V1/V2) but not
 * used in submit path. Marker-level TA continues.
 */

static int pvr_cmd_musakickgfx2(struct mt_pvr_file *file,
				struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_musakickgfx2_in in;
	struct mt_pvr_musakickgfx2_out out = { 0 };
	struct mt_ta_work work;
	struct mt_execution_context *ctx;
	struct mt_pvr_render_context *rctx = NULL;
	struct dma_fence *fence = NULL;
	struct mt_guest *g;
	struct mt_guest_device *d;
	struct mt_marker_store *s;
	struct module *owner = NULL;
	u32 wire_id;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	pr_info("mt_pvr_bridge: musakickgfx2 dispatch: "
		"ctx=%#llx abort=%u kick_ta=%u kick_3d=%u kick_pr=%u "
		"ta_size=%u 3d_size=%u 3dpr_size=%u draws=%u indices=%u mrt=%u "
		"ta_upd=%u ta_fence=%u 3d_upd=%u pmr_sync=%u "
		"check_fence=%d check_fence_3d=%d rt_size=%u\n",
		(unsigned long long)in.h_render_context,
		in.abort, in.kick_ta, in.kick_3d, in.kick_pr,
		in.ta_cmd_size, in.cmd_3d_size, in.cmd_3dpr_size,
		in.num_draw_calls, in.num_indices, in.num_mrts,
		in.client_ta_upd_count, in.client_ta_fence_count,
		in.client_3d_upd_count, in.sync_pmr_count,
		in.check_fence, in.check_fence_3d,
		in.render_target_size);

	/* D5: only TA kicks dispatch; PR/3D have no execution path. */
	if (!in.kick_ta || in.kick_3d) {
		pr_info("mt_pvr_bridge: musakickgfx2: not dispatched "
			"(kick_ta=%u kick_3d=%u)\n", in.kick_ta, in.kick_3d);
		return -EOPNOTSUPP;
	}

	/* r398 Phase 2: per-file VM removed. The kick requires a live
	 * MT_PVR_KIND_CONTEXT object whose render_ctx is resources_ready;
	 * otherwise it is rejected (-EINVAL). The TA marker op requires
	 * route.dm == MT_FW_DM_TA; submission uses rctx->exec_ctx_ta
	 * (r397 Phase 2). */
	{
		struct mt_pvr_object *robj;

		robj = pvr_object_find(file, in.h_render_context,
					 MT_PVR_KIND_CONTEXT);
		if (robj)
			rctx = robj->render_ctx;
		if (!(rctx && rctx->resources_ready && rctx->vm)) {
			pr_info("mt_pvr_bridge: musakickgfx2: no live render_ctx "
				"(r398 Phase 2, per-file VM removed) -> -EINVAL\n");
			return -EINVAL;
		}
		pr_info("mt_pvr_bridge: r391: kick with render_ctx "
			"vm_base_va=%#llx\n",
			(unsigned long long)rctx->vm_base_va);
		/* r391 V2: bind validation on the per-context VM.
		 * Tests bind with empty binding (validates VM state, no oops). */
		ret = mt_gpu_vm_bind_many(&rctx->vm->vm, NULL, 0);
		/* bind_many with count=0 returns -EINVAL (expected);
		 * oops would be BUG. */
		pr_info("mt_pvr_bridge: r391 V2: bind empty ret=%d (expect -EINVAL, no oops)\n",
			ret);
		ret = 0;
	}

	g = pvr_session_acquire(&owner);
	if (!g)
		return -ENODEV;
	d = container_of(g, struct mt_guest_device, state);
	s = &d->markers;

	mutex_lock(&g->trial_lock);
	if (s->lock != &g->trial_lock || !s->can_submit || s->opaque != g ||
	    s->ready || s->work_ready) {
		ret = -EBUSY;
		goto out_unlock;
	}
	/* r397 Phase 2 (r398: rctx is guaranteed live, checked above):
	 * submit under the real TA execution context (node_type=2,
	 * route.dm==3). The marker op takes no ownership (r368), so the
	 * kick borrows the pointer; no kfree. */
	ctx = &rctx->exec_ctx_ta;
	pr_info("mt_pvr_bridge: r397: kick with real exec_ctx_ta (dm=%u)\n",
		ctx->route.dm);
	memset(&work, 0, sizeof(work));
	work.job.state = MT_JOB_HELD;
	work.context = ctx;
	mt_ta_params_from_musakickgfx2(&work.params, &in);
	if (work.params.kick_flags & MT_TA_KICK_PR)
		pr_info("mt_pvr_bridge: musakickgfx2: kick_pr=1 TO-VALIDATE, "
			"recorded; op will honestly refuse\n");

	s->ready = true;
	mutex_unlock(&g->trial_lock);
	/* The op manages s->lock internally; the caller must not hold it. */
	ret = mt_bridge_submit_ta_work(s, &work, &fence);
	mutex_lock(&g->trial_lock);
	s->ready = false;
	mutex_unlock(&g->trial_lock);
	if (owner)
		module_put(owner);

	if (ret) {
		pr_info("mt_pvr_bridge: musakickgfx2: submit_ta_work -> %d\n",
			ret);
		return ret;
	}

	/* D5: OUT.update_fence <- wire_id (no 3D work this round). */
	wire_id = container_of(fence, struct mt_marker_fence, fence)->wire_id;
	dma_fence_put(fence);
	/* Marker-level: the op takes no context ownership (it clears
	 * work->context and never assigns m->context -- r368 falsified the
	 * r367 UAF claim). The dispatch-allocated ctx is therefore
	 * unreferenced after the op returns. r398: ctx is borrowed from
	 * the live render_ctx (no allocation), so nothing to free here. */

	/* r370: production TA completion path (R2b). The frozen probe's
	 * event drain rejects 0x100, so without this poll the marker hangs
	 * forever (r368: wire 6). Retire it via the TA-aware completion;
	 * on timeout, error-signal so check_fence waiters do not hang. */
	mutex_lock(&g->trial_lock);
	ret = pvr_ta_wait_complete(g, s, wire_id);
	if (ret == -ETIMEDOUT) {
		pvr_ta_abandon(g, s, wire_id);
		ret = 0;
	} else if (ret) {
		pr_warn("mt_pvr_bridge: musakickgfx2: TA complete rc=%d wire=%u\n",
			ret, wire_id);
		ret = 0;
	}
	mutex_unlock(&g->trial_lock);

	out.error = 0;
	out.update_fence = (int)wire_id;
	out.update_fence_3d = 0;
	/* r373: report writeback failures instead of logging a misleading
	 * "submitted" line. If userspace passes out_size < 12 (or a bad
	 * out_ptr), pvr_out fails and the OUT buffer is left untouched --
	 * previously dmesg still claimed success, hiding the harness bug. */
	ret = pvr_out(cmd, &out, sizeof(out));
	if (ret) {
		pr_warn("mt_pvr_bridge: musakickgfx2: OUT writeback failed rc=%d wire=%u\n",
			ret, wire_id);
		return ret;
	}
	pr_info("mt_pvr_bridge: musakickgfx2: submitted wire=%u\n", wire_id);
	return 0;

out_unlock:
	mutex_unlock(&g->trial_lock);
	if (owner)
		module_put(owner);
	return ret;
}

/* 0x82:0x14 RGXKICKTA3D5 (r215): accept-and-log observer, mirroring the
 * 0x89:0xa handler (r174). Reports the named CCB window plus the scalar
 * check/update/sync-PMR counts and returns 0 without executing anything:
 * the check/update/sync-PMR pointer fields name userspace arrays that are
 * never dereferenced here, no fence is minted, and no firmware/DMA/
 * translator path is reachable. Real TA/3D execution still needs the
 * DDK2 render backend (r207/r208 boundary).
 */
static int pvr_cmd_kickta3d5_observe(struct mt_pvr_file *file,
				     struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_rgxkickta3d5_in in;
	struct mt_pvr_rgxkickta3d5_out out = { 0 };
	struct mt_pvr_object *obj;
	struct mt_pvr_binding *binding = NULL, *b;
	struct mt_pvr_pmr *pmr = NULL;
	u64 end, off, i, nonzero = 0, first = 0;
	u64 hash = 1469598103934665603ULL;
	u8 head[64];
	u32 head_len = 0;
	int ret;

	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		return ret;
	obj = pvr_object_find(file, in.render_context, MT_PVR_KIND_CONTEXT);
	if (!obj)
		return -ENOENT;
	if (!in.submission_size ||
	    in.submission_size > MT_PVR_SUBMIT3_LOG_MAX ||
	    in.submission_va + in.submission_size < in.submission_va)
		return -EINVAL;
	end = in.submission_va + in.submission_size;
	list_for_each_entry(b, &file->bindings, link) {
		struct mt_pvr_object *res =
			pvr_reservation_find(file, b->reservation);

		if (!res || res->arg0 > in.submission_va)
			continue;
		if (in.submission_va - res->arg0 > res->arg1)
			continue;
		if (end - res->arg0 > res->arg1)
			continue;
		binding = b;
		break;
	}
	if (!binding)
		return -EINVAL;
	pmr = pvr_pmr_find(file, binding->pmr);
	if (!pmr || !pmr->host || !pmr->bytes)
		return -EINVAL;
	if (binding->va > in.submission_va)
		return -EINVAL;
	off = in.submission_va - binding->va;
	if (off > pmr->bytes || in.submission_size > pmr->bytes - off)
		return -EINVAL;
	for (i = 0; i < in.submission_size; i++) {
		u8 byte = ((u8 *)pmr->host)[off + i];

		hash ^= byte;
		hash *= 1099511628211ULL;
		if (!byte)
			continue;
		if (!nonzero)
			first = i;
		nonzero++;
		if (head_len < sizeof(head))
			head[head_len++] = byte;
	}
	pr_info("mt_pvr_bridge: kickta3d5 observe: flags=%#x va=%#llx bytes=%u id=%llu check=%u update=%u pmrsync=%u res=%#llx pmr=%#llx nonzero=%llu first=%#llx fnv=%#llx head=%*ph\n",
		in.submission_flags, (unsigned long long)in.submission_va,
		in.submission_size, (unsigned long long)in.submission_id,
		in.check_count, in.update_count, in.sync_pmr_count,
		(unsigned long long)binding->reservation,
		(unsigned long long)binding->pmr,
		(unsigned long long)nonzero, (unsigned long long)first,
		(unsigned long long)hash, head_len, head);
	return pvr_out(cmd, &out, sizeof(out));
}

/* Prototype fill geometry lives in ../mt_addr_plan.h (r186; r178:
 * orientation evidence pending; the rect builder rejects anything that
 * does not factor the parsed pixel count).
 */

/* Dry-run transfer translation (r181): resolve the destination pool, parse
 * geometry/color, build the TQX fill program the submission would emit, log
 * its digest -- then stop. No session objects, no page-table changes, no
 * submission, no fence. Any ambiguity fails loudly with errno instead of
 * guessing. file->lock held; must not take it again.
 */
/* Locate the destination pool and build the fill program (r267): shared
 * by dry-run (digest only) and live fire (submit). force_pmr selects the
 * pool deterministically (r304: CCB-derived destination); 0 keeps the
 * best-heuristic. Returns the dst PMR, its binding, the rect, and the
 * built program. Caller logs.
 */
static int pvr_submit3_locate_dst(struct mt_pvr_file *file,
				  u64 ccb_pmr,
				  u64 force_pmr,
				  struct mt_pvr_pmr **dst_out,
				  struct mt_pvr_binding **binding_out,
				  struct mt_transfer_fill_rect *rect_out,
				  u8 *prog, u32 prog_size,
				  struct mt_transfer_surface *surf_out)
{
	struct mt_pvr_pmr *pmr, *dst = NULL, *pristine = NULL;
	struct mt_pvr_binding *b, *dst_binding = NULL;
	struct mt_transfer_surface surf = { 0 }, pristine_surf = { 0 };
	struct mt_transfer_fill_rect rect;
	struct mt_tqx_fill_input fi;
	u64 best = 0, best_nz = 0, pristine_pixels = 0;
	u32 best_color = 0;
	int ret;

	if (force_pmr) {
		dst = pvr_pmr_find(file, force_pmr);
		if (!dst || dst->handle == ccb_pmr || !dst->host ||
		    dst->bytes < (1ULL << 20) ||
		    mt_transfer_pool_parse(dst->host, dst->bytes, &surf))
			return -ENODATA;
		list_for_each_entry(b, &file->bindings, link) {
			if (b->pmr == dst->handle) {
				dst_binding = b;
				break;
			}
		}
		if (!dst_binding)
			return -ENOENT;
		goto build;
	}

	list_for_each_entry(pmr, &file->pmrs, link) {
		struct mt_transfer_surface cand;
		u64 i, nz = 0;

		if (pmr->handle == ccb_pmr)
			continue;
		if (pmr->bytes < (1ULL << 20) || !pmr->host)
			continue;
		if (mt_transfer_pool_parse(pmr->host, pmr->bytes, &cand))
			continue;
		for (i = 0; i < pmr->bytes; i++) {
			if (((const u8 *)pmr->host)[i])
				nz++;
		}
		{
			/* Content shape (r310): solid pools have exactly one
			 * distinct pixel word; patterns have more. A solid
			 * fill can only reproduce the former -- count it.
			 */
			u32 w0 = 0, ndistinct = 0;
			u64 w;

			for (w = MT_TRANSFER_POOL_HEAD;
			     w + 4 <= pmr->bytes; w += 4) {
				u32 v;

				memcpy(&v, (const u8 *)pmr->host + w,
				       sizeof(v));
				if (!ndistinct || v != w0) {
					w0 = v;
					if (++ndistinct > 1)
						break;
				}
			}
			pr_info("mt_pvr_bridge: submit3 poolshape: pmr=%#llx distinct=%u first=%#x\n",
				(unsigned long long)pmr->handle, ndistinct,
				w0);
			/* Autorect bounds (r312): first/last nonzero word for
			 * deriving a fill rect from a reference pattern.
			 */
			if (ndistinct > 1) {
				u64 fw = 0, lw = 0;
				u32 fv = 0;
				u64 w2;

				for (w2 = MT_TRANSFER_POOL_HEAD;
				     w2 + 4 <= pmr->bytes; w2 += 4) {
					u32 v;

					memcpy(&v, (const u8 *)pmr->host + w2,
					       sizeof(v));
					if (!v)
						continue;
					if (!fw)
						fw = w2;
					lw = w2;
					if (!fv)
						fv = v;
				}
				pr_info("mt_pvr_bridge: submit3 poolbox: pmr=%#llx first=%llu last=%llu val=%#x\n",
					(unsigned long long)pmr->handle,
					(unsigned long long)fw,
					(unsigned long long)lw, fv);
			}
		}
		pr_info("mt_pvr_bridge: submit3 pool: pmr=%#llx bytes=%llu pixels=%llu nz=%llu color=%#x\n",
			(unsigned long long)pmr->handle,
			(unsigned long long)pmr->bytes,
			(unsigned long long)cand.pixels,
			(unsigned long long)nz, cand.color);
		if (!nz && cand.pixels > pristine_pixels) {
			pristine = pmr;
			pristine_surf = cand;
			pristine_pixels = cand.pixels;
		}
		if (!nz || cand.pixels < best)
			continue;
		if (cand.pixels == best && nz <= best_nz)
			continue;
		best = cand.pixels;
		best_nz = nz;
		best_color = cand.color;
		dst = pmr;
		surf = cand;
	}
	if (!dst) {
		pr_info("mt_pvr_bridge: submit3 dst: no parsed pool\n");
		return -EOPNOTSUPP;
	}
build:
	/* Pristine override (r306): a fill target starts unwritten; when
	 * the best-heuristic lands on a patterned pool while a pristine
	 * pool of the same geometry exists, the pristine one is the
	 * destination. Forced (CCB-derived) selections are honored as-is.
	 */
	if (!force_pmr && best_nz > 0 && pristine &&
	    pristine_pixels == best) {
		dst = pristine;
		surf = pristine_surf;
		/* The pristine pool's own first pixel is zero; the fill
		 * colour comes from the patterned (source) pool. Solid
		 * patterns only -- our fill primitive cannot do gradients
		 * (documented r306 boundary).
		 */
		surf.color = best_color;
		pr_info("mt_pvr_bridge: submit3 dst: pristine override pool=%#llx color=%#x\n",
			(unsigned long long)dst->handle, surf.color);
	}
	pr_info("mt_pvr_bridge: submit3 dst: pool=%#llx pixels=%llu color=%#x forced=%d\n",
		(unsigned long long)dst->handle,
		(unsigned long long)surf.pixels,
		surf.color, force_pmr != 0);
	ret = mt_transfer_fill_rect(&rect, 0, MT_TRANSFER_PROTO_W,
				    MT_TRANSFER_PROTO_H, surf.color, surf.pixels);
	if (ret)
		return ret;
	list_for_each_entry(b, &file->bindings, link) {
		if (b->pmr == dst->handle) {
			dst_binding = b;
			break;
		}
	}
	if (!dst_binding)
		return -ENOENT;
	rect.dst_va = dst_binding->va + MT_TRANSFER_POOL_HEAD;
	fi = (struct mt_tqx_fill_input){
		.destination_va = rect.dst_va, .command_va = MT_TRANSLATE_CMD_VA,
		.element_bytes = MT_TRANSFER_PIXEL_BYTES, .width = rect.width,
		.height = rect.height, .x = 0, .y = 0,
		.rect_width = rect.width, .rect_height = rect.height,
		.color = { rect.color, 0, 0, 0 },
	};
	ret = mt_tqx_fill_build(prog, prog_size, &fi);
	if (ret)
		return ret;
	*dst_out = dst;
	*binding_out = dst_binding;
	*rect_out = rect;
	*surf_out = surf;
	return 0;
}

/* Serialized live fire (r290): the work prepares, submits, waits and
 * verifies one chunk at a time. Pool slices stay loaned to a submitted
 * job until its fence completes, so preparing all chunks up front
 * self-blocks with -EBUSY (r283). trial_lock is taken only for submit
 * and readback; buffers->lock only for prepare; waits stay outside all
 * locks with the 5s fence budget, and a failed chunk stops the loop.
 * Teardown sets fire_abort then cancel_work_sync()s this first, so the
 * wait per chunk bounds unload latency. Never touches file objects.
 */
static void pvr_translator_fire_work(struct work_struct *ws)
{
	static struct mt_tqx_upload_ops upload = {
		.write = pvr_translator_upload_write,
		.read = pvr_translator_upload_read,
	};
	struct mt_tqx_fill_input fi;
	struct mt_bo *bos[4];
	struct mt_tqx_fill_workspace *fill_ws;
	struct dma_fence *fence = NULL;
	u8 *chunk_buf = NULL;
	u32 width, height, color, cores, chunk_rows, nchunks;
	u32 c, pixels = 0, bad = 0;
	u32 first = 0, last = 0;
	u64 seq;
	void *dst_host;
	u64 dst_span;
	bool to_dst;
	long waited;
	int ret = 0;

	(void)ws;
	width = translator.fire_width;
	height = translator.fire_height;
	color = translator.fire_color;
	cores = translator.fire_cores;
	chunk_rows = translator.fire_chunk_h;
	nchunks = translator.fire_nchunks;
	seq = translator.fire_seq;
	dst_host = translator.fire_dst_host;
	dst_span = translator.fire_dst_span;
	to_dst = translator.fire_to_dst;
	if (!width || !nchunks || nchunks > MT_TQX_FIRE_MAX_CHUNKS)
		goto out;
	fill_ws = kvzalloc(sizeof(*fill_ws), GFP_KERNEL);
	chunk_buf = kvzalloc(MT_TQX_SCRATCH_BYTES, GFP_KERNEL);
	if (!fill_ws || !chunk_buf) {
		ret = -ENOMEM;
		goto out_free;
	}
	bos[0] = &translator.tqx_cmd;
	bos[1] = &translator.tqx_scratch;
	bos[2] = &translator.tqx_dma;
	bos[3] = &translator.tqx_state;
	for (c = 0; c < nchunks; c++) {
		struct mt_tqx_work work = { 0 };
		u32 rows = min(chunk_rows, height - c * chunk_rows);
		u32 npx = width * rows;
		u32 k;

		if (READ_ONCE(translator.fire_abort)) {
			ret = -ECANCELED;
			break;
		}
		fi = (struct mt_tqx_fill_input){
			.destination_va = MT_TQX_SCRATCH_VA,
			.command_va = MT_TQX_CMD_VA,
			.element_bytes = MT_TRANSFER_PIXEL_BYTES,
			.width = width, .height = rows,
			.x = 0, .y = 0, .rect_width = width,
			.rect_height = rows,
			.color = { color, 0, 0, 0 },
		};
		WRITE_ONCE(pvr_translator_upload_dev, translator.dev);
		mutex_lock(translator.dev->shared_boot.buffers->lock);
		ret = mt_tqx_fill_work_prepare(&work, fill_ws, &upload,
				&translator.dev->shared_boot,
				&translator.dev->gem.profile, cores,
				&translator.tqx_context, bos, &fi,
				MT_TQX_DMA_VA, MT_TQX_STATE_VA);
		mutex_unlock(translator.dev->shared_boot.buffers->lock);
		WRITE_ONCE(pvr_translator_upload_dev, NULL);
		if (ret) {
			pr_info("mt_pvr_bridge: fire seq=%llu: chunk %u prepare: %d\n",
				(unsigned long long)seq, c, ret);
			if (work.context)
				WARN_ON(mt_tqx_work_cancel(&work));
			break;
		}
		mutex_lock(&translator.dev->state.trial_lock);
		if (translator.dev->markers.total ||
		    translator.dev->markers.ready ||
		    translator.dev->markers.work_ready) {
			ret = -EBUSY;
			pr_info("mt_pvr_bridge: fire seq=%llu: chunk %u markers busy\n",
				(unsigned long long)seq, c);
			mutex_unlock(&translator.dev->state.trial_lock);
			if (work.context)
				WARN_ON(mt_tqx_work_cancel(&work));
			break;
		}
		translator.dev->markers.ready = true;
		translator.dev->markers.work_ready = true;
		ret = translator.dev->markers.ops->submit_tqx_work(
				&translator.dev->markers, &work, &fence);
		translator.dev->markers.work_ready = false;
		translator.dev->markers.ready = false;
		mutex_unlock(&translator.dev->state.trial_lock);
		if (ret) {
			pr_info("mt_pvr_bridge: fire seq=%llu: chunk %u submit: %d\n",
				(unsigned long long)seq, c, ret);
			if (work.context)
				WARN_ON(mt_tqx_work_cancel(&work));
			break;
		}
		waited = dma_fence_wait_timeout(fence, false,
				msecs_to_jiffies(MT_TRANSLATE_FENCE_WAIT_MS));
		ret = waited > 0 ? dma_fence_get_status(fence) :
			(waited < 0 ? (int)waited : -ETIMEDOUT);
		dma_fence_put(fence);
		fence = NULL;
		if (ret != 1) {
			pr_info("mt_pvr_bridge: fire seq=%llu: chunk %u fence: %d\n",
				(unsigned long long)seq, c,
				ret ? ret : -EIO);
			ret = ret ? ret : -EIO;
			break;
		}
		ret = 0;
		/* Verify from a single bulk read, then land the verified
		 * chunk in the UMD pool when requested. */
		WRITE_ONCE(pvr_translator_upload_dev, translator.dev);
		mutex_lock(&translator.dev->state.trial_lock);
		ret = pvr_translator_bo_read(translator.dev,
					     &translator.tqx_scratch, 0,
					     chunk_buf, npx * sizeof(u32));
		mutex_unlock(&translator.dev->state.trial_lock);
		WRITE_ONCE(pvr_translator_upload_dev, NULL);
		if (ret) {
			pr_info("mt_pvr_bridge: fire seq=%llu: chunk %u readback: %d\n",
				(unsigned long long)seq, c, ret);
			break;
		}
		for (k = 0; k < npx; k++) {
			u32 v = ((u32 *)chunk_buf)[k];

			if (!c && !k)
				first = v;
			last = v;
			if (v != color)
				bad++;
		}
		if (bad) {
			ret = -EILSEQ;
			pr_info("mt_pvr_bridge: fire seq=%llu: chunk %u mismatch bad=%u\n",
				(unsigned long long)seq, c, bad);
			break;
		}
		if (to_dst) {
			/* r315: the UMD compare reads dest from pool offset
			 * 0 against source from +HEAD (live GDB: rcx@+0 vs
			 * rsi@+3841 over the full 5242880 bytes), so
			 * destination pixels start at pool base, not HEAD.
			 */
			u64 dst_off = (u64)c * chunk_rows * width *
				      MT_TRANSFER_PIXEL_BYTES;

			if (!dst_host || !dst_span ||
			    dst_off > dst_span ||
			    (u64)npx * sizeof(u32) > dst_span - dst_off) {
				ret = -ERANGE;
				pr_info("mt_pvr_bridge: fire seq=%llu: chunk %u dst bounds\n",
					(unsigned long long)seq, c);
				break;
			}
			memcpy((u8 *)dst_host + dst_off, chunk_buf,
			       (size_t)npx * sizeof(u32));
		}
		pixels += npx;
	}
	kvfree(fill_ws);
	kvfree(chunk_buf);
	pr_info("mt_pvr_bridge: fire seq=%llu: fired=%d chunks=%u verified=%d bad=%u/%u first=%#x last=%#x todst=%d\n",
		(unsigned long long)seq, !ret, nchunks,
		!ret && !bad && pixels == width * height, bad, pixels,
		first, last, to_dst);
out:
	WRITE_ONCE(translator.fire_result, ret);
	WRITE_ONCE(translator.fire_running, false);
	WRITE_ONCE(translator.fire_pending, false);
	return;
out_free:
	kvfree(fill_ws);
	kvfree(chunk_buf);
	goto out;
}

/* Live TQX fill fire (r267, serialized r290): locate the UMD-derived
 * rect, record it, and schedule the work, which prepares, submits,
 * waits and verifies one chunk at a time. file->lock + translator_lock
 * held; never waits here (r147) and never submits here (submit needs a
 * completed prior fence; pipelining self-blocks, r283) — the work does.
 * force_pmr (r304: CCB-derived destination, 0 = best-heuristic) selects
 * the pool deterministically.
 */
static int pvr_submit3_transfer_fire(struct mt_pvr_file *file,
				     struct mt_guest_device *d,
				     u64 ccb_pmr, u64 force_pmr)
{
	struct mt_pvr_pmr *dst = NULL;
	struct mt_pvr_binding *dst_binding = NULL;
	struct mt_transfer_surface surf = { 0 };
	struct mt_transfer_fill_rect rect;
	u8 prog[sizeof(struct mt_tqx_fill_image)];
	u32 cores;
	u64 row_bytes;
	u32 chunk_rows;
	u32 nchunks;
	int ret;

	if (!translator.tqx_ready || !translator.tqx_slices_ready)
		return -EOPNOTSUPP;
	if (READ_ONCE(translator.fire_running))
		return -EBUSY;
	(void)dst_binding;
	ret = pvr_submit3_locate_dst(file, ccb_pmr, force_pmr, &dst,
				     &dst_binding, &rect, prog, sizeof(prog),
				     &surf);
	if (ret)
		return ret;
	/* Chunked fire (r280): the 256KB scratch cannot hold a full frame
	 * (1280x1024x4 = 5MB), so split the rect into row strips that each
	 * fit. Every chunk reuses the scratch base; the work verifies each
	 * chunk before the next overwrites it.
	 */
	if (!rect.width || !rect.height)
		return -EINVAL;
	row_bytes = (u64)rect.width * MT_TRANSFER_PIXEL_BYTES;
	if (!row_bytes || row_bytes > MT_TQX_SCRATCH_BYTES)
		return -E2BIG;
	chunk_rows = (u32)(MT_TQX_SCRATCH_BYTES / row_bytes);
	nchunks = (rect.height + chunk_rows - 1) / chunk_rows;
	if (!nchunks || nchunks > MT_TQX_FIRE_MAX_CHUNKS)
		return -E2BIG;
	ret = mt_tqx_topology_from_info(&cores, &d->gem.profile,
					(void *)d->state.info, PAGE_SIZE);
	if (ret || cores != d->gem.tqx_cores)
		return ret ? ret : -EINVAL;
	translator.fire_nchunks = nchunks;
	translator.fire_chunk_h = chunk_rows;
	translator.fire_width = rect.width;
	translator.fire_height = rect.height;
	translator.fire_color = translate_fire_color ? translate_fire_color :
		rect.color;
	translator.fire_cores = cores;
	translator.fire_seq = ++translator.seq;
	translator.fire_dst_host = (translate_fire_to_dst && dst->host) ?
		dst->host : NULL;
	translator.fire_dst_span = dst->bytes;
	if (translate_fire_to_dst &&
	    ((u64)MT_TRANSFER_POOL_HEAD > dst->bytes ||
	     (u64)rect.width * rect.height * MT_TRANSFER_PIXEL_BYTES >
	     dst->bytes - MT_TRANSFER_POOL_HEAD))
		return -ERANGE;
	translator.fire_to_dst = translate_fire_to_dst;
	translator.fire_result = -EBUSY;
	WRITE_ONCE(translator.fire_abort, false);
	WRITE_ONCE(translator.fire_running, true);
	translator.fire_pending = true;
	schedule_work(&translator.fire_work);
	pr_info("mt_pvr_bridge: fire seq=%llu: scheduled chunks=%u %ux%u color=%#x todst=%d\n",
		(unsigned long long)translator.fire_seq, nchunks,
		rect.width, rect.height, rect.color, translate_fire_to_dst);
	return 0;
}

static int pvr_submit3_transfer_dry_run(struct mt_pvr_file *file,
					const struct mt_pvr_tdm_submit3_in *in,
					u64 ccb_pmr)
{
	struct mt_pvr_pmr *dst = NULL;
	struct mt_pvr_binding *dst_binding = NULL;
	struct mt_transfer_surface surf = { 0 };
	struct mt_transfer_fill_rect rect;
	u8 prog[sizeof(struct mt_tqx_fill_image)];
	u64 phash = 1469598103934665603ULL;
	u32 k;
	int ret;

	ret = pvr_submit3_locate_dst(file, ccb_pmr, 0, &dst, &dst_binding,
				     &rect, prog, sizeof(prog), &surf);
	if (ret) {
		if (ret == -EOPNOTSUPP)
			pr_info("mt_pvr_bridge: submit3 dry-run: no parsed pool\n");
		return ret;
	}
	(void)dst_binding;
	for (k = 0; k < sizeof(prog); k++) {
		phash ^= prog[k];
		phash *= 1099511628211ULL;
	}
	pr_info("mt_pvr_bridge: submit3 dry-run: pool=%#llx pixels=%llu color=%#x va=%#llx %ux%u fnv=%#llx\n",
		(unsigned long long)dst->handle,
		(unsigned long long)surf.pixels, surf.color,
		(unsigned long long)rect.dst_va, rect.width, rect.height,
		(unsigned long long)phash);
	return 0;
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


/* r404: per-bridge-group dispatch helpers, split from pvr_bridge_dispatch.
 * Each helper owns the inner function switch for one bridge group.
 * Behavior is identical; this is a pure mechanical split for readability.
 */
static int pvr_dispatch_srvcore(struct mt_pvr_file *file, u32 function,
			 struct mt_pvr_cmd *cmd)
{
	switch (function) {
	case MT_PVR_FN_CONNECT:
		return pvr_cmd_connect(file, cmd);
	case MT_PVR_FN_DISCONNECT:			/* Disconnect */
	case MT_PVR_FN_RELEASEINFOPAGE:			/* ReleaseInfoPage */
		return pvr_stub_ok(cmd);
	case MT_PVR_FN_ACQUIREGLOBALEVENTOBJECT:			/* AcquireGlobalEventObject */
	case MT_PVR_FN_EVENTOBJECTOPEN:			/* EventObjectOpen */
		return pvr_cmd_event_handle(file, cmd);
	case MT_PVR_FN_RELEASEGLOBALEVENTOBJECT:			/* ReleaseGlobalEventObject */
	case MT_PVR_FN_EVENTOBJECTWAIT:			/* EventObjectWait */
	case MT_PVR_FN_EVENTOBJECTCLOSE:			/* EventObjectClose */
	case MT_PVR_FN_ALIGNMENTCHECK:			/* AlignmentCheck */
	case MT_PVR_FN_EVENTOBJECTWAITTIMEOUT:			/* EventObjectWaitTimeout */
		return pvr_stub_ok(cmd);
	case MT_PVR_FN_GETMULTICOREINFO:			/* GetMultiCoreInfo */
		return pvr_cmd_multicore_info(cmd);
	case MT_PVR_FN_ACQUIREINFOPAGE:			/* AcquireInfoPage */
		return pvr_cmd_info_page(file, cmd);
	default:
		return -ENOTTY;
}
}

static int pvr_dispatch_sync(struct mt_pvr_file *file, u32 function,
			 struct mt_pvr_cmd *cmd)
{
	switch (function) {
	case MT_PVR_FN_ALLOCSYNCPRIMITIVEBLOCK:
		return pvr_cmd_sync_block(file, cmd);
	case MT_PVR_FN_FREESYNCPRIMITIVEBLOCK:			/* FreeSyncPrimitiveBlock */
		return pvr_stub_ok(cmd);
	case MT_PVR_FN_SYNCPRIMSET:			/* SyncPrimSet (free-path clearer wrapper; stubbed) */
	case MT_PVR_FN_SYNCALLOCEVENT:			/* SyncAllocEvent */
	case MT_PVR_FN_SYNCFREEEVENT:			/* SyncFreeEvent (DDK2 destroy tail, r144) */
		return pvr_stub_ok(cmd);
	case MT_PVR_FN_SYNCPRIMCPUSIGNAL:			/* SyncPrimCpuSignal (real write, r222) */
		return pvr_cmd_syncprim_set(file, cmd);
	case MT_PVR_FN_SYNCPRIMIMPORTFD:	/* SyncPrimImportFD (r386) */
		return pvr_cmd_syncprim_importfd(file, cmd);
	default:
		return -ENOTTY;
}
}

static int pvr_dispatch_mm(struct mt_pvr_file *file, u32 function,
			 struct mt_pvr_cmd *cmd)
{
	switch (function) {
	case MT_PVR_FN_PMRMAKELOCALIMPORTHANDLE:			/* PmrMakeLocalImportHandle */
		return pvr_cmd_pmr_make_import(file, cmd);
	case MT_PVR_FN_PMRUNMAKELOCALIMPORTHANDLE:			/* PmrUnmakeLocalImportHandle */
		return pvr_cmd_pmr_unmake_import(file, cmd);
	case MT_PVR_FN_PMRLOCALIMPORTPMR:			/* PmrLocalImportPmr */
		return pvr_cmd_pmr_import(file, cmd);
	case MT_PVR_FN_PMRUNREFPMR:			/* PmrUnrefPmr */
		return pvr_cmd_pmr_unref(file, cmd);
	case MT_PVR_FN_DEVMEMINTCTXDESTROY:			/* DevmemIntCtxDestroy */
		return pvr_cmd_ctx_destroy(file, cmd);
	case MT_PVR_FN_DEVMEMINTHEAPDESTROY:			/* DevmemIntHeapDestroy */
		return pvr_cmd_heap_destroy(file, cmd);
	case MT_PVR_FN_PHYSMEMNEWRAMBACKEDPMR:			/* PhysMemNewRamBackedPmr */
		return pvr_cmd_pmr_alloc(file, cmd);
	case MT_PVR_FN_DEVMEMINTCTXCREATE:			/* DevmemIntCtxCreate */
		return pvr_cmd_ctx_create(file, cmd);
	case MT_PVR_FN_DEVMEMINTHEAPCREATE:			/* DevmemIntHeapCreate */
		return pvr_cmd_heap_create(file, cmd);
	case MT_PVR_FN_DEVMEMINTMAPPMR:			/* DevmemIntMapPmr */
		return pvr_cmd_pmr_map(file, cmd);
	case MT_PVR_FN_DEVMEMINTUNMAPPMR:			/* DevmemIntUnmapPMR */
		return pvr_cmd_unmap_pmr(file, cmd);
	case MT_PVR_FN_DEVMEMINTRESERVERANGE:			/* DevmemIntReserveRange */
		return pvr_cmd_pmr_reserve(file, cmd);
	case MT_PVR_FN_DEVMEMINTUNRESERVERANGE:			/* DevmemIntUnreserveRange */
		return pvr_cmd_unreserve_range(file, cmd);
	case MT_PVR_FN_HEAPCFGHEAPCOUNT:			/* HeapCfgHeapCount */
		return pvr_cmd_heap_count(file, cmd);
	case MT_PVR_FN_HEAPCFGHEAPDETAILS:			/* HeapCfgHeapDetails */
		return pvr_cmd_heap_details(file, cmd);
	case MT_PVR_FN_MTGPUUPDATEOOMSTATS:			/* MTGPUUpdateOOMStats */
		return pvr_cmd_oom_stats(file, cmd);
	default:
		return -ENOTTY;
}
}

static int pvr_dispatch_rgxcompute(struct mt_pvr_file *file, u32 function,
			 struct mt_pvr_cmd *cmd)
{
	switch (function) {
	case MT_PVR_FN_RGXCREATECOMPUTECONTEXT:			/* RGXCreateComputeContext */
		return pvr_cmd_compute_create(file, cmd);
	case MT_PVR_FN_RGXDESTROYCOMPUTECONTEXT:			/* RGXDestroyComputeContext */
		return pvr_cmd_compute_destroy(file, cmd);
	default:
		/* 0x81:0x5 RGXKICKSYNC2 and friends submit real work;
		 * refusing them is the S4 boundary, not a gap.
		 */
		return -ENOTTY;
}
}

#if MT_TA_READBACK_DEBUG && MT_TA_REAL_PACKET
/* r416: T2 debug ioctl 0x82:0xFD — submit real TA, wait for fence,
 * read back the render target pixels. Debug-only (gated, never production).
 * Requires both MT_TA_READBACK_DEBUG and MT_TA_REAL_PACKET. */
static int pvr_cmd_ta_readback(struct mt_pvr_file *file, struct mt_pvr_cmd *cmd)
{
	struct mt_pvr_ta_readback_in in;
	struct mt_pvr_ta_readback_out *out;
	struct mt_pvr_object *robj;
	struct mt_pvr_render_context *rctx;
	struct mt_ta_real_request req;
	struct dma_fence *fence = NULL;
	struct mt_guest *g;
	struct mt_guest_device *d;
	struct module *owner = NULL;
	long waited;
	int ret;

	out = kzalloc(sizeof(*out), GFP_KERNEL);
	if (!out)
		return -ENOMEM;
	ret = pvr_in(cmd, &in, sizeof(in));
	if (ret)
		goto out_free;
	robj = pvr_object_find(file, in.h_render_context, MT_PVR_KIND_CONTEXT);
	if (!robj || !robj->render_ctx) {
		ret = -EINVAL;
		goto out_free;
	}
	rctx = robj->render_ctx;
	if (!rctx->target_ready) {
		ret = -ENODEV;
		goto out_free;
	}
	req.h_render_context = in.h_render_context;
	req.width = in.width;
	req.height = in.height;
	req.n_entries = in.n_entries;
	req.target_va = rctx->target_va;
	ret = mt_ta_submit_real(file, &req, &fence);
	if (ret)
		goto out_free;
	waited = dma_fence_wait_timeout(fence, false, msecs_to_jiffies(5000));
	dma_fence_put(fence);
	if (waited <= 0) {
		ret = waited ? (int)waited : -ETIMEDOUT;
		goto out_free;
	}
	g = pvr_session_acquire(&owner);
	if (!g) {
		ret = -ENODEV;
		goto out_free;
	}
	d = container_of(g, struct mt_guest_device, state);
	ret = pvr_translator_bo_read(d, &rctx->target_bo, 0,
				     out->pixels, MT_T2_TARGET_BYTES);
	if (owner)
		module_put(owner);
	if (ret)
		goto out_free;
	out->status = 0;
	out->completion_code = MT_FW_TA_COMPLETE_CODE;
	ret = pvr_out(cmd, out, sizeof(*out));
out_free:
	kfree(out);
	return ret;
}
#endif

static int pvr_dispatch_rgxta3d(struct mt_pvr_file *file, u32 function,
			 struct mt_pvr_cmd *cmd)
{
	switch (function) {
	case MT_PVR_FN_RGXCREATEZSBUFFER:			/* RGXCreateZSBuffer */
		return pvr_cmd_zs_create(file, cmd);
	case MT_PVR_FN_RGXDESTROYZSBUFFER:			/* RGXDestroyZSBuffer */
		return pvr_cmd_zs_destroy(file, cmd);
	case MT_PVR_FN_RGXCREATERENDERCONTEXT:			/* RGXCreateRenderContext */
		return pvr_cmd_handle_only(file, cmd,
					    MT_PVR_KIND_CONTEXT);
	case MT_PVR_FN_RGXDESTROYRENDERCONTEXT:			/* RGXDestroyRenderContext */
		/* The UMD always tears the context down, even when
		 * creation itself failed partway, so refusing this
		 * with -ENOTTY leaves the teardown incomplete.
		 */
		return pvr_cmd_handle_release(file, cmd,
					      MT_PVR_KIND_CONTEXT);
	case MT_PVR_FN_MUSAKICKGFX2:	/* MUSAKickGFX2 (real TA dispatch, r367) */
		return pvr_cmd_musakickgfx2(file, cmd);
	case MT_PVR_FN_RGXCREATERENDERCONTEXT2:			/* BridgeRGXCreateRenderContext2 (DDK2) */
		return pvr_cmd_render2_create(file, cmd);
	case MT_PVR_FN_RGXDESTROYRENDERCONTEXT2:			/* BridgeRGXDestroyRenderContext2 (DDK2) */
		return pvr_cmd_handle_release(file, cmd,
					      MT_PVR_KIND_CONTEXT);
	case MT_PVR_FN_RGXKICKTA3D5:			/* RGXKickTA3D5 (accept-and-log, r215) */
		return pvr_cmd_kickta3d5_observe(file, cmd);
#if MT_TA_READBACK_DEBUG
	case MT_PVR_FN_DEBUGTAREADBACK:	/* DebugTAReadback (r416, gated) */
		return pvr_cmd_ta_readback(file, cmd);
#endif
	default:
		return -ENOTTY;
}
}

static int pvr_dispatch_rgxkicksync(struct mt_pvr_file *file, u32 function,
			 struct mt_pvr_cmd *cmd)
{
	switch (function) {
	case MT_PVR_FN_RGXCREATEKICKSYNCCONTEXT:			/* RGXCreateKickSyncContext */
		return pvr_cmd_kicksync_create(file, cmd);
	case MT_PVR_FN_RGXDESTROYKICKSYNCCONTEXT:			/* RGXDestroyKickSyncContext */
		return pvr_cmd_kicksync_destroy(file, cmd);
	case MT_PVR_FN_RGXKICKSYNC2:			/* RGXKickSync2 */
	case MT_PVR_FN_RGXSETKICKSYNCCONTEXTPROPERTY:			/* RGXSetKickSyncContextProperty */
	case MT_PVR_FN_RGXKICKSYNC3:			/* RGXKickSync3 (TA submit) */
		return pvr_cmd_kicksync_submit(file, cmd, function);
	case MT_PVR_FN_RGXCREATEKICKSYNCCONTEXT2:			/* BridgeRGXCreateKickSyncContext2 (DDK2) */
		return pvr_cmd_kicksyncctx2_create(file, cmd);
	case MT_PVR_FN_RGXDESTROYKICKSYNCCONTEXT2:			/* BridgeRGXDestroyKickSyncContext2 (DDK2) */
		return pvr_cmd_kicksync_destroy(file, cmd);
	default:
		return -ENOTTY;
}
}

static int pvr_dispatch_rgxhwperf(struct mt_pvr_file *file, u32 function,
			 struct mt_pvr_cmd *cmd)
{
	switch (function) {
	case MT_PVR_FN_RGXACQUIREHWPERFFSETTINGS:		/* MUSA:MUSAAcquireHWPerfSettings */
		return pvr_cmd_hwperf(file, cmd);
	case MT_PVR_FN_RGXRELEASEHWPERFFSETTINGS:		/* MUSA:MUSAReleaseHWPerfSettings */
		return pvr_cmd_hwperf_release(file, cmd);
	default:
		return -ENOTTY;
}
}

static int pvr_dispatch_rgxtdm(struct mt_pvr_file *file, u32 function,
			 struct mt_pvr_cmd *cmd)
{
	switch (function) {
	case MT_PVR_FN_RGXTDMCREATETRANSFERCONTEXT2: /* RGXTDMCreateTransferContext2 */
		return pvr_cmd_tdm_context2_create(file, cmd);
	case MT_PVR_FN_RGXTDMDESTROYTRANSFERCONTEXT2: /* RGXTDMDestroyTransferContext2 */
		return pvr_cmd_tdm_context2_destroy(file, cmd);
	case MT_PVR_FN_RGXTDMGETSHAREDMEMORY:		/* RGXTDMGetSharedMemory */
		return pvr_cmd_tdm_shmem(file, cmd);
	case MT_PVR_FN_RGXTDMRELEASESHAREDMEMORY:		/* RGXTDMReleaseSharedMemory */
		return pvr_cmd_tdm_release(file, cmd);
	case MT_PVR_FN_RGXTDMSUBMITTRANSFER3:		/* RGXTDMSubmitTransfer3 (accept-and-log, r174) */
		return pvr_cmd_tdm_submit3_observe(file, cmd);
	default:
		return -ENOTTY;
}
}

static int pvr_bridge_dispatch(struct mt_pvr_file *file, u32 bridge,
			       u32 function, struct mt_pvr_cmd *cmd)
{
	if (!file->conn->srv_handle)
		return -ENOTCONN;

	switch (bridge) {
	case MT_PVR_BRIDGE_SRVCORE:
		return pvr_dispatch_srvcore(file, function, cmd);
	case MT_PVR_BRIDGE_SYNC:
		return pvr_dispatch_sync(file, function, cmd);
	case MT_PVR_BRIDGE_MM:
		return pvr_dispatch_mm(file, function, cmd);
	case MT_PVR_BRIDGE_RGXCOMPUTE:
		return pvr_dispatch_rgxcompute(file, function, cmd);
	case MT_PVR_BRIDGE_RGXTA3D:
		return pvr_dispatch_rgxta3d(file, function, cmd);
	case MT_PVR_BRIDGE_RGXKICKSYNC:
		return pvr_dispatch_rgxkicksync(file, function, cmd);
	case MT_PVR_BRIDGE_RGXHWPERF:
		return pvr_dispatch_rgxhwperf(file, function, cmd);
	case MT_PVR_BRIDGE_RGXTDM:
		return pvr_dispatch_rgxtdm(file, function, cmd);
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
		for (i = 0; i < pages; i++) {
			/* vmalloc pages are virtually contiguous, but their struct page
			 * entries need not be. Resolve each page independently and fail
			 * through the shared exit if the backing is unexpectedly absent.
			 */
			page = vmalloc_to_page((u8 *)pmr->host + (i << PAGE_SHIFT));
			if (!page) {
				ret = -EFAULT;
				break;
			}
			/* PAGE_KERNEL is a *kernel* pgprot: its _PAGE_USER bit
			 * is clear, so the resulting PTE is not reachable from
			 * user space and the first read faults (observed as a
			 * segfault at info_page+0x48). Or in _PAGE_USER to make
			 * it a genuine userspace mapping.
			 */
			ret = remap_pfn_range(vma,
					vma->vm_start + (i << PAGE_SHIFT),
					page_to_pfn(page), PAGE_SIZE,
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
	pvr_translator_exit();
	WRITE_ONCE(pvr_ready, false);
	if (pvr_drm) {
		drm_dev_unregister(pvr_drm);
		drm_dev_put(pvr_drm);
		pvr_drm = NULL;
	}
	pr_info("mt_pvr_bridge: unloaded cleanly\n");
}

/* ---- r366: TA submission op export (R2b phase 1) ---- */
/* Entry point for the real submit_ta_work op. Runs in the bridge's context
 * (THIS_MODULE=mt_pvr_bridge) so pending TA fences pin the bridge, never a
 * short-lived verifier. See the ABI WARNING on struct mt_marker_ops in
 * kernel/mt_marker_fence.h: do not call s->ops->submit_ta_work on stores
 * initialized by pre-r366 probe builds. */
int mt_bridge_submit_ta_work(struct mt_marker_store *s, struct mt_ta_work *work,
			     struct dma_fence **out)
{
	return mt_marker_submit_ta_work(s, work, out);
}
EXPORT_SYMBOL_GPL(mt_bridge_submit_ta_work);
#if MT_TA_REAL_PACKET
/* r415: Production real-TA submit with bridge-constructed command buffer.
 *
 * Builds a 360B TA command buffer from req->width/height/n_entries,
 * stages it in firmware-visible memory
 * (BO[MT_TA_REAL_STAGING_BO_INDEX]@MT_TA_REAL_STAGING_BO_OFFSET),
 * and submits via the real TA path (mt_bridge_submit_ta_work).
 *
 * Unlike the UMD-driven 0x82:0xC path (where the client provides
 * ta_cmd_va via IN), this constructs the buffer bridge-side for
 * validation without a UMD (r407/r408: no DDK2 UMD on Linux;
 * r414 validated the layout live, 0x100 in 219us).
 *
 * Async: returns 0 with *out_fence on success. The caller owns the
 * fence reference and must wait (pvr_ta_wait_complete) or abandon it;
 * the fence signals on firmware completion (0x100).
 *
 * Staging [MEASURED] (r414): per-context VM is sealed after creation
 * (bind -> -EBUSY), so BO[10]@4096 is reused. See
 * MT_TA_REAL_STAGING_* in kernel/mt_ta_real.h.
 *
 * Gated by MT_TA_REAL_PACKET (default 0): when off, this function
 * does not exist (zero code, zero risk).
 */
/* __maybe_unused: no in-tree caller yet; the API is for future
 * validation ioctls. Remove when the first caller lands. */
__maybe_unused static int mt_ta_submit_real(struct mt_pvr_file *file,
			     const struct mt_ta_real_request *req,
			     struct dma_fence **out_fence)
{
	struct mt_pvr_object *robj;
	struct mt_pvr_render_context *rctx;
	struct mt_guest *g;
	struct mt_guest_device *d;
	struct mt_marker_store *s;
	struct mt_ta_work work;
	struct module *owner = NULL;
	unsigned char *ta_buf;
	u64 ta_va;
	int ret;

	if (!file || !req || !out_fence)
		return -EINVAL;
	if (req->n_entries == 0 ||
	    req->n_entries > MT_TA_REAL_MAX_ENTRIES)
		return -EINVAL;
	/* width/height range checked by mt_ta_real_buffer_build. */
	*out_fence = NULL;

	robj = pvr_object_find(file, req->h_render_context,
				MT_PVR_KIND_CONTEXT);
	if (!robj || !robj->render_ctx)
		return -EINVAL;
	rctx = robj->render_ctx;
	if (!rctx->resources_ready || !rctx->vm || !rctx->exec_ta_ready)
		return -ENODEV;
	if (!rctx->bos_ready[MT_TA_REAL_STAGING_BO_INDEX])
		return -ENODEV;

	ta_buf = kzalloc(MT_TA_CMD_BUFFER_BYTES, GFP_KERNEL);
	if (!ta_buf)
		return -ENOMEM;
	ret = mt_ta_real_buffer_build(ta_buf, req->width, req->height,
				      req->n_entries, req->target_va);
	if (ret)
		goto out_free;

	g = pvr_session_acquire(&owner);
	if (!g) {
		ret = -ENODEV;
		goto out_free;
	}
	d = container_of(g, struct mt_guest_device, state);
	s = &d->markers;

	ta_va = rctx->vas[MT_TA_REAL_STAGING_BO_INDEX] +
		MT_TA_REAL_STAGING_BO_OFFSET;
	ret = pvr_translator_bo_write(d,
				      &rctx->bos[MT_TA_REAL_STAGING_BO_INDEX],
				      MT_TA_REAL_STAGING_BO_OFFSET,
				      ta_buf,
				      MT_TA_CMD_BUFFER_BYTES);
	if (ret)
		goto out_session;

	memset(&work, 0, sizeof(work));
	work.job.state = MT_JOB_HELD;
	work.context = &rctx->exec_ctx_ta;
	work.params.ta_cmd_va = ta_va;
	work.params.ta_cmd_size = MT_TA_CMD_BUFFER_BYTES;
	/* kick_flags/ta_upd_count/ta_fence_count stay 0: pass validation. */

	mutex_lock(&g->trial_lock);
	if (!s->can_submit) {
		mutex_unlock(&g->trial_lock);
		ret = -EHOSTDOWN;
		goto out_session;
	}
	if (s->ready) {
		mutex_unlock(&g->trial_lock);
		ret = -EBUSY;
		goto out_session;
	}
	s->ready = true;
	mutex_unlock(&g->trial_lock);

	ret = mt_bridge_submit_ta_work(s, &work, out_fence);

	mutex_lock(&g->trial_lock);
	s->ready = false;
	mutex_unlock(&g->trial_lock);
	if (ret)
		*out_fence = NULL;

out_session:
	if (owner)
		module_put(owner);
out_free:
	kfree(ta_buf);
	return ret;
}
#endif /* MT_TA_REAL_PACKET */
/* Bridge-exported 3D submit entry (r382, R3). Mirrors
 * mt_bridge_submit_ta_work; gated by MT_3D_SUBMIT_GATE (default 0). */
int mt_bridge_submit_3d_work(struct mt_marker_store *s, struct mt_3d_work *work,
			     struct dma_fence **out)
{
	return mt_marker_submit_3d_work(s, work, out);
}
EXPORT_SYMBOL_GPL(mt_bridge_submit_3d_work);

module_init(pvr_start);
module_exit(pvr_stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Minimal PVR Services bridge for the legacy MASA UMD");
