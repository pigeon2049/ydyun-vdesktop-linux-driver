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
#include <stdlib.h>
#include <string.h>

#include "../../kernel/mt_pvr_wire.h"
#include "../../kernel/mt_pvr_queue.h"
#include "../../kernel/mt_pvr_device.h"
#include "../../kernel/mt_transfer_fill.h"
#include "../../kernel/mt_tqx_fill.h"
#include "../../kernel/mt_ta_real.h"
#include "../../userspace/ta_readback_analyze.h"

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

static int test_mmap_page_count(void)
{
	/* The render-context path maps the exact PMR byte counts 39935 and
	 * 174079. Rounding down would leave the tail unmapped; rejecting a
	 * non-multiple fails the mapping outright.
	 */
	CHECK(mt_pvr_mmap_page_count(1, 4096) == 1);
	CHECK(mt_pvr_mmap_page_count(4096, 4096) == 1);
	CHECK(mt_pvr_mmap_page_count(4097, 4096) == 2);
	CHECK(mt_pvr_mmap_page_count(39935, 4096) == 10);
	CHECK(mt_pvr_mmap_page_count(174079, 4096) == 43);
	CHECK(mt_pvr_mmap_page_count(0, 4096) == 0);
	CHECK(mt_pvr_mmap_page_count(4096, 0) == 0);
	CHECK(mt_pvr_mmap_page_count(4096, 1000) == 0);
	/* The kernel rounds the VMA length before pvr_mmap() sees it. */
	CHECK(mt_pvr_mmap_fits(40960, 39935, 4096));
	CHECK(mt_pvr_mmap_fits(176128, 174079, 4096));
	CHECK(mt_pvr_mmap_fits(4096, 4096, 4096));
	CHECK(!mt_pvr_mmap_fits(8192, 4096, 4096));
	CHECK(!mt_pvr_mmap_fits(4096, 0, 4096));
	return 0;
}

