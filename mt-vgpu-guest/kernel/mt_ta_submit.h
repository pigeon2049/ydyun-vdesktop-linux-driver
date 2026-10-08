/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_TA_SUBMIT_H
#define MT_GUEST_TA_SUBMIT_H

/* R4 design (r364): TA firmware submission channel interface.
 *
 * Motivation [MEASURED]: 0x82:0xC (MUSAKICKGFX2) is a real TA/PR kick --
 * r363 observed kick_ta=1/kick_pr=1/kick_3d=0 with a 268B IN on live
 * hardware; the bridge observer returns -ENOTTY without executing.
 * Real execution (R2b) needs a probe-side channel submitting TA work to
 * the firmware. r355 R4: "probe 仅有 submit_tqx_work，无 TA submit op".
 *
 * Template [MEASURED]: submit_tqx_work (kernel/mt_marker_fence.h) --
 * validate a prepared work owner, allocate a marker fence, move the job,
 * patch wire_id at packet+0x48, mt_fw_queue_try_submit(queue, dm, 0,
 * packet); the dma_fence signals on firmware completion-event match
 * (mt_marker_complete).
 *
 * Firmware queue [MEASURED]: 6 DMs x 0x2e30 bytes, 0x50-byte commands,
 * per-DM producer spinlock, kick = writel(dm, registers+0xb00).
 * dm=1 TQX/transfer, dm=2 3D Universal, dm=0 META/system.
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

#include "mt_pvr_wire.h"	/* struct mt_pvr_musakickgfx2_in for r367 mapping */

/* D2 [INFERRED -> TO-VALIDATE]: TA data master.
 * dm=1 (TQX) and dm=2 (3D) are taken by live code; dm=0 is META/system
 * (excluded from idle checks). TA is a distinct engine, so dm=3.
 * The marker framework already iterates dm 1..5, so dm=3 is structurally
 * supported. If firmware rejects it, fall back to probing dm 4/5. */
#define MT_FW_DM_TA 3U

/* D4 [INFERRED -> TO-VALIDATE]: opcode for the 0x50-byte firmware command
 * (+0x0c). 0x66 = RGXVertex/UniversalQueue in mt_work_opcode()
 * (kernel/mt_work_command.h); TA consumes vertex/tile work. If the
 * firmware NAKs it, capture the Windows KMD's TA opcode as follow-up. */
#define MT_FW_TA_OPCODE 0x66U

/* r365 [MEASURED -> TO-VALIDATE fine semantics]: 0x66-class firmware
 * completion code (vs standard COMPLETE=0). The frozen probe's
 * mt_fw_event_matches() rejects it; TA fences complete via the TA-aware
 * path (r366, kernel/mt_marker_fence.h). */
#define MT_FW_TA_COMPLETE_CODE 0x100U

/* Kick flag bits for mt_ta_submit_params.kick_flags, from IN bbKickTA /
 * bbKickPR (r363 live: kick_ta=1, kick_pr=1, kick_3d=0). */
#define MT_TA_KICK_TA (1U << 0)
#define MT_TA_KICK_PR (1U << 1)

/* D3/D5 [MEASURED layout, INFERRED use]: decoded 0x82:0xC IN subset the TA
 * submit path needs. Field sources cite r363's 45-field table against
 * struct mt_pvr_musakickgfx2_in (kernel/mt_pvr_wire.h).
 * All userspace pointers are captured at decode time; the submit path
 * only sees kernel-side copies (see D8 in the r364 report). */
struct mt_ta_submit_params {
	u64 ta_cmd_va;		/* p_ta_cmd: TA cmd buffer, firmware-visible VA */
	u32 ta_cmd_size;	/* ta_cmd_size (360 in r363) */
	u32 kick_flags;		/* MT_TA_KICK_* */
	u64 ta_upd_sync_off;	/* p_client_ta_upd_sync_off */
	u64 ta_upd_val;		/* p_client_ta_upd_val */
	u64 ta_upd_block;	/* ph_client_ta_upd_block */
	u32 ta_upd_count;	/* client_ta_upd_count (1 in r363) */
	u64 ta_fence_sync_off;	/* p_client_ta_fence_sync_off */
	u64 ta_fence_val;	/* p_client_ta_fence_val */
	u64 ta_fence_block;	/* ph_client_ta_fence_block */
	u32 ta_fence_count;	/* client_ta_fence_count (0 in r363) */
	u64 pr_fence_block;	/* h_pr_fence_ufo_block (0x102d in r363) */
	u32 pr_fence_offset;	/* pr_fence_ufo_sync_offset */
	u32 pr_fence_value;	/* pr_fence_value (1 in r363) */
	s32 check_fence;	/* check_fence: input dma_fence dependency */
};

