/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_RPC_TRANSPORT_H
#define MT_RPC_TRANSPORT_H
#include <linux/io.h>
#include <linux/errno.h>
#include <linux/string.h>
#include "mt_host_query.h"

/* One Guest writer, in the IRQ handler. Host owns the transition to 1. */
static inline bool mt_rpc_ack(void *page)
{
	u32 *state = (u32 *)((u8 *)page + 4);
	u64 *count = (u64 *)((u8 *)page + 8);
	if (READ_ONCE(*state) != 1)
		return false;
	WRITE_ONCE(*state, 2);
	WRITE_ONCE(*count, READ_ONCE(*count) + 1);
	mb();
	return true;
}

/* Caller serializes both rings. An error leaves the offending request queued;
 * earlier successful replies remain committed and are reported in *sent.
 * Input payload/status/padding are never treated as Guest addresses.
 */
static inline int mt_rpc_answer_queries(void *page, void __iomem *custom,
		u64 allocated, u32 utilization, u64 stat2, u32 *sent)
{
	u8 *in = (u8 *)page + 0x210, *out = (u8 *)page + 0x420;
	u8 request[32], reply[32], head, tail, next, output_tail;
	unsigned int budget = 15;
	*sent = 0;
	while (budget--) {
		head = READ_ONCE(in[0]);
		tail = READ_ONCE(in[1]);
		if (head >= 16 || tail >= 16)
			return -EPROTO;
		if (head == tail)
			break;
		rmb();
		memcpy(request, in + 16 + tail * 32, 32);
		if (mt_host_memory_reply(request, allocated, utilization, stat2, reply))
			return -EOPNOTSUPP;
		head = READ_ONCE(out[0]);
		output_tail = READ_ONCE(out[1]);
		if (head >= 16 || output_tail >= 16)
			return -EPROTO;
		next = (head + 1) & 15;
		if (next == output_tail)
			return -ENOSPC;
		memcpy(out + 16 + head * 32, reply, 32);
		wmb();
		WRITE_ONCE(in[1], (tail + 1) & 15);
		WRITE_ONCE(out[0], next);
		mb();
		writeq(2, custom + 0x138);
		mb();
		(*sent)++;
	}
	return 0;
}
#endif
