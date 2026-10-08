/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_PROCESS_RESOURCES_H
#define MT_GUEST_PROCESS_RESOURCES_H
#include "mt_gpu_vm.h"
#include "mt_guest_heaps.h"
#include "mt_device_profile.h"

#define MT_PAGING_COMMAND_BYTES 0x400000U

enum mt_process_shared_slot {
	MT_SHARED_PB, MT_SHARED_PDS, MT_SHARED_YUV, MT_SHARED_KILL,
	MT_SHARED_FENCE, MT_SHARED_PAGING_COMMAND, MT_PROCESS_SHARED_COUNT
};

/* Guest resources from 014d58 -> 00a630. PB and fence are optional; other
 * fixed slots must exist. A non-NULL pool vector adds all three independently
 * owned pool backings. Callers initialize/upload backing separately; this
 * only installs the complete batch atomically and holds BO references. */
static inline int mt_process_resources_bind_pools(struct mt_gpu_vm *vm,
		const struct mt_device_profile *profile,
		struct mt_bo *const bo[MT_PROCESS_SHARED_COUNT],
		struct mt_bo *const pools[MT_GUEST_POOL_COUNT])
{
	static const u32 indices[MT_PROCESS_SHARED_COUNT] = {0, 4, 10, 11, 6, 3};
	struct mt_guest_heap_plan plan;
	struct mt_guest_pool_spec specs[MT_GUEST_POOL_COUNT];
	struct mt_vm_binding bindings[MT_PROCESS_SHARED_COUNT + MT_GUEST_POOL_COUNT];
	u32 i, count = 0;
	if (!vm || !bo) {
		mt_gpu_vm_log("mt_gpu_vm: bind_pools fail args\n");
		return -EINVAL;
	}
	if (!profile || profile->family != 2 || profile->transfer_version != 1)
		return -EOPNOTSUPP;
	mt_guest_plan_heaps(&plan);
	for (i = 0; i < MT_PROCESS_SHARED_COUNT; i++) {
		const struct mt_guest_resource *r = &plan.resources[indices[i]];
		/* Original 014d58 enumerates pool entries after optional PB. */
		if (i == MT_SHARED_PDS && pools) {
			u32 j;
			mt_guest_plan_pools(specs);
			for (j = 0; j < MT_GUEST_POOL_COUNT; j++) {
				if (!pools[j])
					return -ENOENT;
				bindings[count++] = (struct mt_vm_binding){pools[j], specs[j].va,
					0, specs[j].bytes, 0};
			}
		}
		if (!bo[i] && (i == MT_SHARED_PB || i == MT_SHARED_FENCE))
			continue;
		if (!bo[i])
			return -ENOENT;
		bindings[count++] = (struct mt_vm_binding){bo[i], r->va, 0, r->size, 0};
	}
	return mt_gpu_vm_bind_many(vm, bindings, count);
}

/* Fixed-only interface retained for callers that bind dynamic pools separately. */
static inline int mt_process_resources_bind(struct mt_gpu_vm *vm,
		const struct mt_device_profile *profile, struct mt_bo *const bo[MT_PROCESS_SHARED_COUNT])
{
	return mt_process_resources_bind_pools(vm, profile, bo, NULL);
}
#endif
