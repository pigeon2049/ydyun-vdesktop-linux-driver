/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_TA_REAL_H
#define MT_GUEST_TA_REAL_H

/* r411: Real TA packet construction infrastructure.
 *
 * Motivation [MEASURED]: r406 proved 3D firmware ignores marker packets
 * ("submitted-but-ignored"); r407/r408 proved RGXSubmitTA only passes
 * through the client-provided TA buffer VA (709 lines, zero writes);
 * r410 extracted TA buffer field semantics from linux-legacy-umd-5.2.0
 * FUN_00169240 (40B simple / 72B complex entries).
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
#endif

/* r411: Real TA packet gate. 0 = disabled (default). The TA submit path
 * builds marker packets until this is flipped to 1 after live validation
 * of the real packet layout. Marker path is unaffected. */
#define MT_TA_REAL_PACKET 0

/* [MEASURED] (r410, r408): TA command buffer size. 0x168=360 is an
 * immediate size parameter in RGXSubmitTA (decompiled.c:52614). */
#define MT_TA_CMD_BUFFER_BYTES 0x168U

/* [MEASURED] (r410, FUN_00169240): Simple TA entry = 5 qwords = 40B.
 * Complex entry = 9 qwords = 72B (adds scissor/viewport). */
#define MT_TA_ENTRY_SIMPLE_QWORDS 5U
#define MT_TA_ENTRY_SIMPLE_BYTES 40U
#define MT_TA_ENTRY_COMPLEX_QWORDS 9U
#define MT_TA_ENTRY_COMPLEX_BYTES 72U

/* [MEASURED] (r414 live): DM packet offsets for TA buffer VA/size.
 * Firmware accepted VA@+0x28/+0x2c, size@+0x30 and completed 0x100
 * in 219us (dmesg-r414.txt). The 3D analogy (r411) is confirmed. */
#define MT_TA_DM_PKT_TA_VA_LO 0x28U
#define MT_TA_DM_PKT_TA_VA_HI 0x2cU
#define MT_TA_DM_PKT_TA_SIZE  0x30U

/* [MEASURED] (r422, FUN_001796b0:54365-54374): 360B TA buffer is a
 * Header + Entries dual-zone structure (NOT a flat entry array).
 * - Header covers 0x00-0x160. RGXSubmitTA reads 9 qwords from
 *   +0x10/+0x18/+0x20/+0x28/+0x30/+0x38/+0x40/+0x48/+0x60 into psKickTA.
 * - RGXPrepareTA writes Header at +0x10/+0x28/+0x30/+0x68/+0x78/+0xb0/+0xe8
 *   (8B VA/pointer) and +0x120/+0x140/+0x150/+0x160 (4B flags).
 * - Entries do NOT live in this 360B buffer (544B buffer inferred, r422).
 * - r422 root-cause: writing 40B Entries at buf+0 polluted Header fields
 *   at 0x10/0x18/0x20 (Entry Q2/Q3/Q4 overlap) -> firmware timeout (r421).
 * - r414 proved all-zero Header completes ("no work", 219us).
 */
#define MT_TA_BUF_HDR_TARGET_VA 0x10U /* Header+0x10 -> psKickTA[1] render target VA */

/* [MEASURED] (r414): Complete 80B TA DM packet layout (MT_FW_COMMAND_BYTES).
 * +0x0c: opcode = 0x66 (r365)
 * +0x28: TA buffer VA, low 32 bits (little-endian)
 * +0x2c: TA buffer VA, high 32 bits
 * +0x30: TA buffer size in bytes (0x168 = 360)
 * +0x48: wire_id (fence)
 * +0x4c: pid
 * All other bytes zero. */



/* [MEASURED] (r410, FUN_00169240:44293-44332): Simple TA entry (5 qwords).
 * Q0: address/flags. Q1: *(param_1+0x10). Q2: packed dimensions
 *     ((w-1)&0x7fff)<<0x29 | ((h-1)&0x7fff)<<0x1a. Q3: *(lVar29+8).
 *     Q4: (w*h-1) or packed dims. Byte-level flag overlays omitted;
 *     minimal construction only. */
struct mt_ta_entry_simple {
	u64 q0_addr_flags;
	u64 q1;
	u64 q2_dims;
	u64 q3;
	u64 q4;
};

