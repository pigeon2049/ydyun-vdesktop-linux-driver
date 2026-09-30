/* SPDX-License-Identifier: GPL-2.0 */
/* RAM tests for the Stage B bridge core: wire layouts, token rings, the
 * mmap-offset codec and the handle allocator. No hardware, no modules.
 */
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../kernel/mt_pvr_wire.h"
#include "../kernel/mt_pvr_queue.h"

static int checks;

#define CHECK(cond) do { \
	checks++; \
	if (!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		return 1; \
	} \
} while (0)

static int test_wire_offsets(void)
{
	/* Field offsets are the contract with the driver, not just sizes. */
	CHECK(offsetof(struct mt_pvr_connect_out, packed_bvnc) == 0);
	CHECK(offsetof(struct mt_pvr_connect_out, error) == 8);
	CHECK(offsetof(struct mt_pvr_connect_out, capability_flags) == 12);
	CHECK(offsetof(struct mt_pvr_connect_out, kernel_arch) == 16);
	CHECK(offsetof(struct mt_pvr_heap_details_out, base) == 0);
	CHECK(offsetof(struct mt_pvr_heap_details_out, length) == 8);
	CHECK(offsetof(struct mt_pvr_heap_details_out, reserved_length) == 16);
	CHECK(offsetof(struct mt_pvr_heap_details_out, heap_name_out) == 24);
	CHECK(offsetof(struct mt_pvr_heap_details_out, error) == 32);
	CHECK(offsetof(struct mt_pvr_heap_details_out, log2_data_page_size) == 36);
	CHECK(offsetof(struct mt_pvr_heap_details_out, log2_import_alignment) == 40);
	CHECK(offsetof(struct mt_pvr_pmr_in, chunk_size) == 0);
	CHECK(offsetof(struct mt_pvr_pmr_in, user_address) == 60);
	CHECK(offsetof(struct mt_pvr_pmr_out, out_flags) == 12);
	CHECK(offsetof(struct mt_pvr_pmr_out, is_system_mem) == 16);
	CHECK(offsetof(struct mt_pvr_map_in, server_heap) == 0);
	CHECK(offsetof(struct mt_pvr_map_in, pmr) == 8);
	CHECK(offsetof(struct mt_pvr_map_in, reservation) == 16);
	CHECK(offsetof(struct mt_pvr_map_in, map_flags) == 24);
	CHECK(offsetof(struct mt_pvr_sync_block_out, sync_handle) == 0);
	CHECK(offsetof(struct mt_pvr_sync_block_out, sync_pmr) == 8);
	CHECK(offsetof(struct mt_pvr_sync_block_out, error) == 16);
	CHECK(offsetof(struct mt_pvr_sync_block_out, block_size) == 20);
	CHECK(offsetof(struct mt_pvr_sync_block_out, vaddr) == 24);
	/* The measured memType, not the arena class 0x2. */
	CHECK(MT_PVR_SYNC_MEM_TYPE == 0x100000000ULL);
	return 0;
}

static int test_queue_basic(void)
{
	struct mt_pvr_queue q;
	u64 token = 0, seen = 0;
	u32 type = 0xffffffff;

	mt_pvr_queue_init(&q, MT_PVR_RING_ENTRIES);
	CHECK(q.capacity == 64);
	/* One slot is always kept free, matching the vendor rings. */
	CHECK(mt_pvr_queue_free(&q) == MT_PVR_RING_ENTRIES - 1);
	CHECK(mt_pvr_queue_drained(&q));

	/* A zero token is never handed out: it means "no token". */
	CHECK(mt_pvr_queue_submit(&q, 0xabc0, MT_PVR_SUBMIT_PREEMPTIBLE, &token) == 0);
	CHECK(token == 1);
	CHECK(mt_pvr_queue_drain(&q, &seen, &type) == -EAGAIN);
	CHECK(mt_pvr_queue_token_valid(&q, token));
	CHECK(!mt_pvr_queue_token_valid(&q, 0));
	CHECK(!mt_pvr_queue_drained(&q));

	/* An event whose token does not match is stale: it is consumed, but it
	 * must not retire a submission.
	 */
	CHECK(mt_pvr_queue_complete(&q, token + 7, MT_PVR_COMPLETION_NORMAL) == 0);
	CHECK(mt_pvr_queue_drain(&q, &seen, &type) == 0);
	CHECK(q.submit_tail == 0);
	CHECK(q.complete_tail == 1);
	CHECK(q.completed_token == 0);
	CHECK(!mt_pvr_queue_drained(&q));

	CHECK(mt_pvr_queue_complete(&q, token, MT_PVR_COMPLETION_NORMAL) == 0);
	CHECK(mt_pvr_queue_drain(&q, &seen, &type) == 1);
	CHECK(seen == token && type == MT_PVR_COMPLETION_NORMAL);
	CHECK(q.completed_token == token);
	CHECK(mt_pvr_queue_drained(&q));

	/* Out-of-order completions are matched one at a time: the head record
	 * is the only one that may complete.
	 */
	CHECK(mt_pvr_queue_submit(&q, 1, 0, &token) == 0);
	CHECK(mt_pvr_queue_submit(&q, 2, 0, &token) == 0);
	seen = q.submit_tail;
	CHECK(mt_pvr_queue_complete(&q, token, MT_PVR_COMPLETION_NORMAL) == 0);
	CHECK(mt_pvr_queue_drain(&q, NULL, &type) == 0);
	CHECK(q.submit_tail == seen);
	CHECK(!mt_pvr_queue_drained(&q));
	/* The head record is still outstanding, so completing it drains both. */
	CHECK(mt_pvr_queue_submit(&q, 0, 0, &token) == 0);
	return 0;
}

