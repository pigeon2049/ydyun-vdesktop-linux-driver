/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef MT_COPY_UAPI_H
#define MT_COPY_UAPI_H
#include <linux/ioctl.h>
#include <linux/types.h>

/* Experimental staging ABI, not a DRM or original MUSA ABI. No GPU addresses,
 * user pointers, raw command streams or page-table operations are accepted.
 * Destination is input/output, source is input. Synchronous completion means
 * the real GPU fence has succeeded and all 4096 destination bytes were read.
 * Errors after submission (including copyout EFAULT) may have executed work;
 * QUERY counters describe that case. Do not blindly retry non-idempotent work.
 */
#define MT_COPY_ABI 1U
#define MT_COPY_PAGE_BYTES 4096U
#define MT_COPY_CAP_SYNC 1U
#define MT_COPY_CAP_SERIAL 2U
struct mt_copy_query {
	__u32 abi, max_bytes, capabilities, faulted;
	__aligned_u64 submitted, completed, last_sequence;
	__u32 reserved[2];
};
struct mt_copy_request {
	__u32 abi, flags, source_offset, destination_offset, bytes;
	__u32 reserved[3];
	__aligned_u64 sequence; /* input must be zero */
	__u8 source[MT_COPY_PAGE_BYTES];
	__u8 destination[MT_COPY_PAGE_BYTES];
};
#define MT_COPY_QUERY _IOR('M', 0, struct mt_copy_query)
#define MT_COPY_EXEC _IOWR('M', 1, struct mt_copy_request)
#endif
