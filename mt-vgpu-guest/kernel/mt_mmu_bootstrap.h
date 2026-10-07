/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_MMU_BOOTSTRAP_H
#define MT_GUEST_MMU_BOOTSTRAP_H

#include "mt_mmu.h"

/* CPU-side construction for disjoint VA ranges with contiguous or explicit
 * page-list backing. Not a live mapper or a GPU TLB invalidation interface.
 * Allocation order matches 140019074/1400197b4: root, then PD/PT as first used.
 *
 * The walk is three levels (mt_mmu.h): 1024 root entries of 4 B, 512 directory
 * entries of 8 B and 512 table entries of 8 B over 4 KiB pages. A directory
 * entry therefore spans 2 MiB and one table page describes 512 pages. The
 * hardware imposes no cap on the number of mappings; what bounds one address
 * space is the page-table page budget, because every mapping needs at least one
 * table entry. Earlier revisions used a literal 24-range ceiling here, which was
 * a driver array bound and not a property of the MMU.
 */
#define MT_BOOT_ROOT_PAGES 1U
#define MT_BOOT_PC_ENTRIES 1024U
#define MT_BOOT_PD_ENTRIES 512U
#define MT_BOOT_PT_ENTRIES 512U
#define MT_BOOT_PT_SPAN (MT_BOOT_PT_ENTRIES * 4096ULL)
/* Table-page budget (r268): 64U covered the original 32-page translator
 * scene; the 8MB live-fire scratch needs 2048 more (2112 total). Small
 * scenes are unaffected; the cap still bounds runaway VM sizes.
 */
#define MT_BOOT_MAX_TABLE_PAGES 2112U
#define MT_BOOT_MAX_MAPPED_BYTES 0x4000000U
#define MT_MMU_DUMMY_BYTES 0x3000U

/* Mappings one table-page budget can describe. Contiguous callers far exceed
 * the derived figure before pages run out; a mapping that needs its own table
 * page is the worst case. Returns zero for a budget that cannot even hold the
 * root page. */
static inline u32 mt_boot_max_ranges(u32 table_pages)
{
	u64 slots, bytes;
	if (table_pages <= MT_BOOT_ROOT_PAGES)
		return 0;
	slots = (u64)(table_pages - MT_BOOT_ROOT_PAGES) * MT_BOOT_PT_ENTRIES;
	bytes = MT_BOOT_MAX_MAPPED_BYTES / 4096ULL;
	return (u32)(slots < bytes ? slots : bytes);
}

/* Windows MMUMapPage flags, NOT Linux MMU_PROTFLAGS or POSIX RW bits.
 * 018bd0 -> 02a8d8 maps bit 0 to the hardware PTE read-only bit (1).
 * Original Linux RGXDerivePTEProt8 independently confirms RO/coherent bits.
 * Default zero is writable; 3 means read-only + coherent, not read/write.
 */
#define MT_GPU_MAP_DEFAULT 0U
#define MT_GPU_MAP_READ_ONLY 1U
#define MT_GPU_MAP_CACHE_COHERENT 2U

struct mt_mmu_range {
	u64 va, pa;
	u32 size, flags;
};

static inline int mt_boot_find_page(const u32 *keys, u32 count, u32 key)
{
	u32 i;
	for (i = 1; i < count; i++)
		if (keys[i] == key)
			return i;
	return -1;
}

static inline u32 mt_boot_pd_key(u64 va)
{
	return 0x40000000U | mt_mmu_pc_index(va);
}

static inline u32 mt_boot_pt_key(u64 va)
{
	return 0x80000000U | (mt_mmu_pc_index(va) << 9) | mt_mmu_pd_index(va);
}

