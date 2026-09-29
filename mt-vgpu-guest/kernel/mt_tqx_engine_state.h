/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_TQX_ENGINE_STATE_H
#define MT_GUEST_TQX_ENGINE_STATE_H
#include "mt_device_profile.h"

#define MT_TQX_ENGINE_STATE_ALLOCATION 4096U
struct mt_tqx_engine_state_plan {
	u64 encoded_va;
	u32 required_bytes, allocation_bytes, reference_alignment, reserved;
};

/* 093aa8 allocates cores*0x180 bytes at alignment 0x80 for family2;
 * 093cc8 encodes its VA in 40 bits. Linux reserves a whole ordinary-heap
 * page and rejects truncation. cores is caller-supplied, not auto-detected.
 * This only plans storage: no BO allocation, initialization, mapping, GPU
 * ownership or publication. Reference InitContextResource (00a27c) records
 * a CPU mapping without writing state bytes; OS contents and hardware
 * acceptance are not established by the RAM oracle. See tqx-state-init-path.md.
 */
static inline int mt_tqx_engine_state_plan(struct mt_tqx_engine_state_plan *out,
		const struct mt_device_profile *profile, u32 cores, u64 va)
{
	struct mt_tqx_engine_state_plan next = {0};
	if (!out)
		return -EINVAL;
	if (!profile || profile->family != 2 || profile->transfer_version != 1)
		return -EOPNOTSUPP;
	if (!cores || cores > 8 || (va & 4095))
		return -EINVAL;
	if (va < 0x40000000ULL || va > 0x8040000000ULL - MT_TQX_ENGINE_STATE_ALLOCATION)
		return -ERANGE;
	next.encoded_va = va;
	next.required_bytes = cores * 0x180;
	next.allocation_bytes = MT_TQX_ENGINE_STATE_ALLOCATION;
	next.reference_alignment = 0x80;
	*out = next;
	return 0;
}
#endif
