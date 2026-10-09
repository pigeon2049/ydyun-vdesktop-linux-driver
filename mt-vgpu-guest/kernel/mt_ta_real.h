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

/* r415: Production real-TA submit request (parameterized).
 * Replaces the r414 test hook's hardcoded 64x64 single entry. */
struct mt_ta_real_request {
	u64 h_render_context;	/* render context handle */
	u32 width;		/* TA entry render-target width (1..0x8000) */
	u32 height;		/* TA entry render-target height (1..0x8000) */
	u32 n_entries;		/* number of 40B entries, 1..MT_TA_REAL_MAX_ENTRIES */
};

/* Build a 360B TA command buffer from (w, h, n_entries).
 * [MEASURED]: entry format from r410/r414; Q2 packing verified live.
 * Fills n_entries 40B simple entries, zeroes the remainder.
 * Returns 0 on success, -EINVAL on bad input.
 */
static inline int mt_ta_real_buffer_build(unsigned char *buf, u32 w, u32 h,
					  u32 n_entries)
{
	u32 i;
	int ret;

	if (!buf || w == 0 || h == 0 || w > 0x8000 || h > 0x8000)
		return -22; /* -EINVAL */
	if (n_entries == 0 || n_entries > MT_TA_REAL_MAX_ENTRIES)
		return -22; /* -EINVAL */

	memset(buf, 0, MT_TA_CMD_BUFFER_BYTES);
	for (i = 0; i < n_entries; i++) {
		ret = mt_ta_entry_simple_build(
			(struct mt_ta_entry_simple *)(buf + i * MT_TA_ENTRY_SIMPLE_BYTES),
			w, h);
		if (ret)
			return ret;
	}
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

#endif /* MT_GUEST_TA_REAL_H */
