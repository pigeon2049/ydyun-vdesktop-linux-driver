/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_ADDR_PLAN_H
#define MT_ADDR_PLAN_H
/*
 * Single source of truth for the scene VA plan (r186).
 *
 * The live experiment modules (kernel/recovery/mt_live_*.c) and the bridge
 * translator (kernel/recovery/mt_pvr_bridge.c) build the SAME scene layout
 * in different address spaces: the TQX command/DMA/state triple, the
 * context-BO array, and the translator command buffer. These addresses used
 * to be hardcoded per file and already drifted once in practice (r182: the
 * bridge TQX block only works because its VAs match live_3d's).
 *
 * Change values here, never inline. Deliberately NOT included here:
 * stream copy endpoints (0x40100000/0x40200000, live-only scene internals),
 * firmware word markers (0x4.../0x5... tags in mt_ce_stream.h), BAR sizes,
 * and UAPI-derived numbers (bridge/ioctl IDs) -- those live with their
 * owners. Kernel-only (unlike mt_translate_kick.h, which is shared with
 * userspace tests).
 */
#include <linux/types.h>

/* TQX command/DMA/state triple (live_3d layout; the bridge binds the same). */
#define MT_TQX_CMD_VA 0x40000000ULL
#define MT_TQX_DMA_VA 0x40010000ULL
#define MT_TQX_STATE_VA 0x40020000ULL
#define MT_TQX_CMD_BO_BYTES 4096U
#define MT_TQX_DMA_BO_BYTES 8192U
#define MT_TQX_STATE_BO_BYTES 4096U

/* Context-BO array: base + 1MB stride, MT_GFX_CONTEXT_BO_COUNT entries. */
#define MT_CTX_BO_BASE_VA 0x50000000ULL
#define MT_CTX_BO_STRIDE 0x100000ULL

/* Translator scene (bridge-owned space): command buffer + space sizing. */
#define MT_TRANSLATE_CMD_VA 0x48000000ULL
#define MT_TRANSLATE_CMD_BYTES 32768U
#define MT_TRANSLATE_SPACE_PAGES 32U
#define MT_TRANSLATE_FENCE_WAIT_MS 5000U
#define MT_TRANSLATE_WAIT_SLICE_MS 5U

/* Prototype fill geometry (r178: 1280x1024 pool-validated rect). */
#define MT_TRANSFER_PROTO_W 1280U
#define MT_TRANSFER_PROTO_H 1024U

/* TQX stream copy endpoints and stream-slot array (live-only scene
 * internals, r188): copy src/dst pair, per-slot bytes, and the slot_va()
 * base both live modules share. The readback divisor below reuses the
 * command-segment VA; the arithmetic is preserved verbatim.
 */
#define MT_TQX_STREAM_SRC_VA 0x40100000ULL
#define MT_TQX_STREAM_DST_VA 0x40200000ULL
#define MT_TQX_STREAM_SLOT_BYTES 4096U

#endif /* MT_ADDR_PLAN_H */
