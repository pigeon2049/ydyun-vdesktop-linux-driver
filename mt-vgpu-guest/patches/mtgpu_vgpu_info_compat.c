/* SPDX-License-Identifier: GPL-2.0 */
/* Offline-linked helper called by the opt-in v1/v2 info-page patch. */
#ifdef MTGPU_COMPAT_USERSPACE_TEST
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
#else
#include <linux/types.h>
#include <linux/string.h>
#include <asm/barrier.h>
#endif

#define MTGPU_INFO_MAGIC 0xaa557491U
#define MTGPU_INFO_V1 1U
#define MTGPU_INFO_V2 2U
#define MTGPU_INFO_V1_FW_BASE 0x848U
#define MTGPU_INFO_V1_FW_SIZE 0x850U
#define MTGPU_INFO_V2_SEGMENT_START 0x28U
#define MTGPU_INFO_V2_SEGMENT_LIMIT 0xc48U
#define MTGPU_INFO_V2_SEGMENT_COUNT 0xc50U
#define MTGPU_INFO_V2_SEGMENT_STRIDE 24U
#define MTGPU_INFO_V2_FW_FLAG 0x4ULL
#define MTGPU_INFO_V2_BAR2_SIZE 0x20U
#define MTGPU_INFO_V2_FLAGS 0x10U
#define MTGPU_INFO_V2_PB_BASE 0xc90U
#define MTGPU_INFO_V2_PB_SIZE 0xc98U
#define MTGPU_INFO_V2_PB_FLAG 0x10ULL
#define MTGPU_INFO_V2_SHARED_FLAG 0x80ULL
#define MTGPU_INFO_V2_SHARED_SEGMENT_FLAG 0x20ULL
#define MTGPU_INFO_V2_SHARED_SEGMENT_BYTES 0x200000ULL
#define MTGPU_DEVICE_BAR2_BASE 0x78U
#define MTGPU_DEVICE_BAR2_SIZE 0x80U
#define MTGPU_DEVICE_VGPU_INFO 0x1138U
#define MTGPU_VGPU_SHARE_MEM_DEV_ADDR 0x10U
#define MTGPU_VGPU_SHARE_MEM_SIZE 0x20U
#define MTGPU_VGPU_SHARE_MEM_V1_BYTES 0x8000ULL
#define MTGPU_VGPU_SHARE_MEM_V2_BYTES 0x200000ULL
#define MTGPU_FW_MAIN_BYTES 0x800000ULL

static u32 mtgpu_info_read_le32(const volatile u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
	       ((u32)p[3] << 24);
}

static u64 mtgpu_info_read_le64(const volatile u8 *p)
{
	return (u64)mtgpu_info_read_le32(p) |
	       ((u64)mtgpu_info_read_le32(p + 4) << 32);
}

static void mtgpu_info_write_le64(volatile u8 *p, u64 value)
{
	u32 i;

	for (i = 0; i < 8; i++)
		p[i] = (u8)(value >> (i * 8));
}

#ifndef MTGPU_COMPAT_USERSPACE_TEST
/* Windows 140026b30 requests the typed v2 layout before publishing the GPA.
 * This call is enabled only at the audited, enlarged info-buffer allocation.
 * It must never be used for the smaller shared-memory allocation. */
extern phys_addr_t os_virt_to_phys(void *address);
phys_addr_t mtgpu_guest_info_request_v2(void *address);
phys_addr_t mtgpu_guest_info_request_v2(void *address)
{
	mtgpu_info_write_le64((u8 *)address + 0xc48, 3);
	wmb();
	return os_virt_to_phys(address);
}
#endif

u64 mtgpu_guest_fw_heap_base_compat(const void *raw_info);
u64 mtgpu_guest_v2_gdpa_to_host_compat(const void *raw_info, u64 bar2_base,
				       u64 bar2_window_size, u64 gdpa);
void *mtgpu_guest_vpu_share_mem_addr_compat(void *raw_share_mem,
						   void *raw_mtdev);
u64 mtgpu_guest_vpu_share_mem_base_compat(const void *raw_info,
						  const void *raw_share_mem,
						  const void *raw_mtdev);

/*
 * The 2.3 Linux Guest core now allocates one full page for this response.
 * Linux v1 stores FW_MAIN directly at +0x848; the measured Windows-Guest v2
 * response stores typed 24-byte segments beginning at +0x28. For v2, the
 * unique flags&4 segment is the firmware range. Return zero on any ambiguous
 * or malformed response so PhysHeapInit fails closed.
 */