/* Build a minimal 40B simple TA entry (r411).
 * [MEASURED]: Q2 dimension packing from FUN_00169240:44304.
 * [INFERRED]: Q0/Q1/Q3/Q4 zero-filled; firmware may require valid
 *             addresses. TO-VALIDATE on live hardware.
 * w, h: render target dimensions (e.g. 64x64 dummy).
 * Returns 0 on success, -EINVAL on bad input.
 */
static inline int mt_ta_entry_simple_build(struct mt_ta_entry_simple *e,
					   u32 w, u32 h)
{
	u64 qw, qh;

	if (!e || w == 0 || h == 0 || w > 0x8000 || h > 0x8000)
		return -22; /* -EINVAL */

	qw = (u64)((w - 1) & 0x7fff);
	qh = (u64)((h - 1) & 0x7fff);

	e->q0_addr_flags = 0;
	e->q1 = 0;
	/* [MEASURED] Q2 packing: ((w-1)&0x7fff)<<0x29 | ((h-1)&0x7fff)<<0x1a */
	e->q2_dims = (qw << 0x29) | (qh << 0x1a);
	e->q3 = 0;
	/* Q4: dimension product form */
	e->q4 = (u64)(w * h - 1);

	return 0;
}

/* [MEASURED] (r414): TA staging location within the render context.
 * The per-context VM is sealed after creation (mt_gpu_vm_bind_many
 * returns -EBUSY for new bindings, r412), so no new BOs can be mapped
 * post-create. BO[10] (8192B "Rasterisation context state") at offset
 * 4096 is reused; the first 4KB stays intact. Firmware read the 360B
 * correctly and completed 0x100 (r414).
 * [TO-VALIDATE]: BO[10] second-half firmware semantics under real
 * rasterisation load. Long-term fix: dedicate a TA staging BO at
 * context creation time (like the 11 BOs, r389). */
#define MT_TA_REAL_STAGING_BO_INDEX 10U
#define MT_TA_REAL_STAGING_BO_OFFSET 4096U

/* Maximum 40B entries that fit in the 360B buffer (9*40=360). */
#define MT_TA_REAL_MAX_ENTRIES 9U

/* r419 corrected (r410 was wrong): Q0 is flags-only. The OR expression
 * carries forward prev-entry flags; it never ORs in an address.
 * The 48-bit target VA lives in Q1 (decompiled.c:44300).
 * Constant 0x48000000000 = bits 39,42. [MEASURED] */
#define MT_TA_ENTRY_Q0_FLAG_BITS 0x48000000000ULL

/* r419: set TA entry target. Q0 is flags-only (FUN_00169240:44213/44317);
 * the 48-bit target VA goes in Q1 (uStack_88._0_6_=*(param_1+0x10),
 * decompiled.c:44300). r418 OR-ed VA into Q0 and firmware timed out. */
static inline void mt_ta_entry_simple_set_target(struct mt_ta_entry_simple *e,
						 u64 target_va)
{
	if (e) {
		/* r419: Q0 flags-only; addr in Q1 (decompiled.c:44300). */
		e->q0_addr_flags = MT_TA_ENTRY_Q0_FLAG_BITS;
		e->q1 = target_va & 0xFFFFFFFFFFFFULL;
	}
}

/* r416: T2 render target (12th BO, outside the 11-BO spec array).
 * 64x64 RGBA8. VA slot 11: vm_base_va + 11 * MT_RENDER_CONTEXT_VA_STRIDE. */
#define MT_T2_TARGET_WIDTH 64U
#define MT_T2_TARGET_HEIGHT 64U
#define MT_T2_TARGET_BYTES 16384U  /* 64*64*4 RGBA8 */
#define MT_T2_TARGET_BO_SLOT 11U

/* r416: T2 readback debug gate (default 0). Enables the 0x82:0xFD debug
 * ioctl (submit real TA + read back target pixels). Zero impact when off. */
#define MT_TA_READBACK_DEBUG 0

/* r415: Production real-TA submit request (parameterized).
 * Replaces the r414 test hook's hardcoded 64x64 single entry. */
