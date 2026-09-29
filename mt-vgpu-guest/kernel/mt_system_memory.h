/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_SYSTEM_MEMORY_H
#define MT_GUEST_SYSTEM_MEMORY_H
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include "mt_system_address.h"

/* Stable kernel RAM, a flat CPU mapping and immutable translated page list.
 * Owner must outlive all borrowed BOs. Preparation publishes no GPA or root. */
struct mt_system_memory {
	void *cpu;
	u64 *page_pa;
	u32 bytes;
};

static inline void mt_system_memory_fini(struct mt_system_memory *m)
{
	kvfree(m->cpu);
	kvfree(m->page_pa);
	memset(m, 0, sizeof(*m));
}

static inline int mt_system_memory_prepare(struct mt_system_memory *m, u32 bytes,
		const struct mt_system_address *address)
{
	struct mt_system_memory next = {0};
	u32 i;
	int ret;
	if (!m || m->cpu || m->page_pa || m->bytes || !address || !bytes ||
	    (bytes & 4095) || bytes > 0x4000000 || PAGE_SIZE != 4096)
		return -EINVAL;
	/* Zeroing is the Linux allocation policy, not inferred Windows content. */
	next.cpu = kvzalloc(bytes, GFP_KERNEL);
	next.page_pa = kvcalloc(bytes / 4096, sizeof(u64), GFP_KERNEL);
	if (!next.cpu || !next.page_pa) {
		ret = -ENOMEM;
		goto fail;
	}
	next.bytes = bytes;
	for (i = 0; i < bytes / 4096; i++) {
		void *p = (u8 *)next.cpu + i * 4096;
		struct page *page = is_vmalloc_addr(p) ? vmalloc_to_page(p) : virt_to_page(p);
		if (!page) { ret = -EFAULT; goto fail; }
		ret = mt_system_page_address(address, page_to_phys(page), &next.page_pa[i]);
		if (ret)
			goto fail;
	}
	*m = next;
	return 0;
fail:
	mt_system_memory_fini(&next);
	return ret;
}
#endif
