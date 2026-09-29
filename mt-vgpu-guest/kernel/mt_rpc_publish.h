/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_RPC_PUBLISH_H
#define MT_RPC_PUBLISH_H
#include <linux/io.h>
#include <linux/errno.h>
#include <linux/string.h>

/* Caller serializes the selected ring's single Guest producer. Validate all
 * cursors and reserve the full batch before modifying any shared memory.
 * Host tail can only advance while we construct the unpublished records.
 */
static inline int mt_rpc_publish(void *page, void __iomem *custom,
		u32 index, const u8 *records, u32 count)
{
	u8 *ring;
	u8 head, tail;
	u32 i;
	if (!page || !custom || !records || (index != 0 && index != 2) || !count || count > 15)
		return -EINVAL;
	ring = (u8 *)page + index * 0x210;
	head = READ_ONCE(ring[0]);
	tail = READ_ONCE(ring[1]);
	if (head >= 16 || tail >= 16)
		return -EPROTO;
	if (((tail - head - 1) & 15) < count)
		return -EAGAIN;
	for (i = 0; i < count; i++)
		memcpy(ring + 16 + ((head + i) & 15) * 32, records + i * 32, 32);
	wmb();
	WRITE_ONCE(ring[0], (head + count) & 15);
	mb();
	writeq(index, custom + 0x138);
	mb();
	return 0;
}
#endif
