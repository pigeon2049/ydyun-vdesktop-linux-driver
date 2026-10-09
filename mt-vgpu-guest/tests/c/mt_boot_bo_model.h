/* Test-only OS boundary for GEM/TQX lifetime tests. The production boot BO
 * layer depends on PCI BAR allocators; these tests exercise the real GEM,
 * BO, VM and work-preparation code with modeled boot-pool entry points. */
#ifndef MT_TEST_BOOT_BO_MODEL_H
#define MT_TEST_BOOT_BO_MODEL_H
#define MT_GUEST_BO_VRAM_H
#define MT_GUEST_BOOT_BO_H
#include "../../kernel/mt_process_resources.h"
#include "../../kernel/mt_pool_slice.h"

struct mt_vram;
struct mutex;
struct mt_bo_store {
	struct mt_vram *vram;
	struct mutex *lock;
	const struct mt_bo_ops *ops;
	u64 allocated_bytes;
	u32 objects;
	u64 next_pa;
	unsigned allocated, freed;
	bool fail_alloc, fail_clear;
};
struct mt_reserved_pools { struct mt_pool_state slices[MT_GUEST_POOL_COUNT]; };
struct mt_boot_bo_store {
	struct mt_reserved_pools *pools;
	struct mt_bo_store *buffers;
	struct mt_bo *slots[MT_PROCESS_SHARED_COUNT + MT_GUEST_POOL_COUNT];
};

static inline int mt_boot_bo_bind(struct mt_boot_bo_store *s, struct mt_gpu_vm *vm,
		const struct mt_device_profile *profile)
{
	(void)s; (void)vm; (void)profile;
	return -EOPNOTSUPP;
}

static inline int mt_boot_pool_alloc(struct mt_boot_bo_store *s,
		struct mt_execution_context *context, u32 kind, u32 bytes,
		struct mt_pool_slice *out)
{
	(void)s; (void)context; (void)kind; (void)bytes; (void)out;
	return -EOPNOTSUPP;
}
#endif
