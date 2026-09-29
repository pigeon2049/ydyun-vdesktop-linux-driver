/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_ANNOUNCEMENTS_H
#define MT_GUEST_ANNOUNCEMENTS_H
#include "mt_memory_layout.h"

/* 1400268d0 advertises the reference package token between mode and version
 * queries. This identifies the emulated reference protocol package; it is
 * not a Linux driver version. No update query/download is performed here.
 */
#define MT_REFERENCE_PACKAGE_TOKEN 0x48809490dULL
static inline int mt_package_announcement(u64 mode, u8 record[32])
{
	u64 value = MT_REFERENCE_PACKAGE_TOKEN;
	if (!record)
		return -EINVAL;
	/* Only known supported modes. Mode 2 explicitly skips this message. */
	if (mode > 1)
		return -EOPNOTSUPP;
	memset(record, 0, 32);
	memcpy(record, &value, sizeof(value));
	record[9] = 4;
	return 0;
}

/* 140026b30, dedicated shared BAR region (info flags 0x80|1).
 * Three operation-0 notifications, type 3/subtype 0,1,2. The first value is
 * zero for this profile, NOT the firmware GPU physical base. The second is
 * the shared region's guest physical address. No code/data is uploaded here.
 */
static inline int mt_shared_announcements(const void *info, u32 length,
		u64 bar_start, u64 bar_size, u8 records[96])
{
	struct mt_memory_layout layout;
	u64 values[3];
	u32 i;
	int ret;
	if (!records || !bar_start || (bar_start & 4095))
		return -EINVAL;
	ret = mt_memory_parse(info, length, bar_size, &layout);
	if (ret)
		return ret;
	if (layout.shared_size < 0x200000 || bar_start > ~(u64)0 - bar_size)
		return -ERANGE;
	values[0] = 0;
	values[1] = bar_start + layout.shared_offset;
	values[2] = 0x200000;
	memset(records, 0, 96);
	for (i = 0; i < 3; i++) {
		memcpy(records + i * 32, &values[i], 8);
		records[i * 32 + 8] = 3;
		records[i * 32 + 9] = i;
	}
	return 0;
}
#endif
