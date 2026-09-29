/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_GFX_PACKET_H
#define MT_GUEST_GFX_PACKET_H
#include "mt_gfx_registers.h"

#define MT_GFX_PACKET_BYTES 0x46c0U
#define MT_GFX_RECORD_BYTES 0x3580U
#define MT_GFX_RECORD_OFFSET 0x90U
#define MT_GFX_CSW_OFFSET 0x3620U

struct mt_gfx_packet_source {
	struct mt_gfx_register_source registers;
	u8 batch[0x798];
	u32 render_word;
};
struct mt_gfx_packet_input {
	u64 dma_va, state_va, job_ref;
	u32 frame, pid;
};
static inline u32 mt_gfx_get32(const u8 *b, u32 off)
{
	u32 v;
	memcpy(&v, b + off, 4);
	return v;
}
static inline u64 mt_gfx_get64(const u8 *b, u32 off)
{
	u64 v;
	memcpy(&v, b + off, 8);
	return v;
}
static inline void mt_gfx_put32(u8 *b, u32 off, u32 v)
{
	memcpy(b + off, &v, 4);
}
static inline void mt_gfx_put64(u8 *b, u32 off, u64 v)
{
	memcpy(b + off, &v, 8);
}
static inline int mt_gfx_overlap(const void *a, u32 an, const void *b, u32 bn)
{
	unsigned long x = (unsigned long)a, y = (unsigned long)b;
	return x >= y ? x - y < bn : y - x < an;
}

/* Windows UMD 157d80 -> 181ec0 -> 15afa0 -> 15b880, family 2,
 * one TA/3D batch, one core, no indirect/fence/instrumentation lists.
 * CPU staging only: no ioctl, GPU address ownership validation, shader/VDM
 * generation, render-context initialization, upload or publication. The CSW
 * is the original serializer's initial zero state, not proof that a hardware
 * rendering context is usable. Callers must establish those separately.
 * Raw metadata retains source offsets until its resource producers are traced.
 */
