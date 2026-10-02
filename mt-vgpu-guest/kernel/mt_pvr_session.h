/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_PVR_SESSION_H
#define MT_PVR_SESSION_H

/* DMA address shape shared by the bridge's PMR bookkeeping (S4-3 handoff).
 *
 * The bridge serves the MUSA UMD with system-memory PMRs. Real execution
 * needs a DMA lifetime mapping and a GPU PA translated through the live
 * session's negotiated Guest system-memory windows.
 *
 * Deliberately NO cross-module function table: __symbol_get() does not
 * resolve on this kernel (verified empirically -- even printk resolves to
 * NULL from a test module), so the bridge uses PCI lookup, driver-name
 * validation, drvdata, try_module_get(), and core DMA mapping calls. If
 * symbol resolution ever starts working here, that would be a separate,
 * better mechanism -- not this one.
 *
 * Lifetime rules, non-negotiable:
 *  1. Acquisition re-validates everything, every time: device bound to
 *     mt_guest_probe, drvdata present, trial pinned AND connected, all
 *     under device_lock + trial_lock. Registration holds both locks while
 *     creating mappings, so trial teardown cannot race the check. Anything
 *     else degrades the caller to system memory, never an error to the UMD.
 *  2. try_module_get() pins against UNLOAD, not unbind. A later unbind
 *     while mappings live can tear down the bound session and its DMA
 *     environment while mappings remain.
 *     Operational rule, same class as the live-module serialization rule:
 *     rmmod the bridge (which unmaps everything at PMR free) BEFORE
 *     touching the session module. No code can enforce this: the PCI core
 *     gives remove() no veto.
 *  3. DMA API addresses and GPU page-table addresses are distinct domains.
 *     Keep dma_addr only for the matching DMA unmap; derive gpu_pa from the
 *     Guest physical page and negotiated runtime system windows. Never
 *     populate a GPU PTE from dma_addr by assumption.
 *  4. Direction is bidirectional until the bridge learns per-PMR direction.
 */

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
typedef uint64_t u64;
#endif

/* One PMR page: DMA lifetime handle plus GPU-PA page-table input. */
struct mt_pvr_dma_page {
	u64 dma_addr;	/* dma_map_page result; use only for sync/unmap */
	u64 gpu_pa;	/* Guest GPA translated through runtime system windows */
};

#endif /* MT_PVR_SESSION_H */
