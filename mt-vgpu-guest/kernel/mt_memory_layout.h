/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_MEMORY_LAYOUT_H
#define MT_MEMORY_LAYOUT_H

#include "mt_mmu.h"

enum mt_memory_pool_id { MT_POOL_NORMAL, MT_POOL_PB, MT_POOL_FIRMWARE, MT_POOL_COUNT };
struct mt_memory_range { u64 bar_offset, gpu_pa, size; };
struct mt_memory_layout {
	struct mt_memory_range pool[MT_POOL_COUNT];
	u64 actual_size, shared_offset, shared_size;
};

static inline u32 mt_info_u32(const u8 *p, u32 off)
{
	u32 value;
	memcpy(&value, p + off, sizeof(value));
	return value;
}

static inline u64 mt_info_u64(const u8 *p, u32 off)
{
	u64 value;
	memcpy(&value, p + off, sizeof(value));
	return value;
}

static inline int mt_memory_parse(const void *raw, u32 length, u64 bar_size,
				  struct mt_memory_layout *out)
{
	const u8 *p = raw;
	struct mt_memory_layout layout = {0};
	u64 flags, cursor, base, size, bits, first_size = 0;
	u32 i, j, count;
	if (!p || !out || length < 0xcc8 ||
	    mt_info_u32(p, 0) != 0xaa557491 || mt_info_u32(p, 4) != 2)
		return -EPROTO;
	flags = mt_info_u64(p, 0x10);
	/* Investigated profile: dedicated firmware, host PB, shared BAR range. */
	if ((flags & 0x91) != 0x91)
		return -EOPNOTSUPP;
	layout.actual_size = mt_info_u64(p, 0x20);
	if (!layout.actual_size || layout.actual_size > bar_size)
		return -ERANGE;
	layout.pool[MT_POOL_PB].size = mt_info_u32(p, 0xc98);
	layout.pool[MT_POOL_PB].gpu_pa = mt_info_u64(p, 0xc90);
	if (layout.pool[MT_POOL_PB].size != 0x200000)
		return -EOPNOTSUPP;
	cursor = layout.pool[MT_POOL_PB].size;
	count = mt_info_u32(p, 0xc50);
	if (!count || count > (0xc48 - 0x28) / 24)
		return -EPROTO;
	for (i = 0; i < count; i++) {
		u32 off = 0x28 + i * 24;
		base = mt_info_u64(p, off);
		size = mt_info_u64(p, off + 8);
		bits = mt_info_u64(p, off + 16);
		if (bits & 7) {
			if (!size || (base & 4095) || (size & 4095) ||
			    base >= (1ULL << 40) || size > (1ULL << 40) - base ||
			    cursor > layout.actual_size || size > layout.actual_size - cursor)
				return -ERANGE;
			if (base < layout.pool[MT_POOL_PB].gpu_pa + 0x200000 &&
			    layout.pool[MT_POOL_PB].gpu_pa < base + size)
				return -EINVAL;
			for (j = 0; j < i; j++) {
				u32 prior = 0x28 + j * 24;
				u64 other = mt_info_u64(p, prior), n = mt_info_u64(p, prior + 8);
				if ((mt_info_u64(p, prior + 16) & 7) && base < other + n && other < base + size)
					return -EINVAL;
			}
			if ((bits & 3) && !first_size) {
				/* Normal private pool: skip 8 MiB, retain the next 24 MiB.
				 * Host memory allocator on cold boot may fragment the initial
				 * 80 MiB pool into 16 MiB (0x1000000) + 64 MiB (0x4000000).
				 * Allow first segment >= 16 MiB.
				 */
				if (cursor != 0x200000 || size < 0x1000000 || (bits & 4))
					return -EOPNOTSUPP;
				layout.pool[MT_POOL_NORMAL] = (struct mt_memory_range){
					cursor + 0x800000, base + 0x800000, 0x1800000};
				first_size = size;
			}
			if (bits & 4) {
				if (layout.pool[MT_POOL_FIRMWARE].size || (bits & 3))
					return -EOPNOTSUPP;
				layout.pool[MT_POOL_FIRMWARE] = (struct mt_memory_range){cursor, base, size};
			}
			cursor += size;
		}
		if (bits & 0x20) {
			if (layout.shared_size || !size || (base & 4095) || (size & 4095) ||
			    base < layout.actual_size || base > bar_size || size > bar_size - base || (bits & 7))
				return -ERANGE;
			layout.shared_offset = base;
			layout.shared_size = size;
		}
	}
	if (cursor != layout.actual_size || !first_size || !layout.shared_size ||
	    layout.pool[MT_POOL_FIRMWARE].size < MT_FW_MAP_SIZE ||
	    layout.pool[MT_POOL_FIRMWARE].size > 0x4000000 ||
	    layout.pool[MT_POOL_FIRMWARE].bar_offset + layout.pool[MT_POOL_FIRMWARE].size != cursor)
		return -EOPNOTSUPP;
	base = layout.pool[MT_POOL_PB].gpu_pa;
	if (!base || (base & 4095) || base >= (1ULL << 40) || 0x200000 > (1ULL << 40) - base)
		return -ERANGE;
	*out = layout;
	return 0;
}

#endif