static inline int mt_gfx_packet_encode(void *out, u32 capacity,
		const struct mt_device_profile *profile,
		const struct mt_gfx_packet_source *source,
		const struct mt_gfx_packet_input *input)
{
	static const u32 batch_qwords[][2] = {
		{0x88,0x560}, {0x58,0x6f0}, {0xf8,0x6f0},
		{0x20,0x6a8}, {0x28,0x6b0}, {0x18,0x6c8},
		{0x30,0x6c0}, {0x38,0x6b8}, {0xb8,0x720},
		{0xd0,0x718}, {0xd8,0x710}};
	static const u32 metadata_dwords[][2] = {
		{0x160,0x10}, {0x194,0x20}, {0x198,0x24},
		{0x19c,0x50}, {0x1a0,0x60}, {0x1a4,0x40}};
	static const u32 render_qwords[][2] = {
		{0x260,0x190}, {0x268,0x198}, {0x270,0x1a0},
		{0x238,0x1b0}, {0x240,0x1b8}, {0x248,0x1c0},
		{0x218,0x220}, {0x220,0x228}, {0x228,0x230},
		{0x1f0,0x238}, {0x1e8,0x240}, {0x1e0,0x248},
		{0x1d8,0x250}, {0x1f8,0x258}, {0x200,0x260},
		{0x208,0x2e0}};
	static const u32 optional_counts[] = {0x618,0x670,0x750,0x760,0x770,0x780};
	u8 *p = out, *r;
	const u8 *m, *b, *rt;
	u32 i, flags, value;
	u64 q;
	int ret;
	if (!out || capacity < MT_GFX_PACKET_BYTES || !source || !input)
		return -EINVAL;
	if (!profile || profile->family != 2 || profile->primary_version != 2)
		return -EOPNOTSUPP;
	if (mt_gfx_overlap(out, MT_GFX_PACKET_BYTES, source, sizeof(*source)) ||
	    mt_gfx_overlap(out, MT_GFX_PACKET_BYTES, input, sizeof(*input)) ||
	    mt_gfx_overlap(out, MT_GFX_PACKET_BYTES, profile, sizeof(*profile)))
		return -EINVAL;
	m = source->registers.metadata;
	b = source->batch;
	rt = source->registers.render;
	if (mt_gfx_get32(rt, 0x148) > 8)
		return -E2BIG;
	if (mt_gfx_get32(m, 0x80) || b[0x61c])
		return -EOPNOTSUPP;
	for (i = 0; i < sizeof(optional_counts) / sizeof(optional_counts[0]); i++)
		if (mt_gfx_get32(b, optional_counts[i]))
			return -EOPNOTSUPP;
	if ((input->dma_va & 4095) || input->dma_va < 0x40000000ULL ||
	    input->dma_va > 0x8040000000ULL - MT_GFX_PACKET_BYTES)
		return -ERANGE;
	memset(p, 0, MT_GFX_PACKET_BYTES);
	mt_gfx_put64(p, 0x10, input->dma_va + MT_GFX_CSW_OFFSET);
	mt_gfx_put32(p, 0x1c, 0x66);
	mt_gfx_put32(p, 0x28, 0x48);
	mt_gfx_put32(p, 0x2c, 2);
	mt_gfx_put32(p, 0x30, input->frame);
	mt_gfx_put64(p, 0x38, input->job_ref);
	mt_gfx_put32(p, 0x40, input->pid);
	mt_gfx_put32(p, 0x48, 8);
	mt_gfx_put64(p, 0x50, input->state_va);
	mt_gfx_put32(p, 0x60, 5);
	mt_gfx_put32(p, 0x68, 0x78);
	mt_gfx_put32(p, 0x70, 0x3598);
	mt_gfx_put32(p, 0x80, 0x18);
	mt_gfx_put32(p, 0x84, 1);
	mt_gfx_copy_word(p, 0x88, b, 0x558, 4);
	mt_gfx_put32(p, MT_GFX_CSW_OFFSET, 1);
	mt_gfx_put32(p, MT_GFX_CSW_OFFSET + 0x1008, 0x10a0);
	mt_gfx_put32(p, MT_GFX_CSW_OFFSET + 0x100c, 1);
	mt_gfx_put32(p, MT_GFX_CSW_OFFSET + 0x1010, 0x1020);
	r = p + MT_GFX_RECORD_OFFSET;
	mt_gfx_put32(r, 8, MT_GFX_RECORD_BYTES);
	mt_gfx_put32(r, 0xa8, MT_GFX_RECORD_BYTES);
	for (i = 0; i < sizeof(batch_qwords) / sizeof(batch_qwords[0]); i++)
		mt_gfx_copy_word(r, batch_qwords[i][0], b, batch_qwords[i][1], 8);
	q = mt_gfx_get64(b, 0x560);
	mt_gfx_put64(r, 0x128, q ? q + 0x10 : 0);
	flags = b[0x620];
	value = (flags & 7) | ((flags & 8) << 2) | ((flags & 0x10) << 8) |
		((flags & 0x20) << 4) | ((flags & 0x40) << 5) | ((flags & 0x80) << 9);
	mt_gfx_put32(r, 0x140, value);
	mt_gfx_put32(r, 0x60, 2);
	mt_gfx_put32(r, 0x100, 4);
	mt_gfx_copy_word(r, 0x148, b, 0x558, 4);
	mt_gfx_put32(r, 0x154, 4);
	for (i = 0; i < sizeof(metadata_dwords) / sizeof(metadata_dwords[0]); i++)
		mt_gfx_copy_word(r, metadata_dwords[i][0], m, metadata_dwords[i][1], 4);
	mt_gfx_put32(r, 0x164, source->render_word);
	mt_gfx_copy_word(r, 0x170, m, 0x78, 8);
	mt_gfx_copy_word(r, 0x178, m, 0x28, 8);
	mt_gfx_copy_word(r, 0x190, rt, 0x390, 4);
	if (mt_gfx_get64(m, 0)) {
		flags = mt_gfx_get32(b, 0x5c8);
		value = (flags & 3) | ((flags & 4) << 2) | ((flags & 0x30) << 7);
		mt_gfx_put32(r, 0x144, value);
		mt_gfx_copy_word(r, 0x130, b, 0x690, 8);
		mt_gfx_copy_word(r, 0x258, rt, 0x188, 4);
		mt_gfx_copy_word(r, 0x230, rt, 0x1a8, 4);
		mt_gfx_copy_word(r, 0x168, rt, 0x200, 4);
		mt_gfx_copy_word(r, 0x16c, rt, 0x148, 1);
		mt_gfx_copy_word(r, 0x1d4, rt, 0x218, 4);
		for (i = 0; i < sizeof(render_qwords) / sizeof(render_qwords[0]); i++)
			mt_gfx_copy_word(r, render_qwords[i][0], rt, render_qwords[i][1], 8);
		value = mt_gfx_get32(rt, 0);
		mt_gfx_put32(r, 0x1d0, ((value & 1) << 3) | (value & 2) |
			((value & 4) << 3) | ((value & 8) << 1));
		if (flags & 1)
			mt_gfx_copy_word(r, 0x280, rt, 0x2f0, 64);
	}
	/* All register validation is complete before any output is changed. */
	ret = mt_gfx_registers_encode(r + 0x3370, MT_GFX_REG_BYTES,
			profile, &source->registers);
	return ret;
}
#endif
