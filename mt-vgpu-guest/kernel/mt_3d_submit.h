/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_3D_SUBMIT_H
#define MT_GUEST_3D_SUBMIT_H

/* R3 design (r382): 3D firmware submission channel interface.
 *
 * Motivation [MEASURED]: 0x82:0x14 (MUSAKICKGFX5) is a real 3D kick --
 * r379 observed the bridge accept-and-logs it (r215 observer) without
 * executing. r381 determined the 3D opcode is 0x68 (RGXCompute, type 5
 * -> DM2), completion code is standard 0 (not TA's 0x100), and DM2
 * requires a complete command packet (empty markers are ignored, r380).
 *
 * Template [MEASURED]: submit_ta_work (kernel/mt_marker_fence.h, r366) --
 * validate a prepared work owner, allocate a marker fence, build the
 * command packet, mt_fw_queue_try_submit(queue, dm, 0, packet); the
 * dma_fence signals on firmware completion-event match
 * (mt_marker_complete, standard code 0).
 *
 * Each decision below is tagged:
 *   [MEASURED]   code / corpus / live evidence
 *   [INFERRED]   design choice by analogy, needs live proof
 *   [TO-VALIDATE] explicitly deferred to a live round
 */

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <assert.h>	/* static_assert */
#include <stdint.h>
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
#endif

#include "mt_pvr_wire.h"	/* struct mt_pvr_rgxkickta3d5_in for mapping */

/* [MEASURED]: 3D data master is DM2. mt_work_command.h routes type 5
 * (RGXCompute) to DM2; r381 confirmed opcode 0x68 for 3D. */
#define MT_FW_DM_3D 2U

/* [MEASURED] (r381): opcode for the 0x50-byte firmware command (+0x0c).
 * 0x68 = RGXCompute. 'MUSAKICKGFX5' name implies work type 5.
 * 0x66 (RGXVertex) works on DM2 only with real command packets
 * (mt_live_3d.c, r37-r41); empty markers are ignored (r380). */
#define MT_FW_3D_OPCODE 0x68U

/* [MEASURED] (r381): 3D uses the standard COMPLETE code (0).
 * No 3D-specific completion code is defined (unlike TA's 0x100). */
#define MT_FW_3D_COMPLETE_CODE 0U

/* r382: 3D submit gate. 0 = disabled (default). submit_3d_work returns
 * -EOPNOTSUPP until this is flipped to 1. Enable only after live
 * validation of the 0x68 path (r381 TO-VALIDATE). The 0x82:0x14
 * dispatch stays on the r215 observer until then. */
#define MT_3D_SUBMIT_GATE 0

/* Decoded 0x82:0x14 IN subset the 3D submit path needs.
 * Field sources cite struct mt_pvr_rgxkickta3d5_in
 * (kernel/mt_pvr_wire.h, r379).
 * All userspace pointers are captured at decode time; the submit path
 * only sees kernel-side copies. */
struct mt_3d_submit_params {
	u64 submission_va;	/* submission_va: 3D cmd buffer, firmware-visible VA */
	u64 submission_id;	/* submission_id */
	u64 render_context;	/* render_context handle */
	u32 submission_size;	/* submission_size */
	u32 submission_flags;	/* submission_flags */
	u32 check_count;	/* check_count */
	u32 update_count;	/* update_count */
	u32 sync_pmr_count;	/* sync_pmr_count */
	s32 check_fence;	/* check_fence: input dma_fence dependency (0 = none) */
	void *vm_map_hook;	/* R5 hook (r382): per-file VM mapping for
				 * submission_va. Reserved; the submit path
				 * does not dereference it yet. */
};

/* Map a decoded 0x82:0x14 (MUSAKickGFX5) IN to 3D submit params.
 * Pure function (no locks, no hardware); the bridge dispatch will call it
 * before mt_bridge_submit_3d_work() once the gate opens.
 * 0x82:0x14 has no IN check_fence field (unlike 0x82:0xC); check_fence is
 * initialized to 0 (no dependency), reserved for future use. */
static inline void
mt_3d_params_from_rgxkickta3d5(struct mt_3d_submit_params *p,
			       const struct mt_pvr_rgxkickta3d5_in *in)
{
	*p = (struct mt_3d_submit_params){ 0 };
	p->submission_va = in->submission_va;
	p->submission_size = in->submission_size;
	p->submission_id = in->submission_id;
	p->submission_flags = in->submission_flags;
	p->render_context = in->render_context;
	p->check_count = in->check_count;
	p->update_count = in->update_count;
	p->sync_pmr_count = in->sync_pmr_count;
	p->check_fence = 0;
	p->vm_map_hook = NULL;
}

/* ---- gate: layout pins (r382) ---- */
static_assert(sizeof(struct mt_3d_submit_params) == 56,
	      "mt_3d_submit_params size");
static_assert(__builtin_offsetof(struct mt_3d_submit_params, submission_va) == 0,
	      "mt_3d_submit_params.submission_va");
static_assert(__builtin_offsetof(struct mt_3d_submit_params, submission_size) == 24,
	      "mt_3d_submit_params.submission_size");
static_assert(__builtin_offsetof(struct mt_3d_submit_params, check_fence) == 44,
	      "mt_3d_submit_params.check_fence");
static_assert(__builtin_offsetof(struct mt_3d_submit_params, vm_map_hook) == 48,
	      "mt_3d_submit_params.vm_map_hook");
static_assert(MT_FW_DM_3D == 2U, "MT_FW_DM_3D");
static_assert(MT_FW_3D_OPCODE == 0x68U, "MT_FW_3D_OPCODE");
static_assert(MT_3D_SUBMIT_GATE == 0, "MT_3D_SUBMIT_GATE default off");

#endif /* MT_GUEST_3D_SUBMIT_H */
