/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_TRANSLATE_KICK_H
#define MT_TRANSLATE_KICK_H
/* Check-only translator packet envelope (r113/r147): an empty DM2 marker is
 * the verified Linux packet template with the kick sequence stamped at the
 * envelope header +0x08 (the same slot live_3d_drm writes frame_tag to).
 * Pure bytes: no locks, no allocation, no hardware. Kernel and userspace
 * share this file; only integer ops and memcpy are used.
 */
#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/types.h>
typedef u8 mt_tr_u8;
typedef u64 mt_tr_u64;
typedef u32 mt_tr_u32;
#else
#include <stdint.h>
#include <string.h>
typedef uint8_t mt_tr_u8;
typedef uint64_t mt_tr_u64;
typedef uint32_t mt_tr_u32;
#endif

/* Envelope header slot for the kick sequence (r113: tag == kick number). */
#define MT_TRANSLATE_TAG_OFFSET 0x08U
#define MT_TRANSLATE_TAG_BYTES 8U

/* Render-target fields the firmware requires to be VA-valid even for an empty
 * marker (r147: a zeroed template never completes; live_3d_drm patches the
 * same four slots to a bound scratch surface). 128x128 RGBA geometry matches
 * the live scratch surface. Applied once per command buffer at prepare time;
 * never read back, no pixel assertions.
 */
#define MT_TRANSLATE_RT_VA 0x48100000ULL
#define MT_TRANSLATE_RT_BYTES 65536U
#define MT_TRANSLATE_RT_STRIDE 512ULL
#define MT_TRANSLATE_RT_EXTENT (((128ULL) << 16) | 128ULL)
#define MT_TRANSLATE_RT_OFF_VA 0x45a0U
#define MT_TRANSLATE_RT_OFF_STRIDE 0x45a8U
#define MT_TRANSLATE_RT_OFF_EXTENT 0x45b0U
#define MT_TRANSLATE_RT_OFF_DIRECT 0x4668U

static inline void mt_translate_frame_emit(mt_tr_u8 *out, const mt_tr_u8 *tpl,
					   mt_tr_u32 tpl_bytes, mt_tr_u64 tag)
{
	memcpy(out, tpl, tpl_bytes);
	memcpy(out + MT_TRANSLATE_TAG_OFFSET, &tag, MT_TRANSLATE_TAG_BYTES);
}

static inline void mt_translate_rt_patch(mt_tr_u8 *cmd, mt_tr_u64 rt_va)
{
	mt_tr_u64 stride = MT_TRANSLATE_RT_STRIDE;
	mt_tr_u64 extent = MT_TRANSLATE_RT_EXTENT;

	memcpy(cmd + MT_TRANSLATE_RT_OFF_VA, &rt_va, 8);
	memcpy(cmd + MT_TRANSLATE_RT_OFF_STRIDE, &stride, 8);
	memcpy(cmd + MT_TRANSLATE_RT_OFF_EXTENT, &extent, 8);
	memcpy(cmd + MT_TRANSLATE_RT_OFF_DIRECT, &rt_va, 8);
}

#endif