static int test_queue_full_and_wrap(void)
{
	struct mt_pvr_queue q;
	u64 token;
	u32 i;

	mt_pvr_queue_init(&q, 4);
	/* One slot stays empty, so a 4-entry ring carries 3 items. */
	CHECK(mt_pvr_queue_free(&q) == 3);
	for (i = 0; i < 3; i++)
		CHECK(mt_pvr_queue_submit(&q, i, 0, &token) == 0);
	/* Full: the UMD retries, so this must be a clean -EAGAIN. */
	CHECK(mt_pvr_queue_submit(&q, 3, 0, &token) == -EAGAIN);
	CHECK(mt_pvr_queue_free(&q) == 0);

	for (i = 0; i < 3; i++) {
		CHECK(mt_pvr_queue_complete(&q, i + 1, MT_PVR_COMPLETION_NORMAL) == 0);
		CHECK(mt_pvr_queue_drain(&q, &token, NULL) == 1);
	}
	CHECK(mt_pvr_queue_drained(&q));
	/* Wrapping the indices must not confuse the ring. */
	CHECK(mt_pvr_queue_submit(&q, 5, 0, &token) == 0);
	CHECK(token == 4);
	CHECK(mt_pvr_queue_complete(&q, token, MT_PVR_COMPLETION_PREEMPTED) == 0);
	CHECK(mt_pvr_queue_drain(&q, NULL, NULL) == 1);
	CHECK(q.completed_token == 4);
	CHECK(mt_pvr_queue_drained(&q));

	/* Completion ring is one slot short of full; the firmware cannot
	 * lap us silently.
	 */
	mt_pvr_queue_init(&q, 4);
	CHECK(mt_pvr_queue_submit(&q, 0, 0, NULL) == 0);
	for (i = 0; i < 3; i++)
		CHECK(mt_pvr_queue_complete(&q, 1, MT_PVR_COMPLETION_NORMAL) == 0);
	CHECK(mt_pvr_queue_complete(&q, 1, MT_PVR_COMPLETION_NORMAL) == -EAGAIN);
	return 0;
}

static int test_queue_fault(void)
{
	struct mt_pvr_queue q;
	u64 token;

	mt_pvr_queue_init(&q, MT_PVR_RING_ENTRIES);
	CHECK(mt_pvr_queue_submit(&q, 0, 0, &token) == 0);
	CHECK(mt_pvr_queue_complete(&q, token, MT_PVR_COMPLETION_FAULTED) == 0);
	CHECK(mt_pvr_queue_drain(&q, NULL, NULL) == 1);
	CHECK(q.faulted == 1);
	/* A faulted queue must never report "drained": EVENTOBJECTWAIT would
	 * otherwise return success for work the GPU dropped.
	 */
	CHECK(!mt_pvr_queue_drained(&q));
	return 0;
}

static int test_offset_codec(void)
{
	u64 handle = 0;
	u64 offset;

	/* Measured invariant: offset == handle << 12, always 4 KiB aligned. */
	offset = mt_pvr_offset_of(0x1000);
	CHECK(offset == 0x1000000);
	CHECK(mt_pvr_handle_of(offset, &handle) == 0);
	CHECK(handle == 0x1000);

	offset = mt_pvr_offset_of(0x500b);
	CHECK(offset == 0x500b000);
	CHECK(mt_pvr_handle_of(offset, &handle) == 0);
	CHECK(handle == 0x500b);

	/* Unaligned or zero offsets are not something the driver ever sends.
	 * 0x1000 is aligned and decodes to handle 0x1, which is valid.
	 */
	CHECK(mt_pvr_handle_of(0x1000, &handle) == 0);
	CHECK(handle == 0x1);
	CHECK(mt_pvr_handle_of(0x1001, &handle) == -EINVAL);
	CHECK(mt_pvr_handle_of(0x800, &handle) == -EINVAL);
	CHECK(mt_pvr_handle_of(0, &handle) == -EINVAL);
	return 0;
}

