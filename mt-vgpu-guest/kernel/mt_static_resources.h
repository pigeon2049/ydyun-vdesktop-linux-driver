/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_STATIC_RESOURCES_H
#define MT_GUEST_STATIC_RESOURCES_H

#include "mt_mmu.h"

#define MT_STATIC_RESOURCE_BYTES 0x80000U

/* Defined writes in 140019cc4. The caller owns and initializes padding.
 * Preserve all other bytes, just as the reference function does.
 */
static inline int mt_init_static_resources(void *yuv, u32 yuv_size,
		void *dm_kill, u32 kill_size)
{
	static const u64 coefficients[12][3] = {
		{0x20000000ULL, 0x200020000000ULL, 0},
		{0x20000000ULL, 0x200020000000ULL, 0xfff00000fff00000ULL},
		{0x25430000ULL, 0x246e246e0000ULL, 0xfdb7fda9fdb70000ULL},
		{0x200020002000ULL, 0xf10532653b61fa01ULL, 0xe2320a87e6b40000ULL},
		{0x254325432543ULL, 0xeef2395e4399f92dULL, 0xdbbb09a6e0de0000ULL},
		{0x200020002000ULL, 0xe9262cdd38b4f4fdULL, 0xe3891100e97b0000ULL},
		{0x254325432543ULL, 0xe5fc3313408df377ULL, 0xdd431103e4070000ULL},
		{0x200020002000ULL, 0xedb72f303c34fabcULL, 0xe1c80bd2e8500000ULL},
		{0x254325432543ULL, 0xeb3035b84489fa01ULL, 0xdb420b1ee2b30000ULL},
		{0x255f255f255fULL, 0xe5e9333940bef36dULL, 0xdd431103e4070000ULL},
		{0x255f255f255fULL, 0xeee6398943ccf928ULL, 0xdbbb09a6e0de0000ULL},
		{0x255f255f255fULL, 0xeb2035e044bdf9fdULL, 0xdb420b1ee2b30000ULL},
	};
	static const u64 scale[3] = {0x2000, 0x2000, 0x2000};
	static const u64 program[8] = {
		0x0040280800808927ULL, 0x0101002b080000000ULL,
		0x8008089801806000ULL, 0x210060c00000840cULL,
		0x8001001e80042000ULL, 0x8000000400251468ULL,
		0x800000002c80401aULL, 0x800400000000029cULL,
	};
	u32 i, end = 0x80000f1c;
	if (!yuv || !dm_kill || yuv_size < MT_STATIC_RESOURCE_BYTES || kill_size < MT_STATIC_RESOURCE_BYTES)
		return -EINVAL;
	for (i = 0; i < 12; i++)
		memcpy((u8 *)yuv + i * 64, coefficients[i], sizeof(coefficients[i]));
	memcpy((u8 *)yuv + 0x3c0, scale, sizeof(scale));
	memcpy(dm_kill, program, sizeof(program));
	memcpy((u8 *)dm_kill + sizeof(program), &end, sizeof(end));
	return 0;
}

#endif
