/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_PVR_QUEUE_H
#define MT_PVR_QUEUE_H

/* Per-queue completion tracking, modelled on the Windows KMD.
 *
 * mtkm64.sys does not invent fence values: dxgkrnl hands it a FenceID with
 * every command, the driver stores it in a fixed-size work record, the
 * firmware echoes the same value back in a completion event, and the driver
 * only compares the two before advancing the queue. Evidence and line numbers
 * are in reports/windows-kmd-crosscheck.md section 2.
 *
 * The shapes follow that model so the Stage B bridge inherits a design the
 * vendor already validated on the same silicon:
 *
 *   submit ring    64 slots, record carries the token at +0x08
 *   completion     64 slots, event carries the token at +0x08 and a type at +0x04
 *   advance        head = (head + 1) % capacity, only after a token match
 *
 * Two deliberate differences from the Windows code, both because we are not
 * the OS: the token is issued here (Linux has no dxgkrnl to hand us one), and
 * a full submit ring returns -EAGAIN instead of spinning 10000 times and
 * dropping the descriptor (mtkm64.sys disassembly.txt:12417-12441).
 *
 * Single producer per ring: the UMD serialises its own submissions, matching
 * PVR's contract. Callers still hold the connection lock, as they do for every
 * other per-connection structure.
 */

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/errno.h>
#include "mt_guest_heaps.h"
#else
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include "mt_guest_heaps.h"
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
#endif

#define MT_PVR_RING_ENTRIES 64U
#define MT_PVR_TOKEN_NONE 0ULL

/* Completion event types, from the firmware contract the driver dispatches on
 * (decompiled.c:9115-9127): normal, preempted, bypassed, faulted.
 */
enum mt_pvr_completion_type {
	MT_PVR_COMPLETION_NORMAL = 0,
	MT_PVR_COMPLETION_PREEMPTED = 5,
	MT_PVR_COMPLETION_BYPASSED = 0x50,
	MT_PVR_COMPLETION_FAULTED = 0x101,
};

struct mt_pvr_submit_slot {
	u64 token;
	u64 context;
	u32 flags;
	u32 reserved;
	u32 stamp[2];
};

struct mt_pvr_complete_slot {
	u64 token;
	u32 type;
	u32 reserved;
	u64 payload[2];
};

/* A submit slot may be taken out of order by the firmware; this bit marks the
 * ones that may be preempted (0x20 in mtkm64.sys). */
#define MT_PVR_SUBMIT_PREEMPTIBLE 0x20U

struct mt_pvr_queue {
	u64 next_token;
	u32 submit_head;	/* filled by us */
	u32 submit_tail;	/* consumed by the firmware */
	u32 complete_head;	/* filled by the firmware */
	u32 complete_tail;	/* drained by us */
	u32 last_token;		/* most recently submitted */
	u64 completed_token;	/* most recently matched, 0 before first match */
	u32 capacity;
	u32 faulted;
	struct mt_pvr_submit_slot submit[MT_PVR_RING_ENTRIES];
	struct mt_pvr_complete_slot complete[MT_PVR_RING_ENTRIES];
};

static inline void mt_pvr_queue_init(struct mt_pvr_queue *q, u32 capacity)
{
	memset(q, 0, sizeof(*q));
	q->capacity = capacity ? capacity : MT_PVR_RING_ENTRIES;
}

/* Both rings use the vendor's convention: head is the next slot to write,
 * tail is the next slot to read, and one slot stays empty so full and empty
 * are distinguishable. A 64-entry ring therefore carries 63 items, exactly
 * as the Windows driver notes for the DM rings.
 */
static inline u32 mt_pvr_ring_used(u32 head, u32 tail, u32 capacity)
{
	return (head + capacity - tail) % capacity;
}

static inline u32 mt_pvr_ring_free(u32 head, u32 tail, u32 capacity)
{
	return capacity - 1 - mt_pvr_ring_used(head, tail, capacity);
}

static inline int mt_pvr_ring_full(u32 head, u32 tail, u32 capacity)
{
	return (head + 1) % capacity == tail;
}

static inline u32 mt_pvr_queue_free(const struct mt_pvr_queue *q)
{
	return mt_pvr_ring_free(q->submit_head, q->submit_tail, q->capacity);
}