struct mt_ta_real_request {
	u64 h_render_context;	/* render context handle */
	u32 width;		/* TA entry render-target width (1..0x8000) */
	u32 height;		/* TA entry render-target height (1..0x8000) */
	u32 n_entries;		/* number of 40B entries, 1..MT_TA_REAL_MAX_ENTRIES */
	u64 target_va;		/* r416: render target GPU VA for Q0, 0 = none */
};

/* Build a 360B TA command buffer, Header-only (r423).
 * [MEASURED] (r422): 360B = Header (0x00-0x160) + Entries dual-zone.
 * Entries do NOT belong in this buffer; writing them at buf+0 pollutes
 * Header fields at 0x10/0x18/0x20 and hangs firmware (r421).
 *
 * Header-only: zero the buffer, set TA_buf+0x10 = target_va
 * (-> psKickTA[1] render target VA, [MEASURED] r422). All other Header
 * fields stay zero; r414 proved all-zero Header completes ("no work").
 *
 * n_entries must be 0 (Header-only). Non-zero is rejected: the Entries
 * container is unknown (r422), and T5 forbids Entry writes in buf+0x00-0x68.
 * w/h are validated for API stability (unused in Header-only mode).
 * Returns 0 on success, -EINVAL on bad input.
 */
static inline int mt_ta_real_buffer_build(unsigned char *buf, u32 w, u32 h,
					  u32 n_entries, u64 target_va)
{
	if (!buf || w == 0 || h == 0 || w > 0x8000 || h > 0x8000)
		return -22; /* -EINVAL */
	if (n_entries != 0)
		return -22; /* -EINVAL: Header-only; Entries container unknown (r423) */

	memset(buf, 0, MT_TA_CMD_BUFFER_BYTES);
	/* [MEASURED] (r422): TA_buf+0x10 -> psKickTA[1] render target VA. */
	*(u64 *)(buf + MT_TA_BUF_HDR_TARGET_VA) = target_va;
	return 0;
}

/* r415: MT_TA_REAL_PACKET gate opening process.
 *
 * Prerequisites (all must hold before flipping to 1):
 *   1. r414 live validation: DM layout [MEASURED], firmware 0x100. DONE.
 *   2. mt_ta_submit_real() committed and reviewed. DONE (r415).
 *   3. BO staging decision documented (see MT_TA_REAL_STAGING_*). DONE.
 *   4. Pre-live T1/T2/T3 pass on the gated build.
 *   5. Single live validation with gate=1 (r414 procedure).
 *
 * Opening: set MT_TA_REAL_PACKET to 1 below, rebuild
 *          (make -C mt-vgpu-guest kernel W=1), reload bridge via
 *          scripts/safe_rmmod.sh, run single live validation.
 *
 * Rollback: set back to 0 and rebuild. The marker path
 *           (mt_fw_ta_marker_command) is untouched by the gate.
 */

static_assert(sizeof(struct mt_ta_entry_simple) == MT_TA_ENTRY_SIMPLE_BYTES,
	      "mt_ta_entry_simple size");
static_assert(MT_TA_CMD_BUFFER_BYTES == 0x168U, "MT_TA_CMD_BUFFER_BYTES");
static_assert(MT_TA_REAL_PACKET == 0, "MT_TA_REAL_PACKET default off");
static_assert(MT_TA_REAL_MAX_ENTRIES * MT_TA_ENTRY_SIMPLE_BYTES ==
	      MT_TA_CMD_BUFFER_BYTES, "max entries fill buffer");
static_assert(MT_TA_READBACK_DEBUG == 0, "MT_TA_READBACK_DEBUG default off");

#if MT_TA_READBACK_DEBUG
/* r416: 0x82:0xFD DebugTAReadback — submit real TA, wait, read target.
 * Debug-only ABI (gated, never in production). IN: request; OUT: status +
 * completion code + raw RGBA8 pixels of the 64x64 target. */
struct mt_pvr_ta_readback_in {
	u64 h_render_context;
	u32 width;
	u32 height;
	u32 n_entries;
} __attribute__((packed));
struct mt_pvr_ta_readback_out {
	u32 status;		/* 0 = success */
	u32 completion_code;	/* firmware completion (0x100 expected) */
	u8 pixels[MT_T2_TARGET_BYTES];
};
#endif

#endif /* MT_GUEST_TA_REAL_H */
