/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_PVR_SESSION_H
#define MT_PVR_SESSION_H

/* Cross-module contract: PVR bridge (mt_pvr_bridge) to GPU session
 * (mt_guest_probe). S4-3 handoff, step 1.
 *
 * The bridge serves the MUSA UMD with system-memory PMRs. Real execution
 * needs those pages DMA-mapped through the live session's device, plus page
 * tables built over the resulting addresses. This header is the single place
 * both sides agree on shapes and versions.
 *
 * Lifetime rules, non-negotiable:
 *  1. The bridge NEVER hard-depends on the session. Acquisition is
 *     symbol_get() at use time; absence (module not loaded, session torn
 *     down) degrades to system-memory semantics, never an error to the UMD.
 *  2. The version is checked on every acquisition. A mismatch degrades,
 *     never adapts: silent struct drift across modules corrupts memory.
 *  3. symbol_put() balances every successful symbol_get(), on every path
 *     including failures after acquisition.
 *  4. The ops pointer is valid only between get and put. No caching it in
 *     file or PMR structures: the session module can unload at any time.
 *  5. DMA addresses are valid only while the mapping is held. Unmap before
 *     put; use-after-unmap reads garbage or faults the GPU.
 */

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
typedef uint32_t u32;
typedef uint64_t u64;
#endif

/* Bump on ANY layout or semantic change. Both sides static_assert it. */
#define MT_PVR_SESSION_ABI_VERSION 1U

/* DMA direction, from the PMR's point of view. */
enum mt_pvr_dma_dir {
	MT_PVR_DMA_NONE = 0,
	MT_PVR_DMA_TO_DEVICE = 1,	/* UMD writes, GPU reads */
	MT_PVR_DMA_FROM_DEVICE = 2,	/* GPU writes, UMD reads */
	MT_PVR_DMA_BIDIRECTIONAL = 3,
};

/* One page of a mapped PMR. */
struct mt_pvr_dma_page {
	u64 dma_addr;	/* device-visible address, valid while mapped */
	u64 cpu_addr;	/* contributing CPU page, for unmap lookup */
};

/* Session-side service table. Implemented by mt_guest_probe in a later step;
 * consumed by mt_pvr_bridge through symbol_get().
 */
struct mt_pvr_session_ops {
	u32 abi_version;
	/* Map PMR pages for device access. cpu_pages entries are struct page *
	 * (never virtual addresses: virt_to_page() is invalid on vmalloc
	 * addresses, and the bridge backs PMRs with vzalloc). Fills dma_addrs
	 * (caller array of npages) and returns 0, or a negative errno. -ENODEV
	 * means "no live session right now": degrade, do not propagate as a
	 * UMD error.
	 */
	int (*dma_map)(void *session, void **cpu_pages, u32 npages,
		       enum mt_pvr_dma_dir dir, struct mt_pvr_dma_page *dma_addrs);
	/* Release a previous mapping. Addresses must not be used after. */
	void (*dma_unmap)(void *session, struct mt_pvr_dma_page *dma_addrs,
			  u32 npages, enum mt_pvr_dma_dir dir);
	/* Opaque session handle for the two calls above. Acquired and released
	 * together with the ops pointer; never stored.
	 */
	void *(*session_get)(void);
	void (*session_put)(void *session);
};

#endif /* MT_PVR_SESSION_H */
