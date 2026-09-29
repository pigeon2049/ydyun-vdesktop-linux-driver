/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_DEVICE_PROFILE_H
#define MT_GUEST_DEVICE_PROFILE_H
#include "mt_mmu.h"

/* Reference command-library selectors, not proof that a live engine is ready.
 * 1400470c8 selects the family using the PCI device ID from 140042000 /
 * 1400228b4. 140046e1c supplies versions; 14004d558 installs them. */
struct mt_device_profile {
	u32 vendor, device, family;
	u32 primary_version, ce_version, transfer_version, compute_version;
};
static inline int mt_device_profile_select(struct mt_device_profile *out,
		u32 vendor, u32 device)
{
	struct mt_device_profile next = {.vendor=vendor, .device=device};
	if (!out || vendor != 0x1ed5 || device > 0xffff)
		return -ENODEV;
	next.family = device >> 8;
	switch (next.family) {
	case 1: next.primary_version=1; next.transfer_version=1; next.compute_version=1; break;
	case 2: next.primary_version=2; next.transfer_version=1; next.compute_version=1; break;
	case 3: next.primary_version=3; next.ce_version=1; next.transfer_version=1; next.compute_version=1; break;
	case 4: next.primary_version=4; next.ce_version=1; next.transfer_version=1; next.compute_version=2; break;
	case 6: next.primary_version=5; next.ce_version=3; next.compute_version=3; break;
	default: return -ENODEV;
	}
	*out = next;
	return 0;
}

static inline int mt_device_profile_ce(const struct mt_device_profile *p, u32 version)
{
	if (!p || !p->family || !p->ce_version || version != p->ce_version)
		return -EOPNOTSUPP;
	return 0;
}

static inline int mt_device_profile_work(const struct mt_device_profile *p, u32 type)
{
	if (!p || !p->family || (type == 9 && !p->ce_version))
		return -EOPNOTSUPP;
	return 0;
}

static inline int mt_device_profile_transfer(const struct mt_device_profile *p)
{
	return p && p->family && p->transfer_version == 1 ? 0 : -EOPNOTSUPP;
}

static inline int mt_device_profile_node(const struct mt_device_profile *p, u32 type)
{
	if (!p || !p->family || ((type == 3 || type == 4) && !p->ce_version))
		return -EOPNOTSUPP;
	return 0;
}
#endif
