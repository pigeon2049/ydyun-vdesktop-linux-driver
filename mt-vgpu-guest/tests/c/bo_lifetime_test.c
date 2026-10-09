#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../kernel/mt_bo.h"

struct store {
	unsigned fail_alloc, fail_clear, fail_map, bad_backing;
	unsigned allocated, freed, mapped, unmapped;
	u64 bytes;
};
static int alloc(void *opaque, u32 bytes, u32 alignment, struct mt_bo_backing *out)
{
	struct store *s = opaque;
	void *p;
	if (s->fail_alloc)
		return -ENOSPC;
	p = malloc(bytes);
	assert(p);
	memset(p, 0xa5, bytes);
	*out = (struct mt_bo_backing){.handle = p, .gpu_pa = 0x608000000ULL,
		.bar_offset = 0x3000000, .bytes = bytes};
	assert(!(out->gpu_pa & (alignment - 1)));
	if (s->bad_backing)
		out->gpu_pa++;
	s->allocated++;
	s->bytes += bytes;
	return 0;
}
static int clear(void *opaque, const struct mt_bo_backing *b)
{
	struct store *s = opaque;
	if (s->fail_clear)
		return -EIO;
	memset(b->handle, 0, b->bytes);
	return 0;
}
static void release(void *opaque, const struct mt_bo_backing *b)
{
	struct store *s = opaque;
	s->freed++;
	s->bytes -= b->bytes;
	free(b->handle);
}
static int map(void *opaque, const struct mt_bo_backing *b, void **address)
{
	struct store *s = opaque;
	if (s->fail_map)
		return -ENOMEM;
	s->mapped++;
	*address = b->handle;
	return 0;
}
static void unmap(void *opaque, const struct mt_bo_backing *b)
{
	struct store *s = opaque;
	(void)b;
	s->unmapped++;
}
static const struct mt_bo_ops ops = {alloc, clear, release, map, unmap};

int main(void)
{
	struct store s = {0};
	struct mt_bo bo = {0};
	void *p = (void *)1, *q;
	unsigned i, owner_refs = 1, cpu = 0, gpu = 0;
	u32 random = 12345;
	const u64 invalid_sizes[] = {0, 0xfffff001ULL, 1ULL << 32, ~(u64)0};
	for (i = 0; i < sizeof(invalid_sizes) / sizeof(invalid_sizes[0]); i++)
		assert(mt_bo_create(&bo, &ops, &s, invalid_sizes[i], 4096) == -EINVAL);
	assert(mt_bo_create(&bo, &ops, &s, 4096, 8193) == -EINVAL);
	assert(!s.allocated && !bo.refs);
	s.fail_alloc = 1;
	assert(mt_bo_create(&bo, &ops, &s, 4097, 4096) == -ENOSPC && !bo.refs);
	s.fail_alloc = 0;
	s.fail_clear = 1;
	assert(mt_bo_create(&bo, &ops, &s, 4097, 4096) == -EIO && !bo.refs);
	assert(s.allocated == s.freed && !s.bytes);
	s.fail_clear = 0;
	s.bad_backing = 1;
	assert(mt_bo_create(&bo, &ops, &s, 4097, 4096) == -ERANGE && !bo.refs);
	assert(s.allocated == s.freed && !s.bytes);
	s.bad_backing = 0;
	assert(!mt_bo_create(&bo, &ops, &s, 4097, 4096));
	assert(bo.backing.bytes == 8192 && bo.requested_bytes == 4097);
	assert(!mt_bo_check_range(&bo, 4096, 4096));
	assert(mt_bo_check_range(&bo, 8191, 2) == -ERANGE);
	assert(mt_bo_check_range(&bo, ~(u64)0, 2) == -ERANGE);
	assert(mt_bo_check_range(&bo, 1, ~(u64)0) == -ERANGE);
	s.fail_map = 1;
	assert(mt_bo_cpu_begin(&bo, &p) == -ENOMEM && bo.refs == 1 && p == (void *)1);
	s.fail_map = 0;
	assert(!mt_bo_cpu_begin(&bo, &p) && !mt_bo_cpu_begin(&bo, &q) && p == q);
	for (i = 0; i < 8192; i++)
		assert(!((u8 *)p)[i]);
	assert(mt_bo_gpu_begin(&bo) == -EBUSY);
	assert(!mt_bo_put(&bo)); /* Closing the last handle does not revoke maps. */
	assert(bo.refs == 2 && mt_bo_put(&bo) == -EBUSY);
	assert(!mt_bo_cpu_end(&bo) && bo.refs == 1);
	assert(!mt_bo_cpu_end(&bo) && !bo.refs && !s.bytes);
	assert(s.mapped == s.unmapped && s.allocated == s.freed);
	assert(mt_bo_put(&bo) == -ENOENT && mt_bo_cpu_end(&bo) == -EINVAL);
	assert(!mt_bo_create(&bo, &ops, &s, 4096, 4096));
	assert(!mt_bo_gpu_begin(&bo) && !mt_bo_put(&bo));
	assert(bo.refs == 1 && bo.gpu_users == 1 && s.bytes == 4096);
	assert(mt_bo_cpu_begin(&bo, &p) == -EBUSY && mt_bo_gpu_begin(&bo) == -EBUSY);
	assert(mt_bo_put(&bo) == -EBUSY);
	assert(!mt_bo_gpu_end(&bo) && !bo.refs && !s.bytes);
	assert(!mt_bo_create(&bo, &ops, &s, 4096, 4096));
	bo.refs = ~(u32)0;
	assert(mt_bo_get(&bo) == -EOVERFLOW && mt_bo_cpu_begin(&bo, &p) == -EOVERFLOW);
	assert(mt_bo_gpu_begin(&bo) == -EOVERFLOW && !bo.cpu_users && !bo.gpu_users);
	bo.refs = 1;
	/* Retain one owner and compare interleaved operations with independent
	 * owner/map/GPU counts; this is serialized, not a concurrency simulation. */
	for (i = 0; i < 10000; i++) {
		unsigned op;
		random = random * 1664525U + 1013904223U;
		op = (random >> 16) % 6;
		if (op == 0) { assert(!mt_bo_get(&bo)); owner_refs++; }
		if (op == 1 && owner_refs > 1) { assert(!mt_bo_put(&bo)); owner_refs--; }
		if (op == 2) {
			int ret = mt_bo_cpu_begin(&bo, &p);
			if (gpu) assert(ret == -EBUSY); else { assert(!ret); cpu++; }
		}
		if (op == 3 && cpu) { assert(!mt_bo_cpu_end(&bo)); cpu--; }
		if (op == 4) {
			int ret = mt_bo_gpu_begin(&bo);
			if (cpu || gpu) assert(ret == -EBUSY); else { assert(!ret); gpu++; }
		}
		if (op == 5 && gpu) { assert(!mt_bo_gpu_end(&bo)); gpu--; }
		assert(bo.refs == owner_refs + cpu + gpu && bo.cpu_users == cpu && bo.gpu_users == gpu);
	}
	while (cpu--) assert(!mt_bo_cpu_end(&bo));
	if (gpu) assert(!mt_bo_gpu_end(&bo));
	while (owner_refs--) assert(!mt_bo_put(&bo));
	assert(!bo.refs && !s.bytes && s.allocated == s.freed && s.mapped == s.unmapped);
	puts("PASS: BO fault rollback, zeroed padding, CPU/GPU lifetime, overflow guards and 10000 interleaved operations");
	return 0;
}