static inline int mt_pvr_queue_token_valid(const struct mt_pvr_queue *q, u64 token)
{
	return token != MT_PVR_TOKEN_NONE && token <= q->last_token;
}

/* Record a submission. The caller has already validated its command buffer.
 * Returns -EAGAIN when the ring is full; the UMD retries, so no work is lost.
 */
static inline int mt_pvr_queue_submit(struct mt_pvr_queue *q, u64 context,
				      u32 flags, u64 *token_out)
{
	u64 token;
	u32 slot;

	if (mt_pvr_ring_full(q->submit_head, q->submit_tail, q->capacity))
		return -EAGAIN;
	/* 0 is reserved for "no token", so the counter starts at 1 and wraps
	 * back to 1 rather than 0.
	 */
	token = q->next_token + 1;
	if (token == MT_PVR_TOKEN_NONE)
		token = 1;
	slot = q->submit_head;
	q->submit[slot].token = token;
	q->submit[slot].context = context;
	q->submit[slot].flags = flags;
	q->submit[slot].reserved = 0;
	q->submit[slot].stamp[0] = 0;
	q->submit[slot].stamp[1] = 0;
	q->submit_head = (slot + 1) % q->capacity;
	q->next_token = token;
	q->last_token = (u32)token;
	if (token_out)
		*token_out = token;
	return 0;
}

/* Publish a completion event from the firmware side. Used by the event drain
 * path, never by the UMD.
 */
static inline int mt_pvr_queue_complete(struct mt_pvr_queue *q, u64 token,
					u32 type)
{
	u32 slot = q->complete_head;

	if (mt_pvr_ring_full(q->complete_head, q->complete_tail, q->capacity))
		return -EAGAIN;
	q->complete[slot].token = token;
	q->complete[slot].type = type;
	q->complete[slot].reserved = 0;
	q->complete[slot].payload[0] = 0;
	q->complete[slot].payload[1] = 0;
	q->complete_head = (slot + 1) % q->capacity;
	return 0;
}

/* Drain one completion.
 *
 * Mirrors FUN_14000be34 + FUN_14000e6a4: the event ring is drained
 * unconditionally (the firmware will not resend), and only the submit side
 * advances, and only when the event token matches the record at the head of
 * the submit ring. An event that does not match belongs to a submission the
 * queue already retired -- after an engine reset, for instance -- and is
 * dropped rather than allowed to block the ring.
 *
 * Returns 1 when a submission completed, 0 when the event was stale, and
 * -EAGAIN when there is nothing to drain.
 */
static inline int mt_pvr_queue_drain(struct mt_pvr_queue *q, u64 *token_out,
				     u32 *type_out)
{
	u32 slot;
	u64 token;
	u32 type;
	int matched;

	if (q->complete_tail == q->complete_head)
		return -EAGAIN;
	slot = q->complete_tail;
	token = q->complete[slot].token;
	type = q->complete[slot].type;
	q->complete_tail = (slot + 1) % q->capacity;
	if (q->submit_tail == q->submit_head)
		return 0;
	matched = q->submit[q->submit_tail].token == token;
	if (!matched)
		return 0;
	if (type == MT_PVR_COMPLETION_FAULTED)
		q->faulted = 1;
	q->completed_token = token;
	q->submit_tail = (q->submit_tail + 1) % q->capacity;
	if (token_out)
		*token_out = token;
	if (type_out)
		*type_out = type;
	return 1;
}

/* True once every submitted token has been matched, which is what
 * EVENTOBJECTWAIT (0x1:0x5) needs before it may return.
 */
static inline int mt_pvr_queue_drained(const struct mt_pvr_queue *q)
{
	return q->submit_tail == q->submit_head && q->faulted == 0;
}

/* mmap offset codec.
 *
 * Measured across 28 mmap calls in four traces: the driver always passes
 * offset = handle << 12, and the offset is always 4 KiB aligned. So a handle
 * the bridge hands out must satisfy handle == offset >> 12. Allocating the
 * DRM offset first and deriving the handle keeps the two spaces in step.
 */
#define MT_PVR_OFFSET_SHIFT 12
static inline u64 mt_pvr_offset_of(u64 handle)
{
	return handle << MT_PVR_OFFSET_SHIFT;
}

