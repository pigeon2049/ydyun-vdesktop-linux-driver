/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_SYSTEM_ADDRESS_H
#define MT_GUEST_SYSTEM_ADDRESS_H
#include "mt_guest_windows.h"

/* 022f38 -> MmGetPhysicalAddress -> 021d90. This is a Guest GPA
 * translation, not a DMA IOVA. Only the no-MMIO system-RAM branches are
 * implemented here; BAR addresses belong to the separate aperture mapper. */
struct mt_system_address {
	u64 bar2, bar2_bytes, bar4, bar4_bytes, bias;
};

static inline int mt_system_address_init(struct mt_system_address *out,
		const void *info, u32 length, const void *windows, u32 bytes,
		u64 bar4, u64 bar4_bytes)
{
	struct mt_system_address a;
	u64 flags, extra;
	if (!out || !info || length < 0xcc8 || !windows || bytes < MT_GUEST_WINDOWS_BYTES)
		return -EINVAL;
	if (mt_info_u32(info, 0) != 0xaa557491 || mt_info_u32(info, 4) != 2)
		return -EPROTO;
	flags = mt_info_u64(info, 0x10);
	if (!(flags & 2) && !(flags & 0x40))
		return -EOPNOTSUPP; /* Otherwise original writes +0x80 / reads +0x88. */
	a = (struct mt_system_address){.bar2 = mt_info_u64(windows, 0),
		.bar2_bytes = mt_info_u64(windows, 8), .bar4 = bar4,
		.bar4_bytes = bar4_bytes, .bias = mt_info_u64(windows, 0xe0)};
	extra = flags & 2 ? 0 : mt_info_u64(info, 0xca8);
	if (!a.bar2 || !a.bar2_bytes || ((a.bar2 | a.bar2_bytes | bar4 | bar4_bytes |
	    a.bias | extra) & 4095) || a.bar2 > ~(u64)0 - a.bar2_bytes ||
	    bar4 > ~(u64)0 - bar4_bytes || a.bias >= (1ULL << MT_GPU_VA_BITS) ||
	    extra >= (1ULL << MT_GPU_VA_BITS) - a.bias)
		return -ERANGE;
	a.bias += extra;
	*out = a;
	return 0;
}

static inline int mt_system_page_address(const struct mt_system_address *a,
		u64 gpa, u64 *gpu_pa)
{
	if (!a || !gpu_pa || !gpa || (gpa & 4095))
		return -EINVAL;
	if (a->bias >= (1ULL << MT_GPU_VA_BITS) ||
	    gpa >= (1ULL << MT_GPU_VA_BITS) - a->bias)
		return -ERANGE;
	if ((gpa >= a->bar2 && gpa - a->bar2 < a->bar2_bytes) ||
	    (gpa >= a->bar4 && gpa - a->bar4 < a->bar4_bytes))
		return -EXDEV;
	*gpu_pa = gpa + a->bias;
	return 0;
}
#endif
