/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_TQX_COPY_H
#define MT_GUEST_TQX_COPY_H
#include "mt_ce_copy.h"
#include "mt_tqx_texture.h"

#define MT_TQX_COPY_CHUNKS 4U
#define MT_TQX_SURFACE_BYTES 0x68U
#define MT_TQX_STATE_BYTES 0xa8U
struct mt_tqx_copy_input { u64 src, dst, bytes; };
struct mt_tqx_chunk { u64 offset; u32 element_bytes, layers, width, height; };
struct mt_tqx_copy_plan {
	u64 src, dst, bytes;
	u32 count, reserved;
	u32 operations[MT_TQX_COPY_CHUNKS];
	struct mt_tqx_chunk chunks[MT_TQX_COPY_CHUNKS];
	u8 surfaces[MT_TQX_COPY_CHUNKS][MT_TQX_SURFACE_BYTES];
	u8 states[MT_TQX_COPY_CHUNKS][MT_TQX_STATE_BYTES];
	u8 source_descriptors[MT_TQX_COPY_CHUNKS][MT_TQX_TEXTURE_BYTES];
};

/* 1400a5314, restricted to the byte-copy caller 14010f034. */
static inline void mt_tqx_chunk_shape(struct mt_tqx_chunk *c, u32 alignment)
{
	while (!(c->width & 1) && c->element_bytes < alignment) {
		c->element_bytes <<= 1;
		c->width >>= 1;
	}
	while (!(c->width & 1) && (c->height << 2) <= c->width) {
		c->height <<= 1;
		c->width >>= 1;
	}
}

/* 1400a5740 + 1400571e0. CPU descriptions, not GPU command packets.
 * A linear copy uses the same geometry for its source and destination. */
static inline void mt_tqx_chunk_surface(const struct mt_tqx_chunk *c,
		u8 *surface, u8 *state)
{
	static const u64 formats[] = {
		0x010000020000001aULL, 0x0100000200000047ULL,
		0x0100000200000062ULL, 0x0100030200000067ULL,
		0x0504030200000071ULL
	};
	u32 log = 0, b = c->element_bytes, i;
	while (b > 1) { b >>= 1; log++; }
	memset(surface, 0, MT_TQX_SURFACE_BYTES);
	memset(state, 0, MT_TQX_STATE_BYTES);
	mt_fw_put32(surface, 8, 1);
	mt_fw_put64(surface, 0xc, formats[log]);
	mt_fw_put32(surface, 0x14, c->width);
	mt_fw_put32(surface, 0x18, c->height);
	mt_fw_put32(surface, 0x1c, 1);
	mt_fw_put32(surface, 0x20, 1);
	mt_fw_put32(surface, 0x24, 1);
	mt_fw_put32(surface, 0x28, c->layers);
	mt_fw_put32(surface, 0x2c, c->layers);
	mt_fw_put64(state, 0xc, formats[log]);
	mt_fw_put32(state, 0x14, c->element_bytes << 3);
	for (i = 0; i < 4; i++) {
		mt_fw_put32(state, 0x1c + i * 12, c->width);
		mt_fw_put32(state, 0x20 + i * 12, c->height);
		mt_fw_put32(state, 0x24 + i * 12, 1);
	}
	mt_fw_put32(state, 0x4c, 1);
	mt_fw_put32(state, 0x80, log);
}

/* 14010f034 calls 1400a624c with {src, 0}, bytes, 1, 1. Thus the
 * alignment is exactly 16, independent of the destination address.
 * At most an alignment prefix plus three bit groups are produced.
 * Shared geometry for the full plan and the finalized multi-job stream. */
