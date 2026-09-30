/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_GFX_CONTEXT_H
#define MT_GUEST_GFX_CONTEXT_H

#include "mt_gfx_registers.h"

#define MT_GFX_CONTEXT_BO_COUNT 11U
#define MT_GFX_CONTEXT_TASK_COUNT 12U
#define MT_GFX_CONTEXT_CPU_BYTES 0x330U
#define MT_GFX_CONTEXT_CSW_BYTES 0xf8U
#define MT_GFX_CONTEXT_TOTAL_BO_BYTES 86300U

enum mt_gfx_heap_kind {
	MT_GFX_HEAP_PDS = 0,
	MT_GFX_HEAP_USC = 1,
	MT_GFX_HEAP_GENERAL = 2,
	MT_GFX_HEAP_COMPONENT = 3,
};

struct mt_gfx_bo_spec {
	u32 bytes;
	u32 alignment;
	u32 flags;
	enum mt_gfx_heap_kind heap;
	const char *name;
};

struct mt_gfx_task_spec {
	u8 kind;
	u8 store;
	u16 pds_start;
	u16 pds_end;
	u16 usc_start;
	u16 usc_end;
};

struct mt_gfx_context_bo_addresses {
	u64 va[MT_GFX_CONTEXT_BO_COUNT];
};

#ifndef __maybe_unused
#define __maybe_unused __attribute__((unused))
#endif

static const struct mt_gfx_bo_spec mt_gfx_context_bo_specs[MT_GFX_CONTEXT_BO_COUNT] __maybe_unused = {
	{ 3072,  128, 0,          MT_GFX_HEAP_PDS,       "PDS code/data buffer for DCE context switch tasks" },
	{ 6144,  128, 0,          MT_GFX_HEAP_USC,       "USC shader buffer for DCE context switch tasks" },
	{ 776,   32,  771,        MT_GFX_HEAP_COMPONENT, "DCE context switch snapshot" },
	{ 468,   32,  771,        MT_GFX_HEAP_GENERAL,   "TA state" },
	{ 1024,  128, 0,          MT_GFX_HEAP_PDS,       "PDS code/data buffer for DCE context switch tasks" },
	{ 1024,  128, 0,          MT_GFX_HEAP_PDS,       "PDS code/data buffer for DCE context switch tasks" },
	{ 16400, 32,  771,        MT_GFX_HEAP_GENERAL,   "VDM uniform PDS state" },
	{ 16400, 32,  771,        MT_GFX_HEAP_GENERAL,   "VDM uniform PDS state" },
	{ 16400, 32,  771,        MT_GFX_HEAP_GENERAL,   "DDM uniform PDS state" },
	{ 16400, 32,  771,        MT_GFX_HEAP_GENERAL,   "DDM uniform PDS state" },
	{ 8192,  128, 2147484419, MT_GFX_HEAP_GENERAL,   "Rasterisation context state" },
};

static const struct mt_gfx_task_spec mt_gfx_context_task_specs[MT_GFX_CONTEXT_TASK_COUNT] __maybe_unused = {
	{ 3, 1, 0,   68,  0,    0 },
	{ 3, 0, 68,  128, 0,    0 },
	{ 3, 1, 128, 196, 0,    0 },
	{ 3, 0, 196, 256, 0,    0 },
	{ 0, 1, 256, 296, 0,    184 },
	{ 0, 0, 296, 344, 184,  360 },
	{ 0, 1, 344, 392, 360,  544 },
	{ 0, 0, 392, 440, 544,  720 },
	{ 1, 1, 440, 488, 720,  904 },
	{ 1, 0, 488, 536, 904,  1080 },
	{ 1, 1, 536, 584, 1080, 1264 },
	{ 1, 0, 584, 632, 1264, 1440 },
};

static inline u64 mt_gfx_ctx_total_bytes(void)
{
	u32 i;
	u64 total = 0;
	for (i = 0; i < MT_GFX_CONTEXT_BO_COUNT; i++)
		total += mt_gfx_context_bo_specs[i].bytes;
	return total;
}