static inline int mt_mmu_build_pages(void *out, u32 capacity, u64 table_pa,
		const struct mt_mmu_range *ranges, const u64 *const *page_lists,
		u32 count, u32 *used_pages)
{
	/* Heap, not stack: the table budget (r268: 2112 pages) no longer
	 * fits a kernel frame (8KB > 2KB limit). kvzalloc matches the
	 * old zeroed-array semantics.
	 */
	u32 *keys = kvzalloc(sizeof(*keys) * MT_BOOT_MAX_TABLE_PAGES,
			     GFP_KERNEL);
	u32 pages = 1, i, j, k, page, keys_to_add[2];
	u64 va, end, total = 0, flags, value;
	u32 pc_value;
	u8 *bytes = out;
	int pd, pt, ret = 0;
	if (!keys)
		return -ENOMEM;
	if (!out || !used_pages || !ranges || !count ||
	    capacity < 4096 || (capacity & 4095) || (table_pa & 4095) ||
	    table_pa >= (1ULL << MT_GPU_VA_BITS)) {
		ret = -EINVAL;
		goto out;
	}
	/* A caller-supplied budget smaller than the compile-time ceiling may hold
	 * fewer mappings. Exhaustion past this point is reported by the page loop
	 * below as -ENOSPC, which is the real hardware-adjacent limit. */
	if (count > mt_boot_max_ranges(capacity / 4096)) {
		ret = -EINVAL;
		goto out;
	}
	/* Preflight the entire plan before changing a byte of output. */
	for (i = 0; i < count; i++) {
		const struct mt_mmu_range *r = &ranges[i];
		const u64 *list = page_lists ? page_lists[i] : NULL;
		if (!r->size || ((r->va | r->pa | r->size) & 4095) ||
		    r->va >= (1ULL << MT_GPU_VA_BITS) || r->pa >= (1ULL << MT_GPU_VA_BITS) ||
		    r->size > (1ULL << MT_GPU_VA_BITS) - r->va ||
		    (!list && r->size > (1ULL << MT_GPU_VA_BITS) - r->pa) || (r->flags & ~0x1fU)) {
			ret = -EINVAL;
			goto out;
		}
		if (list)
			for (j = 0; j < r->size / 4096; j++)
				if ((list[j] & 4095) || list[j] >= (1ULL << MT_GPU_VA_BITS)) {
					ret = -ERANGE;
					goto out;
				}
		total += r->size;
		if (total > MT_BOOT_MAX_MAPPED_BYTES) {
			ret = -E2BIG;
			goto out;
		}
		for (j = 0; j < i; j++)
			if (r->va < ranges[j].va + ranges[j].size && ranges[j].va < r->va + r->size) {
				ret = -EEXIST;
				goto out;
			}
		end = r->va + r->size;
		for (va = r->va & ~0x1fffffULL; va < end; va += 0x200000) {
			keys_to_add[0] = mt_boot_pd_key(va);
			keys_to_add[1] = mt_boot_pt_key(va);
			for (k = 0; k < 2; k++) {
				if (mt_boot_find_page(keys, pages, keys_to_add[k]) >= 0)
					continue;
				if (pages >= MT_BOOT_MAX_TABLE_PAGES || pages >= capacity / 4096) {
					ret = -ENOSPC;
					goto out;
				}
				keys[pages++] = keys_to_add[k];
			}
		}
	}
	if (pages * 4096ULL > (1ULL << MT_GPU_VA_BITS) - table_pa) {
		ret = -ERANGE;
		goto out;
	}
	for (i = 0; i < count; i++) {
		const u64 *list = page_lists ? page_lists[i] : NULL;
		for (j = 0; j < ranges[i].size / 4096; j++) {
			u64 pa = list ? list[j] : ranges[i].pa + j * 4096ULL;
			if (pa < table_pa + pages * 4096ULL && table_pa < pa + 4096) {
				pr_info("mt_mmu: build_pages overlap i=%u j=%u pa=%#llx tpa=%#llx pages=%u\n",
					i, j, (unsigned long long)pa,
					(unsigned long long)table_pa, pages);
				ret = -EINVAL;
				goto out;
			}
		}
	}
	memset(bytes, 0, pages * 4096);
	for (page = 1; page < pages; page++) {
		if ((keys[page] & 0xc0000000U) == 0x40000000U) {
			pc_value = mt_mmu_pc(table_pa + page * 4096ULL, 1);
			memcpy(bytes + (keys[page] & 1023) * 4, &pc_value, 4);
		} else {
			pd = mt_boot_find_page(keys, pages, 0x40000000U | ((keys[page] >> 9) & 1023));
			value = mt_mmu_pd(table_pa + page * 4096ULL, 1);
			memcpy(bytes + pd * 4096 + (keys[page] & 511) * 8, &value, 8);
		}
	}
	for (i = 0; i < count; i++) {
		const struct mt_mmu_range *r = &ranges[i];
		/* 140018bd0 translates map flags before calling the PTE encoder. */
		flags = 1 | ((r->flags & 3) << 1) | ((r->flags & 0x10) >> 1);
		for (j = 0; j < r->size; j += 4096) {
			va = r->va + j;
			pt = mt_boot_find_page(keys, pages, mt_boot_pt_key(va));
			value = mt_mmu_pt(page_lists && page_lists[i] ?
				page_lists[i][j / 4096] : r->pa + j, flags);
			memcpy(bytes + pt * 4096 + mt_mmu_pt_index(va) * 8, &value, 8);
		}
	}
	*used_pages = pages;
out:
	kvfree(keys);
	return ret;
}

/* Existing contiguous bootstrap callers keep their original interface. */
static inline int mt_mmu_build_bootstrap(void *out, u32 capacity, u64 table_pa,
		const struct mt_mmu_range *ranges, u32 count, u32 *used_pages)
{
	return mt_mmu_build_pages(out, capacity, table_pa, ranges, NULL, count, used_pages);
}

/* 14002a4ec: independent default PD, default PT and zero backing page.
 * These are separate from the ordinary zero-initialized root/PD/PT tree.
 */
static inline int mt_mmu_build_dummy(void *out, u32 size, u64 pa)
{
	u32 i;
	u64 value;
	u8 *bytes = out;
	if (!out || size < MT_MMU_DUMMY_BYTES || (pa & 4095) ||
	    pa > (1ULL << MT_GPU_VA_BITS) - MT_MMU_DUMMY_BYTES)
		return -EINVAL;
	memset(bytes, 0, MT_MMU_DUMMY_BYTES);
	for (i = 0; i < 512; i++) {
		value = mt_mmu_pd(pa + 4096, 0x11);
		memcpy(bytes + i * 8, &value, 8);
		value = mt_mmu_pt(pa + 8192, 0x11);
		memcpy(bytes + 4096 + i * 8, &value, 8);
	}
	return 0;
}

#endif
