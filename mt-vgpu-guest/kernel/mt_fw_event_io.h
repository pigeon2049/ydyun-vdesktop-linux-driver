/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_FW_EVENT_IO_H
#define MT_GUEST_FW_EVENT_IO_H
#include "mt_fw_event.h"
#include "mt_fw_queue_io.h"
static inline void mt_fw_event_copy_from(void *opaque, void *dst, u32 offset, u32 bytes)
{
	struct mt_fw_queue_io *q = opaque;
	memcpy_fromio(dst, q->queue + offset, bytes);
}
static const struct mt_fw_event_ops mt_fw_event_io_ops = {
	.read32 = mt_fw_io_read32, .write32 = mt_fw_io_write32,
	.copy_from = mt_fw_event_copy_from,
	.order_reads = mt_fw_io_barrier, .order_writes = mt_fw_io_barrier,
};
#endif
