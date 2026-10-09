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

/* [INFERRED] (r411): DM packet offsets for TA buffer VA/size, by analogy
 * with 3D (mt_fw_3d_command: VA @+0x28, size @+0x30, r381 MEASURED).
 * TO-VALIDATE: TA firmware may use different offsets. The gate defaults
 * off, so this layout is inert until live validation. */
#define MT_TA_DM_PKT_TA_VA_LO 0x28U
#define MT_TA_DM_PKT_TA_VA_HI 0x2cU
#define MT_TA_DM_PKT_TA_SIZE  0x30U

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

static_assert(sizeof(struct mt_ta_entry_simple) == MT_TA_ENTRY_SIMPLE_BYTES,
	      "mt_ta_entry_simple size");
static_assert(MT_TA_CMD_BUFFER_BYTES == 0x168U, "MT_TA_CMD_BUFFER_BYTES");
static_assert(MT_TA_REAL_PACKET == 0, "MT_TA_REAL_PACKET default off");

#endif /* MT_GUEST_TA_REAL_H */