static int test_handles(void)
{
	struct mt_pvr_handles h;
	u64 a = 0, b = 0;

	mt_pvr_handles_init(&h);
	CHECK(h.next == 0x1000);
	CHECK(mt_pvr_handles_alloc(&h, &a) == 0);
	CHECK(mt_pvr_handles_alloc(&h, &b) == 0);
	CHECK(a == 0x1000 && b == 0x1001);
	/* Distinct handles: bA4 showed aliasing handles send the UMD down a
	 * different failure path.
	 */
	CHECK(a != b);
	CHECK(a && b);

	h.next = MT_PVR_HANDLE_LIMIT;
	CHECK(mt_pvr_handles_alloc(&h, &a) == -ENOSPC);
	return 0;
}

static int test_heap_table(void)
{
	static const char *const names[MT_PVR_HEAP_COUNT] = {
		"General", NULL, NULL, NULL, "Component Control", NULL, NULL,
		"PDS Code and Data", "USC Code", NULL, NULL,
	};
	struct mt_guest_heap_plan plan;
	struct mt_pvr_heap_table table;
	u32 index = 0xffffffff;

	mt_guest_plan_heaps(&plan);
	mt_pvr_heaps_init(&table, &plan, names);

	/* The driver asks for the count first; it must see the 11 slots we
	 * actually serve, not the vendor's 22.
	 */
	CHECK(mt_pvr_heaps_count(&table) == MT_PVR_HEAP_COUNT);
	CHECK(mt_pvr_heaps_count(&table) == 11);

	/* Geometry comes from the plan, which matches the vendor table byte for
	 * byte (reports/windows-heap-table-22.json).
	 */
	CHECK(table.entries[0].base == 0x40000000ULL);
	CHECK(table.entries[0].size == 0x8000000000ULL);
	CHECK(table.entries[1].base == 0x8100000000ULL);
	CHECK(table.entries[1].size == 0x100000000ULL);
	CHECK(table.entries[6].base == 0xe1c0000000ULL);
	CHECK(table.entries[8].size == 0x1000ULL);
	CHECK(table.entries[10].base == 0xf000000000ULL);

	/* Slots 4 and 5 are empty on both sides. */
	CHECK(table.entries[4].size == 0 && table.entries[5].size == 0);

	/* The names the UMD looks up, including the one it reported missing
	 * before we supplied it (bA5).
	 */
	CHECK(mt_pvr_heaps_find(&table, "General", &index) == 0 && index == 0);
	CHECK(mt_pvr_heaps_find(&table, "PDS Code and Data", &index) == 0 && index == 7);
	CHECK(mt_pvr_heaps_find(&table, "USC Code", &index) == 0 && index == 8);
	CHECK(mt_pvr_heaps_find(&table, "Component Control", &index) == 0 && index == 4);

	/* Unnamed slots must not match an empty query, or every lookup would
	 * land on heap 2. Prefix queries must not match either: the UMD
	 * compares whole strings.
	 */
	CHECK(mt_pvr_heaps_find(&table, "", &index) == -ENOENT);
	CHECK(mt_pvr_heaps_find(&table, "Nonexistent", &index) == -ENOENT);
	CHECK(mt_pvr_heaps_find(&table, "Gene", &index) == -ENOENT);
	CHECK(mt_pvr_heaps_find(&table, NULL, &index) == -EINVAL);

	/* Every named heap must resolve back to its own index. */
	{
		u32 named = 0, i;
		for (i = 0; i < table.count; i++) {
			if (!table.entries[i].name)
				continue;
			named++;
			CHECK(mt_pvr_heaps_find(&table, table.entries[i].name,
						&index) == 0);
			CHECK(index == i);
		}
		CHECK(named == 4);
	}
	return 0;
}

int main(void)
{
	CHECK(test_wire_offsets() == 0);
	CHECK(test_queue_basic() == 0);
	CHECK(test_queue_full_and_wrap() == 0);
	CHECK(test_queue_fault() == 0);
	CHECK(test_offset_codec() == 0);
	CHECK(test_handles() == 0);
	CHECK(test_heap_table() == 0);
	printf("pvr_bridge_core_test OK (%d checks)\n", checks);
	return 0;
}