static int test_heap_table(void)
{
	/* Aligned with the guest physical plan: one name per plan position.
	 * "Component Control" belongs at position 3 (base 0xa000000000);
	 * position 4 is empty and a name there is simply dropped during
	 * compaction.
	 */
	static const char *const names[MT_PVR_PLAN_HEAP_COUNT] = {
		"General", NULL, NULL, "Component Control", NULL, NULL,
		NULL, "PDS Code and Data", "USC Code", NULL, NULL,
	};
	struct mt_guest_heap_plan plan;
	struct mt_pvr_heap_table table;
	u32 index = 0xffffffff;

	mt_guest_plan_heaps(&plan);
	mt_pvr_heaps_init(&table, &plan, names, MT_PVR_PLAN_HEAP_COUNT);

	/* The count is the number of *populated* heaps, not the table width.
	 *
	 * The plan's slots 4 and 5 are empty on the reference side as well. They
	 * used to be published as zero-length entries, and the real UMD builds one
	 * arena per heap-table entry: for entry 4 the arena has nothing to
	 * reserve, FUN_0019e7f0() returns 0, and RGXCreateDeviceMemContext fails
	 * with 82 = MTSRV_ERROR_DEVICEMEM_UNABLE_TO_CREATE_ARENA. The UMD was then
	 * measured building exactly 9 arenas, so 9 is the served count.
	 */
	CHECK(mt_pvr_heaps_count(&table) == 9);

	/* No published entry may be empty: that was the whole bug. */
	for (index = 0; index < mt_pvr_heaps_count(&table); index++) {
		CHECK(table.entries[index].base != 0);
		CHECK(table.entries[index].size != 0);
	}

	/* Geometry still comes from the plan, in compacted order. */
	CHECK(table.entries[0].base == 0x40000000ULL);
	CHECK(table.entries[0].size == 0x8000000000ULL);
	CHECK(table.entries[1].base == 0x8100000000ULL);
	CHECK(table.entries[1].size == 0x100000000ULL);
	CHECK(table.entries[4].base == 0xe1c0000000ULL);  /* was index 6 */
	CHECK(table.entries[6].size == 0x1000ULL);        /* was index 8 */
	CHECK(table.entries[8].base == 0xf000000000ULL);  /* was index 10 */

	/* Names travel with their entry through the compaction, so every
	 * by-name lookup the UMD performs still resolves.
	 */
	CHECK(mt_pvr_heaps_find(&table, "General", &index) == 0 && index == 0);
	CHECK(mt_pvr_heaps_find(&table, "PDS Code and Data", &index) == 0 && index == 5);
	CHECK(mt_pvr_heaps_find(&table, "USC Code", &index) == 0 && index == 6);
	CHECK(mt_pvr_heaps_find(&table, "Component Control", &index) == 0 && index == 3);

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

static int test_device_layout(void)
{
	struct mt_pvr_conn conn;
	struct mt_pvr_features features;
	struct mt_pvr_connect_out result;
	/* Large enough for a full info page; static so a failing check leaks
	 * nothing.
	 */
	static u32 page[MT_PVR_INFO_BYTES / 4];

	/* Offsets the UMD reads out of its own connection object. Each one was
	 * confirmed against a live offline session; if the struct moves, one of
	 * these fires rather than a silent misread.
	 */
	CHECK(offsetof(struct mt_pvr_conn, srv_handle) == 0x00);
	CHECK(offsetof(struct mt_pvr_conn, version) == 0x08);
	CHECK(offsetof(struct mt_pvr_conn, pid) == 0x0c);
	CHECK(offsetof(struct mt_pvr_conn, init_flags) == 0x14);
	CHECK(offsetof(struct mt_pvr_conn, info_page) == 0x28);
	CHECK(offsetof(struct mt_pvr_conn, tl_stream) == 0x48);
	CHECK(offsetof(struct mt_pvr_conn, hwperf_um) == 0x50);
	CHECK(offsetof(struct mt_pvr_conn, hwperf_setting) == 0x60);
	CHECK(offsetof(struct mt_pvr_conn, devmem_refs) == 0x70);
	CHECK(offsetof(struct mt_pvr_conn, devmem_ctx) == 0x78);
	CHECK(offsetof(struct mt_pvr_conn, features) == 0xa0);
	CHECK(offsetof(struct mt_pvr_conn, sync_arena) == 0xb0);
	CHECK(offsetof(struct mt_pvr_conn, sync_span) == 0xb8);
	/* The feature block lives behind this skew: GetFeatures adds it. */
	CHECK(MT_PVR_FEATURE_SKEW == 0x620);

	mt_pvr_conn_init(&conn, 4242, 2);
	/* The driver hands the connection pointer back to us as the services
	 * handle, so it has to be self-referential.
	 */
	CHECK(conn.srv_handle == (u64)(uintptr_t)&conn);
	CHECK(conn.version == 1);
	CHECK(conn.pid == 4242);
	CHECK(conn.init_flags == 2);

	mt_pvr_features_init(&features, 1);
	CHECK(mt_pvr_feature_u32(&features, MT_PVR_FEATURE_CORE_COUNT) == 1);
	/* The DDK feature set must stay below 2: a different value sends the
	 * driver down an unvalidated allocation path.
	 */
	CHECK(mt_pvr_feature_u32(&features, MT_PVR_FEATURE_SET) < 2);
	/* The opt-in switch: 0 is a no-op, non-zero lands at +0x54 only. */
	mt_pvr_features_set_ddk(&features, 0);
	CHECK(mt_pvr_feature_u32(&features, MT_PVR_FEATURE_SET) == 0);
	mt_pvr_features_set_ddk(&features, 2);
	CHECK(mt_pvr_feature_u32(&features, MT_PVR_FEATURE_SET) == 2);
	CHECK(mt_pvr_feature_u32(&features, MT_PVR_FEATURE_CORE_COUNT) == 1);
	mt_pvr_features_init(&features, 1);
	CHECK(mt_pvr_feature_u32(&features, MT_PVR_FEATURE_SET) < 2);
	/* Reads past the end return zero instead of walking off the blob. */
	CHECK(mt_pvr_feature_u32(&features, MT_PVR_FEATURE_BYTES) == 0);
	CHECK(mt_pvr_feature_u32(&features, MT_PVR_FEATURE_BYTES - 2) == 0);

	/* Connect must answer with the value the offline session actually
	 * accepted. The three sub-gates below are properties of that constant
	 * and worth pinning; a fourth comparison the decompilation shows
	 * (V == strtol("4")) is not, because it cannot hold at the same time
	 * as top16 == 0x23 -- yet the value demonstrably worked, so the
	 * reconstruction there is wrong and the test does not encode it.
	 */
	mt_pvr_connect_result(&result);
	CHECK(mt_pvr_bvnc_allowed(result.packed_bvnc));
	CHECK(result.packed_bvnc == 0x0023000406600017ULL);
	CHECK((result.packed_bvnc >> 48) == 0x23);
	CHECK(((result.packed_bvnc >> 16) & 0xffff) == 0x660);
	CHECK((u16)result.packed_bvnc == 0x17);
	CHECK(result.error == 0);
	CHECK(mt_pvr_bvnc_allowed(0x0001000000000000ULL));
	CHECK(!mt_pvr_bvnc_allowed(0));
	CHECK(!mt_pvr_bvnc_allowed(0x0023000406600018ULL));

	/* Info page: the driver rejects the page unless these three fields are
	 * right (decompiled.c:92477, 9247d). A static buffer keeps this out of
	 * the heap so a failing CHECK cannot leak.
	 */
	memset(page, 0, sizeof(page));
	mt_pvr_info_page_init(page, sizeof(page));
	CHECK(page[0x00 / 4] == 1);
	CHECK(page[0x44 / 4] == 0xb57);
	CHECK(page[0x48 / 4] == 0x688a847);
	/* A short buffer must be left alone entirely. */
	memset(page, 0, sizeof(page));
	mt_pvr_info_page_init(page, 8);
	CHECK(page[0x00 / 4] == 0);
	CHECK(page[0x44 / 4] == 0);
	return 0;
}

static int test_rgx_app_table(void)
{
	struct mt_pvr_heap_table table;
	u32 index = 0xffffffff;

	/* The bridge must serve the vendor application blueprint, not the
	 * guest physical plan. In particular, the two allocations that blocked
	 * RGXCreateRenderContext have to resolve to 4 GiB heaps.
	 */
	mt_pvr_rgx_app_heaps_init(&table);
	CHECK(mt_pvr_heaps_count(&table) == MT_PVR_HEAP_COUNT);
	CHECK(mt_pvr_heaps_count(&table) == 15);
	CHECK(table.entries[0].base == 0x4000000000ULL);
	CHECK(table.entries[0].size == 274877906944ULL);
	CHECK(table.entries[1].base == 0x8000000000ULL);
	CHECK(table.entries[1].size == 137438953472ULL);
	CHECK(table.entries[3].base == 0xda00000000ULL);
	CHECK(table.entries[3].size == 4294967296ULL);
	CHECK(table.entries[3].reserved_size == 65536ULL);
	CHECK(table.entries[4].base == 0xe000000000ULL);
	CHECK(table.entries[4].size == 4294967296ULL);
	CHECK(table.entries[4].reserved_size == 65536ULL);
	CHECK(table.entries[8].base == 0xec00000000ULL);
	CHECK(table.entries[8].size == 2097152ULL);
	CHECK(table.entries[14].base == 0xf200000000ULL);
	CHECK(table.entries[14].size == 2097152ULL);
	CHECK(mt_pvr_heaps_find(&table, "PDS Code and Data", &index) == 0 && index == 3);
	CHECK(mt_pvr_heaps_find(&table, "USC Code", &index) == 0 && index == 4);
	CHECK(mt_pvr_heaps_find(&table, "General", &index) == 0 && index == 1);
	CHECK(mt_pvr_heaps_find(&table, "Component Control", &index) == 0 && index == 7);
	CHECK(mt_pvr_heaps_find(&table, "FBCDC", &index) == 0 && index == 8);
	CHECK(mt_pvr_heaps_find(&table, "Texture State", &index) == 0 && index == 13);
	for (index = 0; index < mt_pvr_heaps_count(&table); index++) {
		CHECK(table.entries[index].name != NULL);
		CHECK(table.entries[index].base != 0);
		CHECK(table.entries[index].size != 0);
	}
	return 0;
}

static int test_transfer_fill_parse(void)
{
	/* r178 pool algebra: pool = HEAD + pixels*4 + TAIL, both measured. */
	static mt_tf_u8 pool[MT_TRANSFER_POOL_HEAD + 16 + MT_TRANSFER_POOL_TAIL];
	struct mt_transfer_surface s;
	struct mt_transfer_fill_rect r;
	mt_tf_u32 color = 0xff0000ffU;

	memset(pool, 0, sizeof(pool));
	memcpy(pool + MT_TRANSFER_POOL_HEAD, &color, sizeof(color));
	CHECK(mt_transfer_pool_parse(pool, sizeof(pool), &s) == 0);
	CHECK(s.pixels == 4);
	CHECK(s.color == 0xff0000ffU);
	/* Example VA passthrough (pool reservation VA at live time). */
	CHECK(mt_transfer_fill_rect(&r, 0x8000a00000ULL, 1280, 1024,
				    s.color, 1310720) == 0);
	CHECK(r.dst_va == 0x8000a00000ULL);
	CHECK(r.width == 1280 && r.height == 1024);
	/* Rejects: nulls, short/non-multiple/empty pools, wrong splits. */
	CHECK(mt_transfer_pool_parse(NULL, sizeof(pool), &s) == -EINVAL);
	CHECK(mt_transfer_pool_parse(pool, sizeof(pool), NULL) == -EINVAL);
	CHECK(mt_transfer_pool_parse(pool, MT_TRANSFER_POOL_HEAD, &s) == -EINVAL);
	CHECK(mt_transfer_pool_parse(pool, sizeof(pool) - 1, &s) == -EINVAL);
	CHECK(mt_transfer_pool_parse(pool, MT_TRANSFER_POOL_HEAD +
				     MT_TRANSFER_POOL_TAIL, &s) == -EINVAL);
	CHECK(mt_transfer_fill_rect(NULL, 0, 1280, 1024, color, 4) == -EINVAL);
	CHECK(mt_transfer_fill_rect(&r, 0, 0, 1024, color, 4) == -EINVAL);
	CHECK(mt_transfer_fill_rect(&r, 0, 1024, 1024, color, 4) == -EINVAL);
	CHECK(mt_transfer_fill_rect(&r, 0, 2, 2, color, 0) == -EINVAL);
	return 0;
}

static int test_tqx_fill_build_deterministic(void)
{
	/* r181: the destination block inside mt_tqx_fill_build was left
	 * stack-uninitialized (worked live only because fresh kernel/user
	 * stacks read back zeros). Builds must succeed repeatably with
	 * identical bytes; pollute the stack between the two calls so reuse
	 * cannot hide behind a clean frame.
	 */
	struct mt_tqx_fill_input fi = {
		.destination_va = 0x8000a00000ULL, .command_va = 0x48000000ULL,
		.element_bytes = 4, .width = 1280, .height = 1024,
		.x = 0, .y = 0, .rect_width = 1280, .rect_height = 1024,
		.color = { 0xff0000ffU, 0, 0, 0 },
	};
	static u8 p1[512], p2[512];
	volatile u8 trash[512];
	u32 i;

	memset((void *)trash, 0xA5, sizeof(trash));
	for (i = 0; i < sizeof(trash); i++)
		if (trash[i] != 0xA5)
			return 1;
	CHECK(mt_tqx_fill_build(p1, sizeof(p1), &fi) == 0);
	memset((void *)trash, 0x5A, sizeof(trash));
	CHECK(mt_tqx_fill_build(p2, sizeof(p2), &fi) == 0);
	CHECK(memcmp(p1, p2, sizeof(p1)) == 0);
	return 0;
}

static int test_ccb_find_dst_va_roundtrip(void)
{
	/* r304: the CCB destination scan must recover the VA a fill image
	 * was built with (including nonzero high bits), and refuse a
	 * zeroed window loudly. Same layout the UMD generator reproduces.
	 */
	struct mt_tqx_fill_input fi = {
		.destination_va = 0x8000a00000ULL, .command_va = 0x48000000ULL,
		.element_bytes = 4, .width = 1280, .height = 1024,
		.x = 0, .y = 0, .rect_width = 1280, .rect_height = 1024,
		.color = { 0xff0000ffU, 0, 0, 0 },
	};
	static u8 img[512];
	u64 va = 0;
	int i;

	memset(img, 0, sizeof(img));
	CHECK(mt_tqx_fill_build(img, sizeof(img), &fi) == 0);
	CHECK(mt_ccb_find_dst_va(img, sizeof(img), &va) == 0);
	CHECK(va == 0x8000a00000ULL);
	memset(img, 0, sizeof(img));
	CHECK(mt_ccb_find_dst_va(img, sizeof(img), &va) != 0);
	CHECK(mt_ccb_find_dst_va(NULL, sizeof(img), &va) == -EINVAL);
	for (i = 0; i < 4; i++)
		img[i] = 0xFF;
	CHECK(mt_ccb_find_dst_va(img, sizeof(img), &va) != 0);
	return 0;
}

/* r417: behavioral C tests for TA real-packet builders and the T2
 * readback pixel analysis. Appended to tests/c/pvr_bridge_core_test.c
 * before main() by the r417 patch script; registered in main(). */

static int test_ta_entry_simple_build_validation(void)
{
	/* r417: mt_ta_entry_simple_build rejects bad input. The 0xFD
	 * ioctl forwards width/height unchecked and relies on this. */
	struct mt_ta_entry_simple e;

	CHECK(mt_ta_entry_simple_build(NULL, 64, 64) == -EINVAL);
	CHECK(mt_ta_entry_simple_build(&e, 0, 64) == -EINVAL);
	CHECK(mt_ta_entry_simple_build(&e, 64, 0) == -EINVAL);
	CHECK(mt_ta_entry_simple_build(&e, 0x8001, 64) == -EINVAL);
	CHECK(mt_ta_entry_simple_build(&e, 64, 0x8001) == -EINVAL);
	CHECK(mt_ta_entry_simple_build(&e, 0x8000, 0x8000) == 0);

	/* Q2 packing [MEASURED] (r410 FUN_00169240:44304). */
	CHECK(mt_ta_entry_simple_build(&e, 64, 64) == 0);
	CHECK(e.q2_dims == (((u64)63 << 0x29) | ((u64)63 << 0x1a)));
	CHECK(e.q4 == (u64)(64 * 64 - 1));
	CHECK(e.q0_addr_flags == 0 && e.q1 == 0 && e.q3 == 0);
	return 0;
}

static int test_ta_entry_q0_flag_or(void)
{
	/* r419 corrected (r417 was wrong): Q0 is flags-only
	 * (FUN_00169240:44213/44317); the 48-bit target VA goes in Q1
	 * (uStack_88._0_6_=*(param_1+0x10), decompiled.c:44300).
	 * r418 OR-ed VA into Q0 and firmware timed out. */
	struct mt_ta_entry_simple e;

	/* NULL entry: no crash. */
	mt_ta_entry_simple_set_target(NULL, 0x7a001000ULL);

	CHECK(mt_ta_entry_simple_build(&e, 64, 64) == 0);
	mt_ta_entry_simple_set_target(&e, 0);
	CHECK(e.q0_addr_flags == MT_TA_ENTRY_Q0_FLAG_BITS);
	CHECK(e.q1 == 0);

	mt_ta_entry_simple_set_target(&e, 0x7a001000ULL);
	CHECK(e.q0_addr_flags == MT_TA_ENTRY_Q0_FLAG_BITS);
	CHECK(e.q1 == (0x7a001000ULL & 0xFFFFFFFFFFFFULL));
	/* Q0 carries no address bits. */
	CHECK((e.q0_addr_flags & 0xffffffffULL) == 0);

	/* 48-bit masking: bits above 48 are dropped. */
	mt_ta_entry_simple_set_target(&e, 0x1AB007a001000ULL);
	CHECK(e.q1 == 0xAB007a001000ULL);

	/* The constant touches only bits 39 and 42. */
	CHECK((MT_TA_ENTRY_Q0_FLAG_BITS & 0xffffffffULL) == 0);
	CHECK(MT_TA_ENTRY_Q0_FLAG_BITS == ((1ULL << 42) | (1ULL << 39)));
	return 0;
}

static int test_ta_real_buffer_build_target(void)
{
	/* r417: target_va forwarding. 0 keeps the legacy zero Q0
	 * (pre-r416 compatibility); nonzero ORs va|flag into every
	 * entry; the buffer tail stays zeroed. */
	static unsigned char buf[MT_TA_CMD_BUFFER_BYTES];
	struct mt_ta_entry_simple *e0, *e1;
	unsigned int i;

	CHECK(mt_ta_real_buffer_build(NULL, 64, 64, 1, 0) == -EINVAL);
	CHECK(mt_ta_real_buffer_build(buf, 0, 64, 1, 0) == -EINVAL);
	CHECK(mt_ta_real_buffer_build(buf, 64, 0, 1, 0) == -EINVAL);
	CHECK(mt_ta_real_buffer_build(buf, 64, 64, 0, 0) == -EINVAL);
	CHECK(mt_ta_real_buffer_build(buf, 64, 64, 10, 0) == -EINVAL);
	CHECK(mt_ta_real_buffer_build(buf, 64, 64,
				      MT_TA_REAL_MAX_ENTRIES, 0) == 0);

	/* target_va = 0: Q0 stays zero. */
	CHECK(mt_ta_real_buffer_build(buf, 64, 64, 1, 0) == 0);
	e0 = (struct mt_ta_entry_simple *)buf;
	CHECK(e0->q0_addr_flags == 0);
	CHECK(e0->q2_dims == (((u64)63 << 0x29) | ((u64)63 << 0x1a)));

	/* r419: target_va set: Q0=flags-only, Q1=va on every entry; tail zeroed. */
	CHECK(mt_ta_real_buffer_build(buf, 64, 64, 2, 0x7a001000ULL) == 0);
	e0 = (struct mt_ta_entry_simple *)(buf + 0);
	e1 = (struct mt_ta_entry_simple *)(buf + MT_TA_ENTRY_SIMPLE_BYTES);
	CHECK(e0->q0_addr_flags == MT_TA_ENTRY_Q0_FLAG_BITS);
	CHECK(e0->q1 == 0x7a001000ULL);
	CHECK(e1->q0_addr_flags == MT_TA_ENTRY_Q0_FLAG_BITS);
	CHECK(e1->q1 == 0x7a001000ULL);
	for (i = 2 * MT_TA_ENTRY_SIMPLE_BYTES; i < MT_TA_CMD_BUFFER_BYTES; i++)
		CHECK(buf[i] == 0);

	/* 9 entries exactly fill the 360B buffer. */
	CHECK(mt_ta_real_buffer_build(buf, 64, 64, 9, 0x1000ULL) == 0);
	e0 = (struct mt_ta_entry_simple *)(buf + 8 * MT_TA_ENTRY_SIMPLE_BYTES);
	CHECK(e0->q0_addr_flags == MT_TA_ENTRY_Q0_FLAG_BITS);
	CHECK(e0->q1 == 0x1000ULL);
	return 0;
}

static int test_ta_readback_analyze(void)
{
	/* r417: pixel analysis unit tests. Behavior locked to the r416
	 * tool: black counts as a distinct color (but not nonzero);
	 * distinct colors cap at 16; alpha is ignored. */
	struct ta_readback_stats st;
	static const uint8_t zeros[16] = { 0 };
	uint8_t red4[16];
	uint8_t mixed[16];
	uint8_t many[21 * 4];
	unsigned int i;

	/* All zero: nonzero=0, one distinct color (black quirk). */
	ta_readback_analyze(zeros, 4, &st);
	CHECK(st.nonzero == 0);
	CHECK(st.n_distinct == 1);
	CHECK(st.distinct[0] == 0);

	/* One color, varying alpha: alpha ignored. */
	for (i = 0; i < 4; i++) {
		red4[i * 4 + 0] = 0xff;
		red4[i * 4 + 1] = 0x00;
		red4[i * 4 + 2] = 0x00;
		red4[i * 4 + 3] = (uint8_t)(i * 40);
	}
	ta_readback_analyze(red4, 4, &st);
	CHECK(st.nonzero == 4);
	CHECK(st.n_distinct == 1);
	CHECK(st.distinct[0] == 0xff0000);

	/* red, green, blue, black. */
	mixed[0] = 0xff; mixed[1] = 0x00; mixed[2] = 0x00; mixed[3] = 0xff;
	mixed[4] = 0x00; mixed[5] = 0xff; mixed[6] = 0x00; mixed[7] = 0xff;
	mixed[8] = 0x00; mixed[9] = 0x00; mixed[10] = 0xff; mixed[11] = 0xff;
	mixed[12] = 0x00; mixed[13] = 0x00; mixed[14] = 0x00; mixed[15] = 0xff;
	ta_readback_analyze(mixed, 4, &st);
	CHECK(st.nonzero == 3);
	CHECK(st.n_distinct == 4);

	/* 21 distinct colors: capped at 16, nonzero still exact. */
	for (i = 0; i < 21; i++) {
		many[i * 4 + 0] = (uint8_t)(i + 1);
		many[i * 4 + 1] = 0x00;
		many[i * 4 + 2] = 0x00;
		many[i * 4 + 3] = 0xff;
	}
	ta_readback_analyze(many, 21, &st);
	CHECK(st.nonzero == 21);
	CHECK(st.n_distinct == TA_READBACK_MAX_DISTINCT);

	/* Empty input: no crash, zero stats. */
	ta_readback_analyze(NULL, 0, &st);
	CHECK(st.nonzero == 0 && st.n_distinct == 0);
	return 0;
}

int main(void)
{
	CHECK(test_wire_offsets() == 0);
	CHECK(test_device_layout() == 0);
	CHECK(test_queue_basic() == 0);
	CHECK(test_queue_full_and_wrap() == 0);
	CHECK(test_queue_fault() == 0);
	CHECK(test_offset_codec() == 0);
	CHECK(test_handles() == 0);
	CHECK(test_mmap_page_count() == 0);
	CHECK(test_heap_table() == 0);
	CHECK(test_rgx_app_table() == 0);
	CHECK(test_transfer_fill_parse() == 0);
	CHECK(test_tqx_fill_build_deterministic() == 0);
	CHECK(test_ccb_find_dst_va_roundtrip() == 0);
	CHECK(test_ta_entry_simple_build_validation() == 0);
	CHECK(test_ta_entry_q0_flag_or() == 0);
	CHECK(test_ta_real_buffer_build_target() == 0);
	CHECK(test_ta_readback_analyze() == 0);
	printf("pvr_bridge_core_test OK (%d checks)\n", checks);
	return 0;
}
