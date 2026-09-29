/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_FW_QUEUE_IO_H
#define MT_GUEST_FW_QUEUE_IO_H

#include <linux/io.h>
#include <linux/spinlock.h>
#include "mt_fw_queue.h"

struct mt_fw_queue_io {
	void __iomem *queue;
	void __iomem *registers;
	spinlock_t producer[MT_FW_DM_COUNT];
};

static inline u32 mt_fw_io_read32(void *opaque, u32 offset)
{
	struct mt_fw_queue_io *q = opaque;
	return readl(q->queue + offset);
}

static inline void mt_fw_io_write32(void *opaque, u32 offset, u32 value)
{
	struct mt_fw_queue_io *q = opaque;
	writel(value, q->queue + offset);
}

static inline void mt_fw_io_copy_to(void *opaque, u32 offset, const void *source, u32 size)
{
	struct mt_fw_queue_io *q = opaque;
	memcpy_toio(q->queue + offset, source, size);
}

static inline void mt_fw_io_barrier(void *opaque)
{
	mb();
}

static inline void mt_fw_io_kick(void *opaque, u32 dm)
{
	struct mt_fw_queue_io *q = opaque;
	writel(dm, q->registers + 0xb00);
}

static const struct mt_fw_queue_ops mt_fw_io_ops = {
	.read32 = mt_fw_io_read32,
	.write32 = mt_fw_io_write32,
	.copy_to = mt_fw_io_copy_to,
	.order_writes = mt_fw_io_barrier,
	.kick = mt_fw_io_kick,
};

/* Bind existing CPU mappings only: does not initialize or publish device
 * memory. Caller must upload initialized queues, publish resources and
 * establish the firmware lifecycle before using try_submit.
 */
static inline int mt_fw_queue_bind(struct mt_fw_queue_io *q,
		void __iomem *firmware, u32 bytes, void __iomem *registers)
{
	u32 dm;
	if (!q || !firmware || !registers || bytes < MT_FW_STATE_BYTES + MT_FW_QUEUE_BYTES)
		return -EINVAL;
	q->queue = firmware + MT_FW_STATE_BYTES;
	q->registers = registers;
	for (dm = 0; dm < MT_FW_DM_COUNT; dm++)
		spin_lock_init(&q->producer[dm]);
	return 0;
}

static inline int mt_fw_queue_try_submit(struct mt_fw_queue_io *q,
		u32 dm, u32 ring, const void *command)
{
	unsigned long flags;
	int ret;
	if (!q || !q->queue || !q->registers || dm >= MT_FW_DM_COUNT)
		return -EINVAL;
	spin_lock_irqsave(&q->producer[dm], flags);
	ret = mt_fw_queue_submit(&mt_fw_io_ops, q, MT_FW_QUEUE_BYTES, dm, ring, command);
	spin_unlock_irqrestore(&q->producer[dm], flags);
	return ret;
}

#endif
