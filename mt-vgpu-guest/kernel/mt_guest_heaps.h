/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_HEAPS_H
#define MT_GUEST_HEAPS_H

#include "mt_mmu.h"

struct mt_guest_heap {
	u64 base, size, reserved_base, reserved_size;
};

struct mt_guest_resource {
	u64 va;
	u32 size, heap;
};

struct mt_guest_heap_plan {
	struct mt_guest_heap heaps[22];
	struct mt_guest_resource resources[13];
};

#define MT_GUEST_POOL_COUNT 3U
struct mt_guest_pool_spec { u64 va; u32 bytes, heap; };

/* 01dafc/01d98c after the immediate 01ccd8 reservations. These are
 * independent backing allocations, not views of static PDS/USC images. */
static inline void mt_guest_plan_pools(struct mt_guest_pool_spec out[MT_GUEST_POOL_COUNT])
{
	out[0] = (struct mt_guest_pool_spec){0x81ffd03000ULL, 0x200000, 1};
	out[1] = (struct mt_guest_pool_spec){0x84fff00000ULL, 0x100000, 2};
	out[2] = (struct mt_guest_pool_spec){0xf0ffe00000ULL, 0x200000, 10};
}

/* Reference S3000 Guest layout: 14001ccd8, mask 0x1ef9, MMU mode 0.
 * Reserve each heap tail on a 2 MiB boundary (the PT coverage from
 * 14002a3f4), then allocate immediate resources in declaration order.
 * A zero resource VA means deferred/unrequested, never a mapping at zero.
 * This computes GPU virtual ranges only. It does not allocate device memory.
 */
static inline void mt_guest_plan_heaps(struct mt_guest_heap_plan *out)
{
	static const u64 bases[11] = {
		0x40000000ULL, 0x8100000000ULL, 0x8400000000ULL,
		0xa000000000ULL, 0, 0, 0xe1c0000000ULL,
		0xec00000000ULL, 0xec40000000ULL, 0xeb00000000ULL, 0xf000000000ULL
	};
	static const u64 sizes[11] = {
		0x8000000000ULL, 0x100000000ULL, 0x100000000ULL, 0x1000000,
		0, 0, 0x100000000ULL, 0x8000, 0x1000, 0x100000000ULL, 0x100000000ULL
	};
	static const u32 resource_sizes[13] = {
		0x200000, 0, 0, 0x400000, 0x100000, 0x200000, 0x1000,
		0x2000, 0, 0x100000, 0x80000, 0x80000, 0x200000
	};
	static const u32 resource_heaps[13] = {3, 0, 0, 1, 1, 1, 1, 1, 0, 2, 2, 2, 10};
	u64 next[22] = {0};
	u32 i;
	memset(out, 0, sizeof(*out));
	for (i = 0; i < 11; i++) {
		out->heaps[i].base = bases[i];
		out->heaps[i].size = sizes[i];
	}
	for (i = 0; i < 13; i++) {
		out->resources[i].size = resource_sizes[i];
		out->resources[i].heap = resource_heaps[i];
		out->heaps[resource_heaps[i]].reserved_size += resource_sizes[i];
	}
	for (i = 0; i < 22; i++) {
		struct mt_guest_heap *h = &out->heaps[i];
		if (h->reserved_size) {
			h->reserved_size = (h->reserved_size + 0x1fffff) & ~0x1fffffULL;
			h->reserved_base = h->base + h->size - h->reserved_size;
			next[i] = h->reserved_base;
		}
	}
	for (i = 0; i < 13; i++) {
		struct mt_guest_resource *r = &out->resources[i];
		if (!r->size || ((1U << i) & 0x1220))
			continue;
		r->va = next[r->heap];
		next[r->heap] += r->size;
	}
}

#endif