struct mt_tqx_chunk_plan {
	u32 count;
	struct mt_tqx_chunk chunks[MT_TQX_COPY_CHUNKS];
};
static inline int mt_tqx_copy_chunks_build(void *out, u32 capacity,
		const struct mt_tqx_copy_input *in)
{
	struct mt_tqx_chunk_plan plan = {0};
	u64 offset = 0, limit = 1ULL << MT_GPU_VA_BITS;
	u32 remaining, prefix, parts[3], i;
	if (!out || !in || capacity < sizeof(plan) || !in->bytes || in->bytes > 0xffffffffULL)
		return -EINVAL;
	if (in->src >= limit || in->dst >= limit ||
	    in->bytes > limit - in->src || in->bytes > limit - in->dst)
		return -ERANGE;
	remaining = in->bytes;
	prefix = (16 - (in->src & 15)) & 15;
	if (prefix && remaining > 4096) {
		plan.chunks[plan.count++] = (struct mt_tqx_chunk){0, 1, 1, prefix, 1};
		remaining -= prefix;
		offset += prefix;
	}
	parts[0] = remaining & 0xf8000000U;
	parts[1] = remaining & 0x07fff000U;
	parts[2] = remaining & 0x00000fffU;
	for (i = 0; i < 3; i++) {
		struct mt_tqx_chunk *c;
		if (!parts[i])
			continue;
		c = &plan.chunks[plan.count];
		*c = (struct mt_tqx_chunk){offset, 1, 1, parts[i], 1};
		mt_tqx_chunk_shape(c, i == 2 && !plan.count && prefix ? 1 : 16);
		plan.count++;
		offset += parts[i];
	}
	memcpy(out, &plan, sizeof(plan));
	return 0;
}

static inline int mt_tqx_copy_plan_build(void *out, u32 capacity,
		const struct mt_tqx_copy_input *in)
{
	struct mt_tqx_copy_plan plan = {0};
	struct mt_tqx_chunk_plan chunks;
	u32 i;
	int ret;
	if (!out || capacity < sizeof(plan))
		return -EINVAL;
	ret = mt_tqx_copy_chunks_build(&chunks, sizeof(chunks), in);
	if (ret)
		return ret;
	plan.src = in->src; plan.dst = in->dst; plan.bytes = in->bytes;
	plan.count = chunks.count;
	memcpy(plan.chunks, chunks.chunks, sizeof(plan.chunks));
	for (i = 0; i < plan.count; i++) {
		struct mt_tqx_texture_input texture = {
			.source_va=in->src + plan.chunks[i].offset,
			.element_bytes=plan.chunks[i].element_bytes,
			.width=plan.chunks[i].width, .height=plan.chunks[i].height};
		int ret = mt_tqx_texture_build(plan.source_descriptors[i],
				MT_TQX_TEXTURE_BYTES, &texture);
		if (ret)
			return ret;
		mt_tqx_chunk_surface(&plan.chunks[i], plan.surfaces[i], plan.states[i]);
		/* 140114418: equal single-layer byte-copy rectangles, surface type 1. */
		plan.operations[i] = plan.chunks[i].element_bytes <= 4 ? 8 :
			(plan.chunks[i].element_bytes == 8 ? 0x10 : 0x15);
	}
	memcpy(out, &plan, sizeof(plan));
	return 0;
}

static inline int mt_tqx_copy_prepare(void *out, u32 capacity,
		const struct mt_gpu_vm *vm, const struct mt_bo *src, const struct mt_bo *dst,
		const struct mt_tqx_copy_input *in)
{
	u64 src_pa, dst_pa;
	int ret;
	if (!in || !in->bytes || in->bytes > 0xffffffffULL)
		return -EINVAL;
	ret = mt_ce_copy_resolve(vm, src, in->src, in->bytes, &src_pa);
	if (ret)
		return ret;
	ret = mt_ce_copy_resolve(vm, dst, in->dst, in->bytes, &dst_pa);
	if (ret)
		return ret;
	if (src_pa < dst_pa + in->bytes && dst_pa < src_pa + in->bytes)
		return -EINVAL;
	return mt_tqx_copy_plan_build(out, capacity, in);
}
#endif
