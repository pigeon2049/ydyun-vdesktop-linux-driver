/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_FW_EVENT_H
#define MT_GUEST_FW_EVENT_H
#include "mt_fw_queue.h"

/* 14000be34 dispatches word 1. 14000e6a4 compares word 2 with the
 * oldest pending entry's fence; it does not complete every earlier ID. */
struct mt_fw_event { u32 words[6]; };
/* 1400238cc: shared page 0 + 0x330, tested as DWORD == 1. */
static inline int mt_fw_submission_paused(u32 shared_flag) { return shared_flag == 1; }
enum mt_fw_event_kind {
	MT_EVENT_COMPLETE, MT_EVENT_PREEMPT, MT_EVENT_TRACE, MT_EVENT_FAULT, MT_EVENT_UNKNOWN
};
static inline enum mt_fw_event_kind mt_fw_event_kind(const struct mt_fw_event *e)
{
	switch (e->words[1]) {
	case 0: return MT_EVENT_COMPLETE;
	case 5: return MT_EVENT_PREEMPT;
	case 0x50: return MT_EVENT_TRACE;
	case 0x101: return MT_EVENT_FAULT;
	default: return MT_EVENT_UNKNOWN;
	}
}
static inline int mt_fw_event_matches(const struct mt_fw_event *e, u32 pending, u32 oldest)
{
	if (!e)
		return -EINVAL;
	if (mt_fw_event_kind(e) != MT_EVENT_COMPLETE)
		return -EOPNOTSUPP;
	if (!pending)
		return -ENOENT;
	return e->words[2] == oldest ? 0 : -ESTALE;
}

/* NULL-descriptor submit path in 14001475c. This is the reference's empty
 * submission packet (opcode 100), not proof the firmware supports it here. */
static inline void mt_fw_marker_command(void *command, u32 fence)
{
	memset(command, 0, MT_FW_COMMAND_BYTES);
	mt_fw_put32(command, 0x0c, 100);
	mt_fw_put32(command, 0x48, fence);
}

struct mt_fw_event_ops {
	u32 (*read32)(void *, u32);
	void (*write32)(void *, u32, u32);
	void (*copy_from)(void *, void *, u32, u32);
	void (*order_reads)(void *);
	void (*order_writes)(void *);
};

/* One consumer, serialized with submission/teardown. Snapshot each head so
 * a producer cannot keep this call running forever. A failed callback must
 * have no externally visible completion effect: its event remains pending.
 * Previously accepted events are acknowledged once before returning error.
 * No event bytes are cleared and this function never rings a doorbell. */
static inline int mt_fw_event_drain(const struct mt_fw_event_ops *ops, void *io,
		u32 bytes, u32 budget,
		int (*consume)(void *, u32, const struct mt_fw_event *), void *consumer,
		u32 *consumed)
{
	struct mt_fw_event event;
	u32 dm, cursor, head, tail, initial, count = 0;
	int ret = 0;
	if (!ops || !ops->read32 || !ops->write32 || !ops->copy_from ||
	    !ops->order_reads || !ops->order_writes || !consume || !consumed ||
	    bytes < MT_FW_QUEUE_BYTES || !budget)
		return -EINVAL;
	*consumed = 0;
	for (dm = 0; dm < MT_FW_DM_COUNT; dm++) {
		cursor = dm * MT_FW_DM_BYTES + MT_FW_CURSOR_OFFSET + 32;
		head = ops->read32(io, cursor);
		tail = ops->read32(io, cursor + 8);
		if (head >= 64 || tail >= 64) {
			ret = -EIO;
			break;
		}
		initial = tail;
		ops->order_reads(io);
		while (tail != head && count < budget) {
			ops->copy_from(io, &event, dm * MT_FW_DM_BYTES + MT_FW_EVENT_OFFSET +
				      tail * MT_FW_EVENT_BYTES, sizeof(event));
			ret = consume(consumer, dm, &event);
			if (ret)
				break;
			tail = (tail + 1) & 63;
			count++;
		}
		if (tail != initial) {
			ops->order_writes(io);
			ops->write32(io, cursor + 8, tail);
			ops->order_writes(io);
		}
		if (ret || count == budget)
			break;
	}
	*consumed = count;
	return ret;
}
#endif
