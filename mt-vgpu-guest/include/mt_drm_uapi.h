/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef MT_DRM_UAPI_H
#define MT_DRM_UAPI_H
#ifdef __KERNEL__
#include <drm/drm.h>
#else
#include <libdrm/drm.h>
#endif
#define MT_DRM_ABI 1U
#define MT_DRM_SLOT_COUNT 8U
/* Original front-end default. Applications discover the actual maximum with
 * QUERY.slot_bytes; successful allocations still depend on available slots. */
#define MT_DRM_SLOT_BYTES 65536U
#define MT_DRM_IO_BYTES 4096U
#define MT_DRM_CAP_COPY 1U
#define MT_DRM_CAP_FILL 2U
#define MT_DRM_CAP_3D   4U
struct drm_mt_query {
	__u32 abi, slot_count, slot_bytes, leased;
	__u32 faulted, retained, capabilities, reserved;
	__u64 submitted, completed, last_sequence;
	/* Mappings currently installed in each GPU address space. Reported so a
	 * client can see address-space headroom without guessing. */
	__u64 vm2d_mappings, vm3d_mappings;
	/* Derived mapping ceiling of the 3D space, from the page-table budget. */
	__u64 vm3d_max_mappings;
};
struct drm_mt_create { __u32 bytes, flags, handle, reserved; };
/* Explicit CPU transfer, at most one page per call. No mmap/PRIME yet. */
struct drm_mt_rw {
	__u32 handle, flags;
	__u64 offset;
	__u32 bytes, reserved;
	__u8 data[MT_DRM_IO_BYTES];
};
/* Synchronous ioctl, using direct VRAM source/destination GEM objects.
 * out_syncobj is an existing binary DRM syncobj handle, replaced with the
 * actual GPU fence at publication. Distinct source/destination are required.
 * seq input is zero. An error after publication may still have executed work.
 */
struct drm_mt_copy {
	__u32 source, destination;
	__u64 source_offset, destination_offset;
	__u32 bytes, flags, out_syncobj, reserved;
	__u64 sequence;
};
/* Tightly packed little-endian 32-bit pixels, clipped rectangle fully inside
 * width x height. offset is 4-byte aligned; the whole surface fits the GEM.
 * color is the raw pixel value, sequence input is zero. Native GPU clear.
 * out_syncobj is optional (0 = unused, same convention as SUBMIT_3D):
 * the fence sequence is still returned in-struct. r130 adjudication:
 * the driver never required nonzero syncobj (`if (r->out_syncobj)` since
 * r40); the old EINVAL expectation was a day-one test aspiration. */
struct drm_mt_fill {
	__u32 destination, out_syncobj;
	__u64 offset;
	__u32 width, height, x, y, rect_width, rect_height, color, flags;
	__u64 sequence;
};
/* 3D workload execution on Data Master 2 (Universal Queue) */
struct drm_mt_submit_3d {
	__u32 out_syncobj;   /* Optional syncobj handle (0 if unused) */
	__u32 flags;         /* Submit flags */
	__u64 frame_tag;     /* Frame sequence identifier */
	__u64 sequence;      /* Hardware fence sequence output */
	__u32 latency_us;    /* Measured hardware execution latency (us) */
	__u32 target_handle; /* Optional target GEM object handle (0 if none) */
};
#define DRM_MT_QUERY 0x00
#define DRM_MT_CREATE 0x01
#define DRM_MT_READ 0x02
#define DRM_MT_WRITE 0x03
#define DRM_MT_COPY 0x04
#define DRM_MT_FILL 0x05
#define DRM_MT_SUBMIT_3D 0x06
#define DRM_IOCTL_MT_QUERY DRM_IOR(DRM_COMMAND_BASE + DRM_MT_QUERY, struct drm_mt_query)
#define DRM_IOCTL_MT_CREATE DRM_IOWR(DRM_COMMAND_BASE + DRM_MT_CREATE, struct drm_mt_create)
#define DRM_IOCTL_MT_READ DRM_IOWR(DRM_COMMAND_BASE + DRM_MT_READ, struct drm_mt_rw)
#define DRM_IOCTL_MT_WRITE DRM_IOW(DRM_COMMAND_BASE + DRM_MT_WRITE, struct drm_mt_rw)
#define DRM_IOCTL_MT_COPY DRM_IOWR(DRM_COMMAND_BASE + DRM_MT_COPY, struct drm_mt_copy)
#define DRM_IOCTL_MT_FILL DRM_IOWR(DRM_COMMAND_BASE + DRM_MT_FILL, struct drm_mt_fill)
#define DRM_IOCTL_MT_SUBMIT_3D DRM_IOWR(DRM_COMMAND_BASE + DRM_MT_SUBMIT_3D, struct drm_mt_submit_3d)
#endif
