/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_FW_QUEUE_H
#define MT_GUEST_FW_QUEUE_H

#include "mt_fw_layout.h"

/* mtkm64.sys 14000bf20/14000bdc4/14000c198. Each DM has two
 * 64-entry command rings, a 64-entry event ring and three cursor pairs.
 * Only 63 entries are usable. Cursors are 32 bits at 16-byte strides.
 */
#define MT_FW_DM_COUNT 6U
#define MT_FW_DM_BYTES 0x2e30U
#define MT_FW_COMMAND_BYTES 0x50U
#define MT_FW_COMMAND_RING_BYTES 0x1400U
#define MT_FW_EVENT_BYTES 0x18U
#define MT_FW_EVENT_OFFSET 0x2800U
#define MT_FW_CURSOR_OFFSET 0x2e00U
#define MT_FW_RING_ENTRIES 64U
#define MT_FW_CONNECT 0x46U
#define MT_FW_DISCONNECT 0x47U

/* Caller serializes producers per DM and keeps the mapping alive.
 * order_writes must order device-memory writes, not just compiler accesses.
 * The backend receives queue-relative offsets; kick receives the DM index.
 */
struct mt_fw_queue_ops {
	u32 (*read32)(void *opaque, u32 offset);
	void (*write32)(void *opaque, u32 offset, u32 value);
	void (*copy_to)(void *opaque, u32 offset, const void *source, u32 size);
	void (*order_writes)(void *opaque);
	void (*kick)(void *opaque, u32 dm);
};

static inline int mt_fw_queue_init_image(void *queue, u32 size)
{
	if (!queue || size < MT_FW_QUEUE_BYTES)
		return -EINVAL;
	memset(queue, 0, MT_FW_QUEUE_BYTES);
	return 0;
}

/* Optional fields reproduce 14000c0ac's param_5[0] / param_5[2].
 * Connect/disconnect pass NULL. PID is supplied by the Linux caller.
 */
static inline void mt_fw_kernel_command(void *command, u32 opcode, u32 pid,
					const u32 *parameters)
{
	memset(command, 0, MT_FW_COMMAND_BYTES);
	mt_fw_put32(command, 0x0c, opcode);
	mt_fw_put32(command, 0x4c, pid);
	if (parameters) {
		mt_fw_put32(command, 0x18, parameters[0]);
		mt_fw_put32(command, 0x20, parameters[2]);
	}
}

/* One attempt; a full ring returns EAGAIN without memory or MMIO writes.
 * Unlike the Windows 10,000-iteration spin, retries belong to a bounded
 * higher-level wait outside the producer spinlock.
 */
static inline int mt_fw_queue_submit(const struct mt_fw_queue_ops *ops,
		void *opaque, u32 bytes, u32 dm, u32 ring, const void *command)
{
	u32 cursor, head, tail, next, destination;
	if (!ops || !ops->read32 || !ops->write32 || !ops->copy_to ||
	    !ops->order_writes || !ops->kick || !command ||
	    bytes < MT_FW_QUEUE_BYTES || dm >= MT_FW_DM_COUNT || ring >= 2)
		return -EINVAL;
	cursor = dm * MT_FW_DM_BYTES + MT_FW_CURSOR_OFFSET + ring * 16;
	head = ops->read32(opaque, cursor);
	tail = ops->read32(opaque, cursor + 8);
	if (head >= MT_FW_RING_ENTRIES || tail >= MT_FW_RING_ENTRIES)
		return -EIO;
	next = (head + 1) & (MT_FW_RING_ENTRIES - 1);
	if (next == tail)
		return -EAGAIN;
	destination = dm * MT_FW_DM_BYTES + ring * MT_FW_COMMAND_RING_BYTES +
		      head * MT_FW_COMMAND_BYTES;
	ops->copy_to(opaque, destination, command, MT_FW_COMMAND_BYTES);
	ops->order_writes(opaque);
	ops->write32(opaque, cursor, next);
	ops->order_writes(opaque);
	(void)ops->read32(opaque, cursor); /* Reference reads back published head. */
	ops->kick(opaque, dm);
	ops->order_writes(opaque);
	return 0;
}

/* Includes firmware-produced events; ignoring their cursor can falsely
 * report idle during disconnect. Returns 1 empty, 0 pending, negative error.
 * Caller must quiesce submissions first. This snapshot alone is not a fence.
 */
static inline int mt_fw_queue_dm_idle(const struct mt_fw_queue_ops *ops,
		void *opaque, u32 bytes, u32 dm)
{
	u32 ring, cursor, head, tail;
	if (!ops || !ops->read32 || bytes < MT_FW_QUEUE_BYTES || dm >= MT_FW_DM_COUNT)
		return -EINVAL;
	for (ring = 0; ring < 3; ring++) {
		cursor = dm * MT_FW_DM_BYTES + MT_FW_CURSOR_OFFSET + ring * 16;
		head = ops->read32(opaque, cursor);
		tail = ops->read32(opaque, cursor + 8);
		if (head >= MT_FW_RING_ENTRIES || tail >= MT_FW_RING_ENTRIES)
			return -EIO;
		if (head != tail)
			return 0;
	}
	return 1;
}

static inline int mt_fw_queue_work_idle(const struct mt_fw_queue_ops *ops,
		void *opaque, u32 bytes)
{
	u32 dm;
	int ret;
	/* 14000bc9c excludes DM0, whose disconnect is checked separately. */
	for (dm = 1; dm < MT_FW_DM_COUNT; dm++) {
		ret = mt_fw_queue_dm_idle(ops, opaque, bytes, dm);
		if (ret != 1)
			return ret;
	}
	return 1;
}

#endif
