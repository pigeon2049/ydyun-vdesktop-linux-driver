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
