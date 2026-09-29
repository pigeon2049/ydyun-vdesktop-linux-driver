/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_WINDOWS_H
#define MT_GUEST_WINDOWS_H

#include "mt_memory_layout.h"
#include "mt_fw_layout.h"

/* Recovered platform region +0x400..+0x517, referenced by the post-connect
 * context. This is a CPU buffer, NOT a BAR image or a firmware upload.
 * 140026390 refreshes selected fields; preserve all other initialized bytes.
 * Size describes the recovered region, not a proven Host copy length.
 */
#define MT_GUEST_WINDOWS_BYTES 0x118U

struct mt_guest_window_inputs {
	u64 system_memory_bytes;
	/* Original platform fields: retain raw names until their full ABI is
	 * established. S3000 defaults +9d8=0x8000000000, +9e0=0. */
	u64 platform_18, platform_9d8, platform_9e0;
};

static inline int mt_guest_windows_refresh(void *out, u32 bytes,
		const void *raw_info, u32 length, const struct mt_guest_window_inputs *a)
{
	const u8 *info = raw_info;
	u8 next[MT_GUEST_WINDOWS_BYTES];
	u64 firmware = 0, firmware_size = 0, normal = 0;
	u64 flags, actual, pb_size, pb_pa, firmware_offset, half, remaining;
	u32 i, count;

	if (!out || bytes < sizeof(next) || !a || !info || length < 0xcc8)
		return -EINVAL;
	if (mt_info_u32(info, 0) != 0xaa557491 || mt_info_u32(info, 4) != 2)
		return -EPROTO;
	count = mt_info_u32(info, 0xc50);
	if (!count || count > (0xc48 - 0x28) / 24)
		return -EPROTO;
	for (i = 0; i < count; i++) {
		u32 off = 0x28 + i * 24;
		u64 base = mt_info_u64(info, off), bits = mt_info_u64(info, off + 16);

		if (!firmware && (bits & 4)) {
			firmware = base;
			firmware_size = mt_info_u64(info, off + 8);
		}
		if (!normal && (bits & 3))
			normal = base;
	}
	if (!firmware || !normal)
		return -ENODEV;
	flags = mt_info_u64(info, 0x10);
	actual = mt_info_u64(info, 0x20);
	pb_size = (flags & 0x10) ? mt_info_u32(info, 0xc98) : 0;
	pb_pa = (flags & 0x10) ? mt_info_u64(info, 0xc90) : 0;
	/* Reject arithmetic wraparound rather than copying the reference's
	 * unchecked arithmetic on malformed device-info/system inputs. */
	if (a->system_memory_bytes < 0x20000000ULL ||
	    normal > ~(u64)0 - 0x2000000 || firmware_size > actual ||
	    actual - firmware_size < pb_size + 0x2000000 ||
	    ((flags & 1) && !(flags & 0x80) &&
	     (firmware_size < 0x200000 || firmware > ~(u64)0 - 0x200000)))
		return -ERANGE;
	half = (((a->system_memory_bytes - 0x20000000ULL) >> 1) + 0xfff) & ~0xfffULL;
	firmware_offset = actual - firmware_size;
	remaining = actual - pb_size - 0x2000000 - firmware_size;
	memcpy(next, out, sizeof(next));
#define MT_WINDOW_SET(offset, value) mt_fw_put64(next, (offset) - 0x400, (value))
	MT_WINDOW_SET(0x420, 0);
	MT_WINDOW_SET(0x430, a->platform_9e0);
	MT_WINDOW_SET(0x438, half);
	MT_WINDOW_SET(0x440, a->platform_9d8);
	MT_WINDOW_SET(0x4d8, a->platform_18);
	MT_WINDOW_SET(0x4e0, a->platform_9d8);
	if (!(flags & 1)) {
		MT_WINDOW_SET(0x4e8, 0);
		MT_WINDOW_SET(0x4f0, 0);
		MT_WINDOW_SET(0x4f8, 0);
	} else {
		if (!(flags & 0x80)) {
			firmware_offset += 0x200000;
			firmware += 0x200000;
			firmware_size -= 0x200000;
		}
		MT_WINDOW_SET(0x4e8, firmware_offset);
		MT_WINDOW_SET(0x4f0, firmware_size);
		MT_WINDOW_SET(0x4f8, firmware);
	}
	MT_WINDOW_SET(0x500, 0);
	MT_WINDOW_SET(0x508, pb_size);
	MT_WINDOW_SET(0x510, pb_pa);
	MT_WINDOW_SET(0x448, pb_size);
	MT_WINDOW_SET(0x460, pb_size);
	MT_WINDOW_SET(0x478, pb_size);
	MT_WINDOW_SET(0x458, normal);
	MT_WINDOW_SET(0x470, normal);
	MT_WINDOW_SET(0x488, normal);
	MT_WINDOW_SET(0x450, 0);
	MT_WINDOW_SET(0x468, 0);
	MT_WINDOW_SET(0x480, 0x800000);
	MT_WINDOW_SET(0x490, pb_size + 0x800000);
	MT_WINDOW_SET(0x498, 0x1800000);
	MT_WINDOW_SET(0x4a0, normal + 0x800000);
	MT_WINDOW_SET(0x4c0, pb_size + 0x2000000);
	/* The reference subtracts the original full firmware range here,
	 * even in the legacy branch which removes 2 MiB above. */
	MT_WINDOW_SET(0x4c8, remaining);
	MT_WINDOW_SET(0x4d0, normal + 0x2000000);
#undef MT_WINDOW_SET
	memcpy(out, next, sizeof(next));
	return 0;
}

/* Fresh Guest region: 1400222b4 clears the platform object, 14002fe44
 * seeds the BAR2 triplet, and 140026390 refreshes the remaining fields.
 * Native-only 140026f58/140027024 are not part of this path. This still
 * does not allocate/publish physical backing or establish Host lifetime.
 */
static inline int mt_guest_windows_build(void *out, u32 bytes,
		const void *raw_info, u32 length, const struct mt_guest_window_inputs *a,
		u64 bar2_gpa, u64 bar2_bytes)
{
	u8 next[MT_GUEST_WINDOWS_BYTES] = {0};
	int ret;

	if (!out || bytes < sizeof(next) || !raw_info || length < 0xcc8 ||
	    !bar2_gpa || (bar2_gpa & 4095) || !bar2_bytes ||
	    (bar2_bytes & 4095) || bar2_gpa > ~(u64)0 - bar2_bytes ||
	    mt_info_u64(raw_info, 0x20) > bar2_bytes)
		return -EINVAL;
	mt_fw_put64(next, 0, bar2_gpa);
	mt_fw_put64(next, 8, bar2_bytes);
	ret = mt_guest_windows_refresh(next, sizeof(next), raw_info, length, a);
	if (ret)
		return ret;
	memcpy(out, next, sizeof(next));
	return 0;
}
#endif
