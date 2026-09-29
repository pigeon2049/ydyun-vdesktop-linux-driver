/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_TQX_TOPOLOGY_H
#define MT_GUEST_TQX_TOPOLOGY_H
#include "mt_device_profile.h"

/* Guest 025338(platform+8) copies info+c9c to platform+94;
 * 0205ac/042000/0228b4 and 04d558 carry it to device+3b4.
 * A valid response establishes a layout count, not engine readiness.
 * Eight is the current Linux encoder limit, not a discovered hardware limit.
 */
static inline int mt_tqx_topology_from_info(u32 *out,
		const struct mt_device_profile *profile, const void *info, u32 bytes)
{
	u32 magic, version, cores;
	if (!out || !info || bytes < 0xcc8)
		return -EINVAL;
	if (!profile || profile->family != 2 || profile->transfer_version != 1)
		return -EOPNOTSUPP;
	memcpy(&magic, info, 4);
	memcpy(&version, (const u8 *)info + 4, 4);
	if (magic != 0xaa557491 || version != 2)
		return -EPROTO;
	memcpy(&cores, (const u8 *)info + 0xc9c, 4);
	if (!cores || cores > 8)
		return -ERANGE;
	*out = cores;
	return 0;
}
#endif