u64 mtgpu_guest_fw_heap_base_compat(const void *raw_info)
{
	const volatile u8 *info = raw_info;
	u32 version;
	u64 base, size;

	if (!info || mtgpu_info_read_le32(info) != MTGPU_INFO_MAGIC)
		return 0;
	version = mtgpu_info_read_le32(info + 4);
	if (version == MTGPU_INFO_V1) {
		base = mtgpu_info_read_le64(info + MTGPU_INFO_V1_FW_BASE);
		size = mtgpu_info_read_le32(info + MTGPU_INFO_V1_FW_SIZE);
		if (!base || size < MTGPU_FW_MAIN_BYTES || base > ~(u64)0 - size)
			return 0;
		return base;
	}
	if (version == MTGPU_INFO_V2) {
		u32 count = mtgpu_info_read_le32(info + MTGPU_INFO_V2_SEGMENT_COUNT);
		u32 max_count = (MTGPU_INFO_V2_SEGMENT_LIMIT -
				 MTGPU_INFO_V2_SEGMENT_START) /
				MTGPU_INFO_V2_SEGMENT_STRIDE;
		u32 i, matches = 0;

		if (count > max_count)
			return 0;
		base = 0;
		size = 0;
		for (i = 0; i < count; i++) {
			u32 offset = MTGPU_INFO_V2_SEGMENT_START +
				     i * MTGPU_INFO_V2_SEGMENT_STRIDE;
			u64 segment_base = mtgpu_info_read_le64(info + offset);
			u64 segment_size = mtgpu_info_read_le64(info + offset + 8);
			u64 flags = mtgpu_info_read_le64(info + offset + 16);

			if (!(flags & MTGPU_INFO_V2_FW_FLAG))
				continue;
			matches++;
			base = segment_base;
			size = segment_size;
		}
		if (matches != 1 || !base || size < MTGPU_FW_MAIN_BYTES ||
		    base > ~(u64)0 - size)
			return 0;
		return base;
	}
	return 0;
}

/*
 * Windows FUN_140026b30 publishes its V2 flags&0x20 shared segment at
 * BAR2_base + segment.base (the segment is a BAR2 offset, not a GPU PAddr).
 * The Linux Guest core's fixed +0x448 load is the equivalent V1 field. Keep
 * that V1 value unchanged; resolve V2 only when the shared-memory capability,
 * unique segment, minimum size, and actual PCI BAR2 window all agree. The
 * Windows V2 path maps the complete 2 MiB shared segment; publish that size
 * only after its address has passed validation. Return the original structure
 * pointer because the linked core uses it after this call.
 */
void *mtgpu_guest_vpu_share_mem_addr_compat(void *raw_share_mem,
						   void *raw_mtdev)
{
	const volatile u8 *mtdev = raw_mtdev;
	volatile u8 *share_mem = raw_share_mem;
	const volatile u8 *info;
	u64 address = 0;
	u64 share_size = MTGPU_VGPU_SHARE_MEM_V1_BYTES;
	u32 version;

	if (!share_mem)
		return NULL;
	mtgpu_info_write_le64(share_mem + MTGPU_VGPU_SHARE_MEM_DEV_ADDR, 0);
	/* Keep the Linux v1 contract unless a complete v2 mapping is resolved. */
	mtgpu_info_write_le64(share_mem + MTGPU_VGPU_SHARE_MEM_SIZE,
			      MTGPU_VGPU_SHARE_MEM_V1_BYTES);
	if (!mtdev)
		return raw_share_mem;
	info = (const volatile u8 *)(unsigned long)
		mtgpu_info_read_le64(mtdev + MTGPU_DEVICE_VGPU_INFO);
	if (!info || mtgpu_info_read_le32(info) != MTGPU_INFO_MAGIC)
		return raw_share_mem;
	version = mtgpu_info_read_le32(info + 4);
	if (version == MTGPU_INFO_V1) {
		address = mtgpu_info_read_le64(info + 0x448U);
	} else if (version == MTGPU_INFO_V2) {
		u64 header_flags = mtgpu_info_read_le64(info + MTGPU_INFO_V2_FLAGS);
		u64 bar2_base, bar2_size, segment_base = 0, segment_size = 0;
		u32 count = mtgpu_info_read_le32(info + MTGPU_INFO_V2_SEGMENT_COUNT);
		u32 max_count = (MTGPU_INFO_V2_SEGMENT_LIMIT -
				 MTGPU_INFO_V2_SEGMENT_START) /
				MTGPU_INFO_V2_SEGMENT_STRIDE;
		u32 i, matches = 0;

		/* A malformed v2 page must not advertise a partial legacy mapping. */
		share_size = 0;
		mtgpu_info_write_le64(share_mem + MTGPU_VGPU_SHARE_MEM_SIZE, 0);

		if (!(header_flags & MTGPU_INFO_V2_SHARED_FLAG) ||
		    count > max_count)
			return raw_share_mem;
		for (i = 0; i < count; i++) {
			u32 offset = MTGPU_INFO_V2_SEGMENT_START +
				     i * MTGPU_INFO_V2_SEGMENT_STRIDE;
			u64 flags = mtgpu_info_read_le64(info + offset + 16);

			if (!(flags & MTGPU_INFO_V2_SHARED_SEGMENT_FLAG))
				continue;
			matches++;
			segment_base = mtgpu_info_read_le64(info + offset);
			segment_size = mtgpu_info_read_le64(info + offset + 8);
		}
		if (matches != 1 || segment_size < MTGPU_INFO_V2_SHARED_SEGMENT_BYTES ||
		    segment_base > ~(u64)0 - segment_size)
			return raw_share_mem;
		bar2_base = mtgpu_info_read_le64(mtdev + MTGPU_DEVICE_BAR2_BASE);
		bar2_size = mtgpu_info_read_le64(mtdev + MTGPU_DEVICE_BAR2_SIZE);
		if (!bar2_base || !bar2_size ||
		    bar2_base > ~(u64)0 - bar2_size || segment_base > bar2_size ||
		    segment_size > bar2_size - segment_base ||
		    bar2_base > ~(u64)0 - segment_base)
			return raw_share_mem;
		address = bar2_base + segment_base;
		share_size = MTGPU_VGPU_SHARE_MEM_V2_BYTES;
	} else {
		return raw_share_mem;
	}
	mtgpu_info_write_le64(share_mem + MTGPU_VGPU_SHARE_MEM_DEV_ADDR, address);
	mtgpu_info_write_le64(share_mem + MTGPU_VGPU_SHARE_MEM_SIZE,
			      share_size);
	return raw_share_mem;
}