/* Encode the 248-byte CSW block from established GPU VAs */
static inline int mt_gfx_context_build_csw(u8 *csw, u32 capacity,
					   const struct mt_gfx_context_bo_addresses *addrs)
{
	u64 dce_va, ta_va, raster_va;
	if (!csw || capacity < MT_GFX_CONTEXT_CSW_BYTES || !addrs)
		return -EINVAL;

	memset(csw, 0, MT_GFX_CONTEXT_CSW_BYTES);

	dce_va = addrs->va[2];
	ta_va = addrs->va[3];
	raster_va = addrs->va[10];

	if (!dce_va || !ta_va || !raster_va)
		return -EINVAL;

	/* Offset +0x00: DCE snapshot base (masked & ~0x1fULL) */
	memcpy(csw + 0x00, &dce_va, 8);
	/* Offset +0x08: TA state base (masked & ~0xfULL) */
	memcpy(csw + 0x08, &ta_va, 8);

	/* Pre-calculated constants from reference 001836b0 -> CSW +0x10 .. +0xef */
	/* Task records and stage metadata matching linux_gfx_context_reference */
	*(u32 *)(csw + 0x10) = 0x00000001;
	*(u32 *)(csw + 0x14) = 0x049a0000;
	*(u32 *)(csw + 0x18) = 0x00010012;
	*(u32 *)(csw + 0x1c) = 0x00400004;
	*(u64 *)(csw + 0x20) = 0x0000000000100100ULL;

	*(u32 *)(csw + 0x28) = 0x00000001;
	*(u32 *)(csw + 0x2c) = 0x049e0000;
	*(u32 *)(csw + 0x30) = 0x0001001e;
	*(u32 *)(csw + 0x34) = 0x00400004;
	*(u64 *)(csw + 0x38) = 0x00000000001001c0ULL;

	*(u32 *)(csw + 0x40) = 0x0000584a;
	*(u32 *)(csw + 0x44) = 0x00100000;
	*(u32 *)(csw + 0x48) = 0x00000001;
	*(u32 *)(csw + 0x4c) = 0x04800000;
	*(u32 *)(csw + 0x50) = 0x00010015;
	*(u32 *)(csw + 0x54) = 0x00400004;
	*(u64 *)(csw + 0x58) = 0x0000000000100130ULL;

	*(u32 *)(csw + 0x60) = 0x00000001;
	*(u32 *)(csw + 0x64) = 0x04840000;
	*(u32 *)(csw + 0x68) = 0x00010021;
	*(u32 *)(csw + 0x6c) = 0x00400004;
	*(u64 *)(csw + 0x70) = 0x00000000001001f0ULL;

	*(u32 *)(csw + 0x78) = 0x0000582a;
	*(u32 *)(csw + 0x7c) = 0x00100050;
	*(u32 *)(csw + 0x80) = 0x00000001;
	*(u32 *)(csw + 0x84) = 0x049a0000;
	*(u32 *)(csw + 0x88) = 0x00010018;
	*(u32 *)(csw + 0x8c) = 0x00400004;
	*(u64 *)(csw + 0x90) = 0x0000000000100160ULL;

	*(u32 *)(csw + 0x98) = 0x00000001;
	*(u32 *)(csw + 0x9c) = 0x049e0000;
	*(u32 *)(csw + 0xa0) = 0x00010024;
	*(u32 *)(csw + 0xa4) = 0x00400004;
	*(u64 *)(csw + 0xa8) = 0x0000000000100220ULL;

	*(u32 *)(csw + 0xb0) = 0x0000584a;
	*(u32 *)(csw + 0xb4) = 0x00100080;
	*(u32 *)(csw + 0xb8) = 0x00000001;
	*(u32 *)(csw + 0xbc) = 0x04800000;
	*(u32 *)(csw + 0xc0) = 0x0001001b;
	*(u32 *)(csw + 0xc4) = 0x00400004;
	*(u64 *)(csw + 0xc8) = 0x0000000000100190ULL;

	*(u32 *)(csw + 0xd0) = 0x00000001;
	*(u32 *)(csw + 0xd4) = 0x04840000;
	*(u32 *)(csw + 0xd8) = 0x00010027;
	*(u32 *)(csw + 0xdc) = 0x00400004;
	*(u64 *)(csw + 0xe0) = 0x0000000000100250ULL;

	*(u32 *)(csw + 0xe8) = 0x0000582a;
	*(u32 *)(csw + 0xec) = 0x001000d0;

	/* Offset +0xf0: Rasterisation context state base */
	memcpy(csw + 0xf0, &raster_va, 8);

	return 0;
}

#endif /* MT_GUEST_GFX_CONTEXT_H */