static inline int mt_pvr_handle_of(u64 offset, u64 *handle)
{
	if (offset & ((1ULL << MT_PVR_OFFSET_SHIFT) - 1))
		return -EINVAL;
	*handle = offset >> MT_PVR_OFFSET_SHIFT;
	return *handle ? 0 : -EINVAL;
}

/* Handle allocator.
 *
 * Every observable handle in the traces came out of one small space
 * (0x1000..0x9000), so the bridge keeps a single monotonic space rather than
 * per-class partitioning. Two rules matter to the UMD: a handle is never zero
 * (bA4 showed zero handles alias and break later unref/map steps), and two
 * live objects never share one.
 */
#define MT_PVR_HANDLE_BASE 0x1000ULL
#define MT_PVR_HANDLE_LIMIT 0x10000000ULL	/* keeps offset below 2^44 */

struct mt_pvr_handles {
	u64 next;
};

static inline void mt_pvr_handles_init(struct mt_pvr_handles *h)
{
	h->next = MT_PVR_HANDLE_BASE;
}

static inline int mt_pvr_handles_alloc(struct mt_pvr_handles *h, u64 *handle)
{
	if (h->next >= MT_PVR_HANDLE_LIMIT)
		return -ENOSPC;
	*handle = h->next++;
	return 0;
}

static inline void mt_pvr_handles_free(struct mt_pvr_handles *h, u64 handle)
{
	/* Only teardown calls this, and only once nothing is live: recycling
	 * a handle the UMD still holds would make a stale name point at a new
	 * object.
	 */
	if (handle && handle < h->next)
		h->next = handle;
}

/* Heap table server for 0x6:0x20 MM:HeapCfgHeapDetails and 0x6:0x1e
 * HeapCfgHeapCount.
 *
 * The driver looks heaps up by name (MTSRVFindHeapByName in the UMD), and the
 * name lives in the caller's buffer: the bridge copies it out during
 * devmem-context construction, exactly like the 5.2 header describes. Getting
 * this wrong is not cosmetic -- bA5 found the devmem context failed until
 * "USC Code" was supplied, and bA7 found the render context failing on a
 * missing "General".
 *
 * The geometry comes from mt_guest_plan_heaps(), which now matches the vendor
 * driver's own 22-entry table entry for entry (see
 * reports/windows-heap-table-22.json). Only the 11 slots we fill are served;
 * index 4 and 5 are empty on both sides and are reported as zero-sized, which
 * is what the reference does.
 */
#define MT_PVR_HEAP_COUNT 11U

struct mt_pvr_heap_entry {
	const char *name;
	mt_gpuvaddr base;
	u64 size;
	u32 log2_page_size;
};

struct mt_pvr_heap_table {
	struct mt_pvr_heap_entry entries[MT_PVR_HEAP_COUNT];
	u32 count;
};

static inline void mt_pvr_heaps_init(struct mt_pvr_heap_table *table,
				     const struct mt_guest_heap_plan *plan,
				     const char *const *names)
{
	u32 i;

	memset(table, 0, sizeof(*table));
	table->count = MT_PVR_HEAP_COUNT;
	for (i = 0; i < MT_PVR_HEAP_COUNT; i++) {
		table->entries[i].base = plan->heaps[i].base;
		table->entries[i].size = plan->heaps[i].size;
		table->entries[i].log2_page_size = 12;
		/* An unnamed heap stays NULL: the driver only copies a name
		 * when one exists, and it reports the name it wanted in its
		 * next error, which is how we discovered the missing ones.
		 */
		table->entries[i].name = names ? names[i] : NULL;
	}
}

/* Name lookup, same strcmp semantics as the UMD's own DevmemFindHeapByName. */
static inline int mt_pvr_heaps_find(const struct mt_pvr_heap_table *table,
				    const char *name, u32 *index)
{
	u32 i;

	if (!name)
		return -EINVAL;
	for (i = 0; i < table->count; i++) {
		if (table->entries[i].name &&
		    !strcmp(table->entries[i].name, name)) {
			*index = i;
			return 0;
		}
	}
	return -ENOENT;
}

static inline u32 mt_pvr_heaps_count(const struct mt_pvr_heap_table *table)
{
	return table->count;
}

#endif /* MT_PVR_QUEUE_H */