/* D5 [r367]: map a decoded 0x82:0xC (MUSAKickGFX2) IN to TA submit params.
 * Pure function (no locks, no hardware); the bridge dispatch calls it
 * before mt_bridge_submit_ta_work(). Field sources cite r363's 45-field
 * table against struct mt_pvr_musakickgfx2_in (kernel/mt_pvr_wire.h).
 * Userspace pointers are captured at decode time (D8); the submit path
 * only sees kernel-side copies. */
static inline void
mt_ta_params_from_musakickgfx2(struct mt_ta_submit_params *p,
			       const struct mt_pvr_musakickgfx2_in *in)
{
	*p = (struct mt_ta_submit_params){ 0 };
	p->ta_cmd_va = in->p_ta_cmd;
	p->ta_cmd_size = in->ta_cmd_size;
	if (in->kick_ta)
		p->kick_flags |= MT_TA_KICK_TA;
	if (in->kick_pr)
		p->kick_flags |= MT_TA_KICK_PR;
	p->ta_upd_sync_off = in->p_client_ta_upd_sync_off;
	p->ta_upd_val = in->p_client_ta_upd_val;
	p->ta_upd_block = in->ph_client_ta_upd_block;
	p->ta_upd_count = in->client_ta_upd_count;
	p->ta_fence_sync_off = in->p_client_ta_fence_sync_off;
	p->ta_fence_val = in->p_client_ta_fence_val;
	p->ta_fence_block = in->ph_client_ta_fence_block;
	p->ta_fence_count = in->client_ta_fence_count;
	p->pr_fence_block = in->h_pr_fence_ufo_block;
	p->pr_fence_offset = in->pr_fence_ufo_sync_offset;
	p->pr_fence_value = in->pr_fence_value;
	p->check_fence = in->check_fence;
}

/* D1 [INFERRED]: new marker op, parallel to submit_tqx_work
 * (kernel/mt_marker_fence.h). Independent -- not a branch inside
 * submit_tqx_work -- because TA differs in DM, work struct, validation
 * (route.dm) and command construction; the proven TQX path stays
 * untouched. Added as a 5th mt_marker_ops entry at implementation:
 *
 *   int (*submit_ta_work)(struct mt_marker_store *s,
 *                         struct mt_ta_work *work,
 *                         struct dma_fence **out);
 *
 * struct mt_ta_work (implementation sketch, mirrors struct mt_tqx_work):
 *   struct mt_work_job job;               -- owned command packet + BOs
 *   struct mt_execution_context *context;  -- route.dm must equal MT_FW_DM_TA
 *   struct mt_pool_slice *pool_slices[3];  -- borrowed, context-owned
 *   struct mt_ta_submit_params params;     -- decoded IN subset above
 *
 * D6/D7 (fence + errors) follow submit_tqx_work verbatim: validate before
 * any hardware write, -EAGAIN on queue-full with ownership preserved,
 * restore on submit failure, never reuse wire IDs. See r364 report.
 */

/* ---- gate: layout pins (r364) ---- */
static_assert(sizeof(struct mt_ta_submit_params) == 104,
	      "mt_ta_submit_params size");
static_assert(__builtin_offsetof(struct mt_ta_submit_params, ta_cmd_va) == 0,
	      "mt_ta_submit_params.ta_cmd_va");
static_assert(__builtin_offsetof(struct mt_ta_submit_params, ta_upd_count) == 40,
	      "mt_ta_submit_params.ta_upd_count");
static_assert(__builtin_offsetof(struct mt_ta_submit_params, check_fence) == 96,
	      "mt_ta_submit_params.check_fence");
static_assert(MT_FW_DM_TA == 3U, "MT_FW_DM_TA");
static_assert(MT_FW_DM_TA > 2U, "TA DM must not collide with TQX(1)/3D(2)");

#endif /* MT_GUEST_TA_SUBMIT_H */
