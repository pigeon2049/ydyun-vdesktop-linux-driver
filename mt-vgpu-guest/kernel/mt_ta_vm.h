/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * mt_ta_vm.h — Per-file GPU VM/BO backend for TA command buffers (R5, r374).
 *
 * Design only (r374): interface definitions and static assertions, no
 * implementation logic. The implementation is a future round's work.
 *
 * Background:
 * - r372/r373: end-to-end TA path works at marker level (zero-draw).
 * - r364 D8: real TA payload needs the TA command buffer in firmware-visible
 *   GPU memory, via mt_bo_system_borrow() into a per-file GPU VM.
 * - r208: the current `store=file` facade (pvr_gpu_plan_bo_ops, CPU-only)
 *   cannot enter the device session VM. R5 must use the real device BO
 *   store with real page tables, mirroring the TQX path's c->process->vm.
 *
 * Status labels used in comments:
 *   [MEASURED] — backed by live evidence or existing working code.
 *   [INFERRED] — design deduction, needs live validation.
 *   [TO-VALIDATE] — must be proven on hardware before use.
 */

#ifndef MT_TA_VM_H
#define MT_TA_VM_H

#include <linux/types.h>

/* [MEASURED] TA command buffer reservation in per-file GPU VA space.
 * r363 measured ta_cmd_size=360. Reserve 4KiB (one page) per TA kick;
 * the VA base is per-file so concurrent UMD contexts cannot collide.
 * [INFERRED] Exact base address; must avoid TQX/3D reservation ranges.
 * [TO-VALIDATE] Firmware acceptance of this VA range for TA commands.
 */
#define MT_TA_CMD_VA_BASE	0x70000000ULL
#define MT_TA_CMD_VA_SIZE	0x1000ULL	/* 4KiB per kick */

/* [MEASURED] Mirrors the TQX validation gate (mt_marker_fence.h:246):
 * work may submit only when the VM is sealed and page tables uploaded.
 * The TA path currently skips this gate (marker-level, no VM needed).
 * [INFERRED] Gate opens per-file once that file's VM is sealed+uploaded.
 */
#define MT_TA_VM_READY(vm)	((vm) && (vm)->sealed && (vm)->uploaded)

/*
 * struct mt_ta_vm_context — Per-file TA VM context (R5).
 *
 * [INFERRED] Lifecycle modeled on the TQX path's c->process->vm:
 * - Created on first TA submit for a file descriptor (bridge ioctl context).
 * - Destroyed on file close (release callback).
 * - One VM per file: independent GPU VA spaces isolate UMD contexts.
 *
 * [MEASURED] struct mt_gpu_vm and struct mt_bo already exist and are used
 * by the TQX path. This struct only binds them to a file lifetime.
 *
 * [TO-VALIDATE] File-close teardown ordering vs in-flight TA markers.
 */
struct mt_ta_vm_context {
	/* Real device-store VM (never the store=file facade). */
	struct mt_gpu_vm *vm;
	/* Page-table BOs in the device BO store (for upload). */
	struct mt_bo *page_tables;
	/* Owning file; used for per-file lookup, not dereferenced. */
	void *owner_file;
	/* Number of TA command buffers currently bound in this VM. */
	u32 bound_cmd_buffers;
	/* Set once vm->sealed && vm->uploaded (gate open for this file). */
	bool ready;
};

/*
 * struct mt_ta_cmd_mapping — One TA command buffer's GPU mapping.
 *
 * [INFERRED] Flow (each step must precede any hardware write, D7):
 *  1. pin_user_pages() on p_ta_cmd..p_ta_cmd+ta_cmd_size (userspace VA).
 *  2. Build mt_system_memory {cpu, page_pa[], bytes} from pinned pages.
 *  3. mt_bo_system_borrow() into the device BO store (s->lock held).
 *     [MEASURED] This function exists (mt_bo_vram.h:149) and r208 proved
 *     mt_gpu_vm_bind_many() accepts borrowed BOs.
 *  4. mt_gpu_vm_bind_many() at MT_TA_CMD_VA_BASE + slot*MT_TA_CMD_VA_SIZE.
 *  5. If VM not sealed: seal + upload page tables (firmware publish).
 *  6. gpu_va (below) goes into the TA firmware command packet.
 *  7. On completion: unpin pages, release borrowed BO; VM persists.
 *
 * [TO-VALIDATE] Steps 4-6 on live hardware (firmware VA acceptance,
 * page-table upload correctness, per-file isolation).
 */
struct mt_ta_cmd_mapping {
	/* Firmware-visible GPU VA of the bound command buffer. */
	u64 gpu_va;
	/* Size in bytes (from ta_cmd_size, page-aligned up for pin). */
	u32 size;
	/* Borrowed BO in the device store (released on unmap). */
	struct mt_bo *borrowed_bo;
	/* Pinned userspace pages (unpinned on unmap). */
	void *pinned_pages;
	u32 nr_pages;
	/* Back-pointer to the per-file VM context (not owned). */
	struct mt_ta_vm_context *vm_ctx;
};

/* Static assertions: interface layout stability (r374 gate). */
_Static_assert(sizeof(struct mt_ta_vm_context) % 8 == 0,
	       "mt_ta_vm_context must stay 8-byte aligned");
_Static_assert(MT_TA_CMD_VA_SIZE == 4096,
	       "TA cmd reservation must be one page");
_Static_assert(MT_TA_CMD_VA_BASE + MT_TA_CMD_VA_SIZE > MT_TA_CMD_VA_BASE,
	       "TA cmd VA range must not wrap");

#endif /* MT_TA_VM_H */
