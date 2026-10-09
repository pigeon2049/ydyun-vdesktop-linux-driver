/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_RENDER_CONTEXT_H
#define MT_GUEST_RENDER_CONTEXT_H

#include "mt_bo.h"
#include "mt_gfx_context.h"
#include "mt_execution_context.h"
#include <linux/types.h>

/* Forward: defined in kernel/recovery/mt_pvr_bridge.c (r376).
 * Only used as a pointer here, so the full definition is not needed. */
struct mt_bridge_ta_vm;

/* Per-context VA stride (r387 §3.3).
 * TO-VALIDATE: 16MB is an engineering estimate (86KB BOs + headroom);
 * confirm against firmware VA limits during R6-2 live validation. */
#define MT_RENDER_CONTEXT_VA_STRIDE (16U << 20)

/* Per-context real state (R6 Route A, r387 §1.2, r388).
 * Mounted on struct mt_pvr_object.render_ctx (NULL = uninitialized or
 * non-CONTEXT kind). Allocated at 0x82:0x12 create (R6-2), freed at
 * 0x82:0x13 destroy (R6-3). Pure structure in R6-1; no logic. */
struct mt_pvr_render_context {
	/* 11 BOs: mt_gfx_context_bo_specs, 86,300B total (mt_live_3d.c). */
	struct mt_bo bos[MT_GFX_CONTEXT_BO_COUNT];
	u64 vas[MT_GFX_CONTEXT_BO_COUNT];
	bool bos_ready[MT_GFX_CONTEXT_BO_COUNT];

	/* Firmware execution state (mt_live_3d.c:296). */
	struct mt_execution_process process;
	struct mt_execution_context exec_ctx_3d;  /* node_type=5 -> DM2 (r389) */
	struct mt_execution_context exec_ctx_ta;  /* node_type=2 -> DM3 (r397) */
	bool exec_ready;      /* exec_ctx_3d created */
	bool exec_ta_ready;   /* exec_ctx_ta created */

	/* CSW: 248B (mt_gfx_context_build_csw). */
	u8 csw[MT_GFX_CONTEXT_CSW_BYTES];

	/* Per-context VM (R5 evolution, r387 §3). */
	struct mt_bridge_ta_vm *vm;
	u64 vm_base_va;
	/* r416: T2 render target (12th BO, RGBA8 64x64). Created+bound at
	 * create (before exec process; VM refuses binds once active_uses>0),
	 * released at destroy. target_va feeds pixel readback (0x82:0xFD OUT). */
	struct mt_bo target_bo;
	u64 target_va;
	bool target_ready;
	/* r431: RgnHeader (13th BO). TA Header +0x10 = RgnHeader device VA
	 * ([MEASURED] r430, 3-hop chain). 64x64: 0x100B, pre-filled
	 * 0xFFFFFFFF (InitRegionHeaderBuffer). Created+bound at create,
	 * released at destroy. rgnheader_va feeds TA Header+0x10. */
	struct mt_bo rgnheader_bo;
	u64 rgnheader_va;
	bool rgnheader_ready;
	/* Page-table BO for the VM (d->buffers-backed, r389). Must outlive vm. */
	struct mt_bo pt_bo;

	/* State flag: set true once all resources are ready (R6-2). */
	bool resources_ready;
};

#endif /* MT_GUEST_RENDER_CONTEXT_H */
