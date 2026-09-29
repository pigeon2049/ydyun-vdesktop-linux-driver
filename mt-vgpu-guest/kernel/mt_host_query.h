/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_HOST_QUERY_H
#define MT_HOST_QUERY_H
#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#else
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
#endif

/* 14002729c: request type 1, subtype 1 returns the accumulated allocation
 * bytes set by 140011510 -> 14000ee8c -> 140023880. The request's payload
 * and status bytes are unspecified and MUST NOT be interpreted as pointers.
 * Subtype 0 returns the low DWORD of EscapeSetGpuUtilStat (14002363c).
 * Subtype 2 returns its second statistic, falling back to adapter+0x1d90
 * when the statistic is zero. The Linux caller supplies that resolved value;
 * current runtime passes zero because it has no equivalent telemetry and GPU
 * work submission remains disabled. This consumes the idle query without
 * claiming to report utilization during active GPU work.
 * The containing RPC dispatcher copies type/subtype and sets operation=2.
 */
static inline int mt_host_memory_reply(const u8 request[32], u64 allocated,
				      u32 utilization, u64 stat2, u8 reply[32])
{
	unsigned int i;
	if (request[8] != 1 || request[9] > 2 || request[11] != 1)
		return -1;
	if (request[9] == 0)
		allocated = utilization;
	else if (request[9] == 2)
		allocated = stat2;
	memset(reply, 0, 32);
	for (i = 0; i < 8; i++)
		reply[i] = allocated >> (i * 8);
	reply[8] = 1;
	reply[9] = request[9];
	reply[11] = 2;
	return 0;
}
#endif
