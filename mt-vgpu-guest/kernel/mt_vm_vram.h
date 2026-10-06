/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_VM_VRAM_H
#define MT_GUEST_VM_VRAM_H
#include <linux/vmalloc.h>
#include "mt_bo_vram.h"
#include "mt_gpu_vm.h"
#include "mt_process_resources.h"
#include "mt_boot_bo.h"

struct mt_vm_store;
struct mt_vm_vram {
	struct mt_gpu_vm vm;
	struct mt_bo tables;
	struct mt_vm_store *store;
};
struct mt_vm_store_ops {
	int (*create)(struct mt_vm_store *, u32 table_pages, struct mt_vm_vram **);
	int (*bind)(struct mt_vm_vram *, struct mt_bo *, u64 va, u32 offset, u32 bytes, u32 flags);
	int (*bind_shared)(struct mt_vm_vram *, const struct mt_device_profile *, struct mt_bo *const *);
	int (*bind_boot_shared)(struct mt_vm_vram *, const struct mt_device_profile *);
	int (*unbind)(struct mt_vm_vram *, u64 va, u32 bytes);
	int (*upload)(struct mt_vm_vram *);
	int (*seal)(struct mt_vm_vram *);
	int (*destroy)(struct mt_vm_vram *);
};
struct mt_vm_store {
	struct mt_bo_store *buffers;
	struct mt_boot_bo_store *boot;
	const struct mt_vm_store_ops *ops;
	u32 objects;
};

static int mt_vm_vram_create(struct mt_vm_store *store, u32 pages, struct mt_vm_vram **out)
{
	struct mt_bo_store *s = store->buffers;
	struct mt_vm_vram *v;
	void *image, *scratch;
	u32 capacity;
	int ret;
	lockdep_assert_held(s->lock);
	if (!out || !pages || pages > MT_BOOT_MAX_TABLE_PAGES)
		return -EINVAL;
	capacity = pages * PAGE_SIZE;
	v = kzalloc(sizeof(*v), GFP_KERNEL);
	image = kvzalloc(capacity, GFP_KERNEL);
	scratch = kvzalloc(capacity, GFP_KERNEL);
	if (!v || !image || !scratch) {
		ret = -ENOMEM;
		goto free_cpu;
	}
	ret = mt_bo_create(&v->tables, s->ops, s, capacity, PAGE_SIZE);
	if (ret)
		goto free_cpu;
	ret = mt_gpu_vm_init(&v->vm, &v->tables, image, scratch, capacity);
	/* The VM now holds its own reference, or init failed and retained none. */
	mt_bo_put(&v->tables);
	if (ret)
		goto free_cpu;
	v->store = store;
	store->objects++;
	*out = v;
	return 0;
free_cpu:
	kvfree(image);
	kvfree(scratch);
	kfree(v);
	return ret;
}

static int mt_vm_vram_bind(struct mt_vm_vram *v, struct mt_bo *bo,
		u64 va, u32 offset, u32 bytes, u32 flags)
{
	lockdep_assert_held(v->store->buffers->lock);
	return mt_gpu_vm_bind(&v->vm, bo, va, offset, bytes, flags);
}

static int mt_vm_vram_unbind(struct mt_vm_vram *v, u64 va, u32 bytes)
{
	lockdep_assert_held(v->store->buffers->lock);
	return mt_gpu_vm_unbind(&v->vm, va, bytes);
}

static int mt_vm_vram_bind_shared(struct mt_vm_vram *v,
		const struct mt_device_profile *profile, struct mt_bo *const *bo)
{
	lockdep_assert_held(v->store->buffers->lock);
	return mt_process_resources_bind(&v->vm, profile, bo);
}

static int mt_vm_vram_bind_boot_shared(struct mt_vm_vram *v,
		const struct mt_device_profile *profile)
{
	lockdep_assert_held(v->store->buffers->lock);
	return mt_boot_bo_bind(v->store->boot, &v->vm, profile);
}

static int mt_vm_vram_upload(struct mt_vm_vram *v)
{
	struct mt_gpu_vm *vm = &v->vm;
	int ret;
	lockdep_assert_held(v->store->buffers->lock);
	if (vm->sealed || vm->active_uses || v->tables.cpu_users || v->tables.gpu_users)
		return -EBUSY;
	vm->uploaded = false;
	ret = mt_bo_vram_write(&v->tables, 0, vm->image, vm->capacity);
	if (!ret)
		ret = mt_bo_vram_read(&v->tables, 0, vm->scratch, vm->capacity);
	if (!ret && memcmp(vm->scratch, vm->image, vm->capacity))
		ret = -EIO;
	if (!ret)
		vm->uploaded = true;
	/* Deliberately no root/doorbell publication from this memory operation. */
	return ret;
}

static int mt_vm_vram_seal(struct mt_vm_vram *v)
{
	lockdep_assert_held(v->store->buffers->lock);
	return mt_gpu_vm_seal(&v->vm);
}

static int mt_vm_vram_destroy(struct mt_vm_vram *v)
{
	void *image = v->vm.image, *scratch = v->vm.scratch;
	int ret;
	lockdep_assert_held(v->store->buffers->lock);
	/* The seal protected this space while live. With no active uses or
	 * owners nothing can observe the reopening, so idle teardown unseals
	 * first instead of stranding every owned reference (r137/r138). */
	if (v->vm.sealed) {
		ret = mt_gpu_vm_unseal(&v->vm);
		if (ret)
			return ret;
	}
	ret = mt_gpu_vm_fini(&v->vm);
	if (ret)
		return ret;
	v->store->objects--;
	kvfree(image);
	kvfree(scratch);
	kfree(v);
	return 0;
}

static const struct mt_vm_store_ops mt_vm_vram_ops = {
	.create = mt_vm_vram_create, .bind = mt_vm_vram_bind, .unbind = mt_vm_vram_unbind,
	.bind_shared = mt_vm_vram_bind_shared,
	.bind_boot_shared = mt_vm_vram_bind_boot_shared,
	.upload = mt_vm_vram_upload, .seal = mt_vm_vram_seal, .destroy = mt_vm_vram_destroy,
};

static inline void mt_vm_store_init(struct mt_vm_store *s, struct mt_bo_store *buffers)
{
	*s = (struct mt_vm_store){.buffers = buffers, .ops = &mt_vm_vram_ops};
}
#endif
