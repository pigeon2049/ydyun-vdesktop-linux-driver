/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_RUNTIME_CONTEXT_H
#define MT_RUNTIME_CONTEXT_H

#ifndef __KERNEL__
#include <stdbool.h>
#endif
#include "mt_fw_context.h"
#include "mt_guest_windows.h"

/* Caller supplies zeroed, persistent CPU pages and holds the session lock.
 * GPA values are CPU physical addresses, not dma_map_* IOVAs. Publication
 * retains all three buffers (descriptor, windows, info) and firmware backing.
 */
struct mt_runtime_context {
	void *descriptor, *windows;
	u64 descriptor_gpa;
	bool prepared, published, notified;
	int result, event_result;
};

static inline int mt_runtime_context_build(struct mt_runtime_context *r,
		void *descriptor, void *windows, u64 descriptor_gpa,
		const struct mt_fw_context_addresses *addresses,
		const void *info, u32 info_bytes,
		const struct mt_guest_window_inputs *inputs,
		u64 bar2_gpa, u64 bar2_bytes)
{
	u8 context[MT_FW_CONTEXT_BYTES], aperture[MT_GUEST_WINDOWS_BYTES];
	int ret;
	if (!r || r->prepared || r->published || !descriptor || !windows ||
	    !descriptor_gpa || (descriptor_gpa & 4095))
		return -EINVAL;
	ret = mt_guest_windows_build(aperture, sizeof(aperture), info, info_bytes,
		inputs, bar2_gpa, bar2_bytes);
	if (ret)
		return ret;
	ret = mt_fw_context_build(context, sizeof(context), addresses);
	if (ret)
		return ret;
	memcpy(descriptor, context, sizeof(context));
	memcpy(windows, aperture, sizeof(aperture));
	r->descriptor = descriptor;
	r->windows = windows;
	r->descriptor_gpa = descriptor_gpa;
	r->prepared = true;
	r->result = 0;
	return 0;
}

/* No speculative or repeated publication. write must order CPU stores before
 * each MMIO write and must not fail after the first address is exposed. A
 * completed callback is only a submitted notification, not a Host/GPU ack.
 */
static inline int mt_runtime_context_publish(struct mt_runtime_context *r,
		u32 guest, u32 firmware, u32 started, u32 healthy, bool pinned,
		u64 info_flags, void (*write)(void *, u32, u64), void *opaque)
{
	if (!r || !r->prepared || !write)
		return -EINVAL;
	if (r->published)
		return -EALREADY;
	if (!pinned || guest != 2 || firmware != 2 || !started || !healthy)
		return -EAGAIN;
	/* The session is already self-pinned. Mark retention before exposing GPA. */
	r->published = true;
	write(opaque, 0xf0, r->descriptor_gpa);
	if (info_flags & 1) {
		write(opaque, 0x70, 1);
		r->notified = true;
	}
	return 0;
}

/* Firmware disconnect does not prove the context GPA has been withdrawn.
 * Until its separate lifetime is established, no published buffer is freed.
 */
static inline int mt_runtime_context_can_release(const struct mt_runtime_context *r)
{
	return r && r->published ? -EBUSY : 0;
}
#endif