/*
 * The Linux VPU command ring maps BAR2 directly. In the Windows v2 layout,
 * the ring lives at the BAR2 address published in vgpu_share_mem, which is
 * BAR2_base + the validated flags&0x20 segment offset. Reparse the page at
 * mapping time and require one in-range shared segment plus exact agreement
 * with the published address and 2 MiB map length. A v1 caller keeps its old
 * mapping.
 */
u64 mtgpu_guest_vpu_share_mem_base_compat(const void *raw_info,
						  const void *raw_share_mem,
						  const void *raw_mtdev)
{
	const volatile u8 *info = raw_info;
	const volatile u8 *share_mem = raw_share_mem;
	const volatile u8 *mtdev = raw_mtdev;
	u64 flags, address, share_size, bar2_base, bar2_size, offset;
	u64 segment_base = 0, segment_size = 0;
	u32 count, max_count, i, matches = 0;

	if (!info || !share_mem || !mtdev ||
	    mtgpu_info_read_le32(info) != MTGPU_INFO_MAGIC ||
	    mtgpu_info_read_le32(info + 4) != MTGPU_INFO_V2)
		return 0;
	flags = mtgpu_info_read_le64(info + MTGPU_INFO_V2_FLAGS);
	if (!(flags & MTGPU_INFO_V2_SHARED_FLAG))
		return 0;
	count = mtgpu_info_read_le32(info + MTGPU_INFO_V2_SEGMENT_COUNT);
	max_count = (MTGPU_INFO_V2_SEGMENT_LIMIT - MTGPU_INFO_V2_SEGMENT_START) /
		    MTGPU_INFO_V2_SEGMENT_STRIDE;
	if (count > max_count)
		return 0;
	for (i = 0; i < count; i++) {
		u32 record = MTGPU_INFO_V2_SEGMENT_START +
			     i * MTGPU_INFO_V2_SEGMENT_STRIDE;
		u64 segment_flags = mtgpu_info_read_le64(info + record + 16);

		if (!(segment_flags & MTGPU_INFO_V2_SHARED_SEGMENT_FLAG))
			continue;
		matches++;
		segment_base = mtgpu_info_read_le64(info + record);
		segment_size = mtgpu_info_read_le64(info + record + 8);
	}
	if (matches != 1 || segment_size < MTGPU_INFO_V2_SHARED_SEGMENT_BYTES ||
	    segment_base > ~(u64)0 - segment_size)
		return 0;
	address = mtgpu_info_read_le64(share_mem + MTGPU_VGPU_SHARE_MEM_DEV_ADDR);
	share_size = mtgpu_info_read_le64(share_mem + MTGPU_VGPU_SHARE_MEM_SIZE);
	bar2_base = mtgpu_info_read_le64(mtdev + MTGPU_DEVICE_BAR2_BASE);
	bar2_size = mtgpu_info_read_le64(mtdev + MTGPU_DEVICE_BAR2_SIZE);
	if (!address || share_size != MTGPU_VGPU_SHARE_MEM_V2_BYTES ||
	    !bar2_base || !bar2_size || bar2_base > ~(u64)0 - bar2_size ||
	    segment_base > bar2_size || segment_size > bar2_size - segment_base ||
	    bar2_base > ~(u64)0 - segment_base ||
	    address != bar2_base + segment_base)
		return 0;
	offset = address - bar2_base;
	if (offset >= bar2_size || share_size > bar2_size - offset)
		return 0;
	return address;
}

