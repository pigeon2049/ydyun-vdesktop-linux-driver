/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_MMU_H
#define MT_GUEST_MMU_H

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error GPU table serialization currently requires a little-endian build.
#endif

/* Three-level, 4 KiB-page encoding recovered from mtkm64.sys.
 * This header builds CPU-side tables only. Publishing a root to the GPU
 * requires the still-incomplete context/invalidation protocol.
 */
#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#include <linux/errno.h>
#else
#include <stdint.h>
#include <string.h>
#include <errno.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
#endif

#define MT_GPU_VA_BITS 40
#define MT_FW_MAP_SIZE 0x800000U
#define MT_FW_TABLE_BYTES 0x6000U

static inline u32 mt_mmu_pc(u64 address, u64 flags)
{
	/* 14002a82c */
	return (u32)((address >> 12) << 4) | (flags & 1) | ((flags >> 1) & 8);
}

static inline u64 mt_mmu_pd(u64 address, u64 flags)
{
	/* 14002a878 */
	return (address & 0xffffffffe0ULL) | (flags & 1) | ((flags >> 1) & 8);
}

static inline u64 mt_mmu_pt(u64 address, u64 flags)
{
	/* 14002a8d8. Bit 62 is intentional, not an x86 page-table NX bit. */
	return (address & 0xfffffff000ULL) | (flags & 7) |
	       ((flags >> 1) & 8) | ((flags & 8) << 59);
}

static inline u32 mt_mmu_pc_index(u64 va) { return (va >> 30) & 0x3ff; }
static inline u32 mt_mmu_pd_index(u64 va) { return (va >> 21) & 0x1ff; }
static inline u32 mt_mmu_pt_index(u64 va) { return (va >> 12) & 0x1ff; }

/* Construct an 8 MiB mapping: PC, PD, then four PT pages. Addresses here
 * are GPU-visible device physical addresses, not CPU BAR physical addresses.
 * Caller owns the six aligned pages and must reserve the backing ranges.
 * Data layout is little-endian; currently supported only on x86_64.
 */
static inline int mt_mmu_build_firmware(void *tables, u64 table_address,
				       u64 firmware_address, u64 firmware_va)
{
	u32 *pc = tables;
	u64 *pd = (u64 *)((u8 *)tables + 0x1000);
	u32 directory = mt_mmu_pd_index(firmware_va), i, j;
	u64 limit = 1ULL << MT_GPU_VA_BITS;

	if (!tables || ((unsigned long)tables & 7) ||
	    (table_address & 0xfff) || (firmware_address & 0xfff) ||
	    (firmware_va & 0x1fffff) || directory > 508)
		return -EINVAL;
	if (table_address > limit - MT_FW_TABLE_BYTES ||
	    firmware_address > limit - MT_FW_MAP_SIZE ||
	    firmware_va > limit - MT_FW_MAP_SIZE)
		return -ERANGE;
	if (table_address < firmware_address + MT_FW_MAP_SIZE &&
	    firmware_address < table_address + MT_FW_TABLE_BYTES)
		return -EINVAL;
	memset(tables, 0, MT_FW_TABLE_BYTES);
	pc[mt_mmu_pc_index(firmware_va)] = mt_mmu_pc(table_address + 0x1000, 1);
	for (i = 0; i < 4; i++) {
		u64 *pt = (u64 *)((u8 *)tables + (i + 2) * 0x1000);
		pd[directory + i] = mt_mmu_pd(table_address + (i + 2) * 0x1000, 1);
		for (j = 0; j < 512; j++)
			/* 140018bd0 maps FW flags 0x1c to entry flags 9. */
			pt[j] = mt_mmu_pt(firmware_address + ((u64)i * 512 + j) * 0x1000, 9);
	}
	return 0;
}
#endif
