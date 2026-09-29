/* SPDX-License-Identifier: GPL-2.0 */
#ifdef MTGPU_COMPAT_USERSPACE_TEST
#include <stdint.h>
#include <stddef.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
#else
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/build_bug.h>
#include "mtgpu/mtgpu_drv.h"
#endif

extern u64 mtgpu_guest_v2_gdpa_to_host_compat(const void *raw_info,
						      u64 bar2_base,
						      u64 bar2_window_size,
						      u64 gdpa);
extern u64 mtgpu_guest_v1_device_paddr_to_host_device_paddr(void *ps_dev_config,
							      u64 gdpa);
u64 GuestDevicePAddrToHostDevicePAddr(void *ps_dev_config, u64 gdpa);

#define MTGPU_INFO_MAGIC 0xaa557491U
#define MTGPU_INFO_VERSION_OFFSET 4U
#define MTGPU_INFO_CONFIG_LINK_OFFSET 0x78U
#define MTGPU_INFO_DEVICE_LINK_OFFSET 0x08U
#define MTGPU_INFO_PAGE_LINK_OFFSET 0x70U

static const volatile u8 *mtgpu_guest_info_from_device_config(void *config,
							       u64 *bar2_base)
{
	const volatile u8 *level;
	void *next;

#ifndef MTGPU_COMPAT_USERSPACE_TEST
	BUILD_BUG_ON(offsetof(struct mtgpu_platform_data, pcie_memory_base) != 8);
	BUILD_BUG_ON(offsetof(struct mtgpu_platform_data, vz_data.vgpu_info) != 0x70);
#endif

	if (!config)
		return NULL;
	level = (const volatile u8 *)config;
	next = *(void * volatile *)(level + MTGPU_INFO_CONFIG_LINK_OFFSET);
	if (!next)
		return NULL;
	level = next;
	next = *(void * volatile *)(level + MTGPU_INFO_DEVICE_LINK_OFFSET);
	if (!next)
		return NULL;
	level = next;
	/* +0x40/+0x48 are MMU size/card-base, not the BAR2 window. */
	*bar2_base = *(const volatile u64 *)(level + 0x08U);
	return *(const volatile u8 * volatile *)(level + MTGPU_INFO_PAGE_LINK_OFFSET);
}

static u32 mtgpu_guest_read_le32(const volatile u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
	       ((u32)p[3] << 24);
}

static u64 mtgpu_guest_read_le64(const volatile u8 *p)
{
	return (u64)mtgpu_guest_read_le32(p) |
	       ((u64)mtgpu_guest_read_le32(p + 4) << 32);
}

u64 GuestDevicePAddrToHostDevicePAddr(void *ps_dev_config, u64 gdpa)
{
	u64 bar2_base, bar2_window_size;
	const volatile u8 *info = mtgpu_guest_info_from_device_config(ps_dev_config,
								       &bar2_base);
	u32 version;

	if (!info || mtgpu_guest_read_le32(info) != MTGPU_INFO_MAGIC)
		return 0;
	version = mtgpu_guest_read_le32(info + MTGPU_INFO_VERSION_OFFSET);
	if (version == 1)
		return mtgpu_guest_v1_device_paddr_to_host_device_paddr(ps_dev_config,
									 gdpa);
	if (version == 2) {
		/* PVR passes a device-relative PA; the Windows oracle takes a GPA.
		 * The original v1 function also compares relative PAs to the MMU
		 * card base. Never interpret an absolute GPA as a relative offset. */
		bar2_window_size = mtgpu_guest_read_le64(info + 0x20);
		if (!bar2_base || gdpa >= bar2_window_size ||
		    bar2_base > ~(u64)0 - bar2_window_size)
			return 0;
		return mtgpu_guest_v2_gdpa_to_host_compat((const void *)info,
							   bar2_base,
							   bar2_window_size,
							   bar2_base + gdpa);
	}
	return 0;
}