/*
 * The Windows v2 translator (mtkm64.sys FUN_140027ab4) treats GDPA as an
 * absolute device address in the BAR2-backed virtual aperture: subtract the
 * BAR2 GPU base, then map an optional PB free-list prefix followed by the
 * flags&7 segments packed in table order. The Linux v1 translator obtains
 * that base and size from its device-state object before handling other v1
 * ranges. Return zero when the state/page range disagrees or v2 cannot map
 * the request address into one published range.
 */
u64 mtgpu_guest_v2_gdpa_to_host_compat(const void *raw_info, u64 bar2_base,
				       u64 bar2_window_size, u64 gdpa)
{
	const volatile u8 *info = raw_info;
	u64 bar2_size, header_flags, offset, cursor = 0;
	u64 pb_base = 0;
	u32 pb_size = 0, count, max_count, i;

	if (!info || mtgpu_info_read_le32(info) != MTGPU_INFO_MAGIC ||
	    mtgpu_info_read_le32(info + 4) != MTGPU_INFO_V2)
		return 0;

	bar2_size = mtgpu_info_read_le64(info + MTGPU_INFO_V2_BAR2_SIZE);
	header_flags = mtgpu_info_read_le64(info + MTGPU_INFO_V2_FLAGS);
	count = mtgpu_info_read_le32(info + MTGPU_INFO_V2_SEGMENT_COUNT);
	max_count = (MTGPU_INFO_V2_SEGMENT_LIMIT - MTGPU_INFO_V2_SEGMENT_START) /
		    MTGPU_INFO_V2_SEGMENT_STRIDE;
	if (!bar2_size || bar2_window_size != bar2_size ||
	    bar2_base > ~(u64)0 - bar2_window_size || gdpa < bar2_base ||
	    gdpa - bar2_base >= bar2_size || count > max_count)
		return 0;
	offset = gdpa - bar2_base;

	if (header_flags & MTGPU_INFO_V2_PB_FLAG) {
		pb_base = mtgpu_info_read_le64(info + MTGPU_INFO_V2_PB_BASE);
		pb_size = mtgpu_info_read_le32(info + MTGPU_INFO_V2_PB_SIZE);
		if ((pb_size && !pb_base) || pb_size > bar2_size ||
		    pb_base > ~(u64)0 - pb_size)
			return 0;
	}

	if (offset < pb_size)
		return pb_base + offset;
	offset -= pb_size;

	/* Validate the whole published map before returning any partial match. */
	for (i = 0; i < count; i++) {
		u32 record = MTGPU_INFO_V2_SEGMENT_START +
			     i * MTGPU_INFO_V2_SEGMENT_STRIDE;
		u64 base = mtgpu_info_read_le64(info + record);
		u64 size = mtgpu_info_read_le64(info + record + 8);
		u64 flags = mtgpu_info_read_le64(info + record + 16);

		if (!(flags & 7) || !size)
			continue;
		if (!base || base > ~(u64)0 - size || cursor > bar2_size ||
		    size > bar2_size - cursor)
			return 0;
		cursor += size;
	}
	if (offset >= cursor)
		return 0;

	cursor = 0;
	for (i = 0; i < count; i++) {
		u32 record = MTGPU_INFO_V2_SEGMENT_START +
			     i * MTGPU_INFO_V2_SEGMENT_STRIDE;
		u64 base = mtgpu_info_read_le64(info + record);
		u64 size = mtgpu_info_read_le64(info + record + 8);
		u64 flags = mtgpu_info_read_le64(info + record + 16);

		if (!(flags & 7) || !size)
			continue;
		if (offset < cursor + size)
			return base + (offset - cursor);
		cursor += size;
	}
	return 0;
}
