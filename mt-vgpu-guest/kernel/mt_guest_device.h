/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_DEVICE_H
#define MT_GUEST_DEVICE_H
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/vmalloc.h>
#include "mt_guest_state.h"
#include "mt_guest_announcements.h"
#include "mt_rpc_publish.h"
#include "mt_rpc_service.h"
#include "mt_runtime_context.h"
#include "mt_bo_vram.h"
#include "mt_vm_vram.h"
#include "mt_gem.h"

/* Guest-visible PCI window + firmware segment anchor (r269): BAR2 is the
 * 16G prefetchable window (lspci: 0x800000000-0xbffffffff); info segment 5
 * lives at 0x43000000 (decode-device-info.py, version-2 info). Named once;
 * the recovery checks below compare by name. Neither is the 1GiB
 * vm_memory_size_bytes quota itself, which the kernel never consumes.
 */
#define MT_GUEST_BAR2_BASE 0x800000000ULL
#define MT_GUEST_BAR2_BYTES 0x400000000ULL
#define MT_GUEST_SEG5_ADDR 0x43000000ULL

/* S3000 vGPU identity guard (r271): vendor/device/subsystem quads were
 * pasted in eleven recovery files. Compare by name; the drm_snapshot
 * loose match (vendor/device only) is a different semantic and stays.
 */
static inline bool mt_guest_match_s3000(u16 vendor, u16 device,
					u16 subvendor, u16 subdevice)
{
	return vendor == 0x1ed5 && device == 0x0222 &&
	       subvendor == 0x1ed5 && subdevice == 0x1101;
}
#include "mt_marker_fence.h"

struct mt_guest_device {
	struct mt_guest state;
	struct mt_rpc_service service;
	bool retained_kick_attempted;
	bool trial_bus_master;
	struct mt_runtime_context runtime;
	struct mt_bo_store buffers;
	struct mt_vm_store address_spaces;
	struct mt_gem_store gem;
	struct mt_marker_store markers;
	struct mt_execution_store execution;
	struct mt_boot_bo_store shared_boot;
	/* System RAM owner, separate from the ABI-stable boot structure. */
	struct mt_system_memory paging_command;
	struct mt_reserved_pools reserved_pools;
};
#endif
