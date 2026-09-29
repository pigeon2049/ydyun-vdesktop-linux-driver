/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_STATIC_PROGRAMS_H
#define MT_GUEST_STATIC_PROGRAMS_H
#include "mt_tqx_program.h"

#define MT_STATIC_PDS_BYTES (MT_TQX_PROGRAM_BANK_BYTES - MT_TQX_SHADER_BANK_BYTES)
#define MT_STATIC_USC_BYTES MT_TQX_SHADER_BANK_BYTES

/* 01a898 -> device/module getters -> 0d93ac/119250. PDS and USC are
 * distinct shared resources. Preserve their unspecified tails; creating
 * these CPU images alone does not upload or make a context executable. */
static inline int mt_static_programs_build(void *pds, u32 pds_bytes,
		void *usc, u32 usc_bytes, const struct mt_device_profile *profile)
{
	unsigned long p = (unsigned long)pds, u = (unsigned long)usc;
	if (!pds || !usc || pds_bytes < MT_STATIC_PDS_BYTES || usc_bytes < MT_STATIC_USC_BYTES)
		return -EINVAL;
	if (!profile || profile->family != 2 || profile->transfer_version != 1)
		return -EOPNOTSUPP;
	if (p >= u ? p - u < MT_STATIC_USC_BYTES : u - p < MT_STATIC_PDS_BYTES)
		return -EINVAL;
	memcpy(pds, mt_tqx_program_bytes + MT_STATIC_USC_BYTES, MT_STATIC_PDS_BYTES);
	memcpy(usc, mt_tqx_program_bytes, MT_STATIC_USC_BYTES);
	return 0;
}
#endif
