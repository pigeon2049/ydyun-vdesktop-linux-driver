/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_TRANSFER_FILL_H
#define MT_TRANSFER_FILL_H
/* T3-transfer fill-input builder (r179).
 *
 * Geometry and color travel in the UMD's pixel-pool PMR contents, not in the
 * CCB (r178): [HEAD zeros][W*H 4-byte pixels][TAIL zeros]. The offsets below
 * are measured on the fill path (1280x1024 and 64x64 both satisfy
 * pool = HEAD + pixels*4 + TAIL exactly); other paths may lay pools out
 * differently, so the parser validates the arithmetic instead of trusting it.
 *
 * Width/height factorization is deliberately NOT derived here: a flat pixel
 * count cannot split W from H. The caller supplies both and the builder
 * rejects mismatches (w*h != pixels), so a wrong factorization fails loudly
 * at build time instead of mis-filling on hardware.
 *
 * Pure bytes: kernel and userspace share this file; only integer ops and
 * memcpy are used. No locks, no allocation, no hardware.
 */
#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/types.h>
typedef u8 mt_tf_u8;
typedef u64 mt_tf_u64;
typedef u32 mt_tf_u32;
#else
#include <stdint.h>
#include <string.h>
typedef uint8_t mt_tf_u8;
typedef uint64_t mt_tf_u64;
typedef uint32_t mt_tf_u32;
#endif

/* Measured pool framing on the fill path (r178). */
#define MT_TRANSFER_POOL_HEAD 3841U
#define MT_TRANSFER_POOL_TAIL 254U
#define MT_TRANSFER_PIXEL_BYTES 4U

struct mt_transfer_surface {
	/* Pixel count (pool head/tail stripped, in 4-byte pixels). */
	mt_tf_u64 pixels;
	/* First pixel word, copied verbatim; channel order uninterrupted. */
	mt_tf_u32 color;
};

struct mt_transfer_fill_rect {
	mt_tf_u64 dst_va;
	mt_tf_u32 width, height, color;
};

/* Parse a pixel pool into {pixels, color}. Returns 0 or -EINVAL. */
static inline int mt_transfer_pool_parse(const mt_tf_u8 *pool, mt_tf_u64 len,
					 struct mt_transfer_surface *out)
{
	mt_tf_u64 body, pixels;
	mt_tf_u32 color;

	if (!pool || !out || len <= MT_TRANSFER_POOL_HEAD + MT_TRANSFER_POOL_TAIL)
		return -EINVAL;
	body = len - MT_TRANSFER_POOL_HEAD - MT_TRANSFER_POOL_TAIL;
	if (body % MT_TRANSFER_PIXEL_BYTES)
		return -EINVAL;
	pixels = body / MT_TRANSFER_PIXEL_BYTES;
	if (!pixels)
		return -EINVAL;
	memcpy(&color, pool + MT_TRANSFER_POOL_HEAD, sizeof(color));
	out->pixels = pixels;
	out->color = color;
	return 0;
}

/* Build a full-surface fill rect. Width/height come from the caller (see
 * header note); the pixel-count cross-check rejects wrong splits loudly.
 * Returns 0 or -EINVAL.
 */
static inline int mt_transfer_fill_rect(struct mt_transfer_fill_rect *out,
					mt_tf_u64 dst_va, mt_tf_u32 width,
					mt_tf_u32 height, mt_tf_u32 color,
					mt_tf_u64 pixels)
{
	if (!out || !width || !height || !pixels)
		return -EINVAL;
	if ((mt_tf_u64)width * (mt_tf_u64)height != pixels)
		return -EINVAL;
	out->dst_va = dst_va;
	out->width = width;
	out->height = height;
	out->color = color;
	return 0;
}

#endif
