/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_GEM_H
#define MT_GUEST_GEM_H
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include "mt_vm_vram.h"
#include "mt_work_command.h"
#include "mt_ce_paging.h"
#include "mt_device_profile.h"
#include "mt_tqx_copy.h"
#include "mt_tqx_program.h"
#include "mt_tqx_heap.h"
#include "mt_tqx_upload.h"
#include "mt_tqx_dma.h"
#include "mt_tqx_topology.h"
#include "mt_tqx_submission.h"
#include "mt_tqx_work.h"

/* Internal GEM bridge; no public ioctl ABI or DRM node is registered here.
 * All entry points take the session lock themselves. In particular never
 * drop the last GEM reference while holding that lock: ->free takes it.
 * A caller supplies a live DRIVER_GEM device and keeps the store alive.
 */
struct mt_gem_store;
struct mt_gem_object {
	struct drm_gem_object base;
	struct mt_gem_store *store;
	struct mt_bo *bo;
};
struct mt_gem_ops {
	int (*prepare_tqx_work)(struct mt_gem_store *, struct drm_device *, struct drm_file *,
			const u32 *, struct mt_vm_vram *, struct mt_execution_context *,
			const struct mt_tqx_submission_input *, struct mt_tqx_work *);
	int (*prepare_tqx_work_from_pools)(struct mt_gem_store *, struct drm_device *, struct drm_file *,
			const u32 *, struct mt_vm_vram *, struct mt_execution_context *,
			const struct mt_tqx_submission_input *, struct mt_tqx_work *);
	int (*cancel_tqx_work)(struct mt_gem_store *, struct mt_tqx_work *);
	int (*release_tqx_context_pools)(struct mt_gem_store *, struct mt_execution_context *);
	int (*destroy_execution_context)(struct mt_gem_store *, struct mt_execution_context *);
	int (*upload_tqx_submission)(struct mt_gem_store *, struct drm_device *, struct drm_file *,
			const u32 *, struct mt_vm_vram *, const struct mt_tqx_submission_input *, void *, u32);
	int (*encode_tqx_dma)(struct mt_gem_store *, const struct mt_tqx_upload_result *,
			const struct mt_tqx_dma_input *, void *, u32);
	int (*upload_tqx_stream)(struct mt_gem_store *, struct drm_device *, struct drm_file *,
			const u32 *, struct mt_vm_vram *, const struct mt_tqx_heap_input *, void *, u32);
	int (*prepare_tqx_stream)(struct mt_gem_store *, struct drm_device *, struct drm_file *,
			const u32 *, struct mt_vm_vram *, const struct mt_tqx_heap_input *, void *, u32);
	int (*prepare_tqx_programs)(struct mt_gem_store *, void *, u32);
	int (*prepare_tqx_copy)(struct mt_gem_store *, struct drm_device *, struct drm_file *,
			    u32, u32, struct mt_vm_vram *, const struct mt_tqx_copy_input *, void *, u32);
	int (*prepare_copy_paging)(struct mt_gem_store *, struct drm_device *, struct drm_file *,
			    u32, u32, u32, struct mt_vm_vram *, const struct mt_ce3_paging_input *, void *, u32);
	int (*prepare_copy_stream)(struct mt_gem_store *, struct drm_device *, struct drm_file *,
			    u32, u32, u32, struct mt_vm_vram *, const struct mt_ce3_stream_input *, void *, u32);
	int (*prepare_copy)(struct mt_gem_store *, struct drm_device *, struct drm_file *,
			    u32, u32, struct mt_vm_vram *, const struct mt_ce_copy_input *, void *, u32);
	int (*create)(struct mt_gem_store *, struct drm_device *, u64, struct drm_gem_object **);
	int (*create_handle)(struct mt_gem_store *, struct drm_device *, struct drm_file *, u64, u32 *);
	int (*bind_handle)(struct mt_gem_store *, struct drm_device *, struct drm_file *,
			   u32, struct mt_vm_vram *, u64, u32, u32, u32);
	int (*prepare_work)(struct mt_gem_store *, struct drm_device *, struct drm_file *,
			    u32, struct mt_vm_vram *, const struct mt_work_command_inputs *, void *, u32);
};
struct mt_gem_store {
	struct mt_bo_store *buffers;
	struct mt_device_profile profile;
	struct device *parent;
	const struct mt_gem_ops *ops;
	u32 objects;
	u32 tqx_cores;
};

/* Install topology only before objects exist. Re-query of the same count is
 * harmless; live changes need context/state reallocation, not silent reuse.
 * The lock also serializes this field with CPU descriptor encoding. */
/* Caller holds buffers->lock, including the probe-time information query. */
static inline int mt_gem_configure_tqx_info_locked(struct mt_gem_store *s,
		const void *info, u32 bytes)
{
	u32 cores;
	int ret;
	if (!s || !s->buffers)
		return -EINVAL;
	ret = mt_tqx_topology_from_info(&cores, &s->profile, info, bytes);
	if (ret)
		return ret;
	if (s->tqx_cores == cores)
		ret = 0;
	else if (s->objects || s->tqx_cores)
		ret = -EBUSY;
	else
		s->tqx_cores = cores;
	return ret;
}

static inline int mt_gem_configure_tqx_info(struct mt_gem_store *s,
		const void *info, u32 bytes)
{
	int ret;
	if (!s || !s->buffers)
		return -EINVAL;
	mutex_lock(s->buffers->lock);
	ret = mt_gem_configure_tqx_info_locked(s, info, bytes);
	mutex_unlock(s->buffers->lock);
	return ret;
}

static void mt_gem_bo_release(struct mt_bo *bo)
{
	kfree(bo);
}

static void mt_gem_free(struct drm_gem_object *obj)
{
	struct mt_gem_object *g = container_of(obj, struct mt_gem_object, base);
	struct mt_gem_store *s = g->store;
	struct drm_device *drm = obj->dev;
	mutex_lock(s->buffers->lock);
	/* BO metadata is separately allocated: VM/GPU references can survive
	 * destruction of this GEM wrapper and the originating file. */
	WARN_ON(mt_bo_put(g->bo));
	s->objects--;
	mutex_unlock(s->buffers->lock);
	drm_gem_object_release(obj);
	kfree(g);
	drm_dev_put(drm);
}

static struct dma_buf *mt_gem_export(struct drm_gem_object *obj, int flags)
{
	(void)obj;
	(void)flags;
	/* BAR private memory cannot use the system-page PRIME sg-table path. */
	return ERR_PTR(-EOPNOTSUPP);
}

static int mt_gem_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	(void)obj;
	(void)vma;
	/* No user mapping until revocation and CPU/GPU fence ordering exist. */
	return -EOPNOTSUPP;
}

static const struct drm_gem_object_funcs mt_gem_funcs = {
	.free = mt_gem_free, .export = mt_gem_export, .mmap = mt_gem_mmap,
};

static int mt_gem_create(struct mt_gem_store *s, struct drm_device *drm,
		u64 bytes, struct drm_gem_object **out)
{
	struct mt_gem_object *g;
	struct mt_bo *bo;
	int ret;
	if (!s || !drm || !out || drm->dev != s->parent ||
	    !drm_core_check_feature(drm, DRIVER_GEM))
		return -EINVAL;
	if (!bytes || bytes > 0xfffff000ULL)
		return -EINVAL;
	g = kzalloc(sizeof(*g), GFP_KERNEL);
	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (!g || !bo) {
		kfree(g);
		kfree(bo);
		return -ENOMEM;
	}
	mutex_lock(s->buffers->lock);
	ret = mt_bo_create(bo, s->buffers->ops, s->buffers, bytes, PAGE_SIZE);
	if (!ret) {
		bo->release = mt_gem_bo_release;
		s->objects++;
	}
	mutex_unlock(s->buffers->lock);
	if (ret) {
		kfree(bo);
		kfree(g);
		return ret;
	}
	g->store = s;
	g->bo = bo;
	/* private_object_init does not retain dev; keep it for release paths. */
	drm_dev_get(drm);
	drm_gem_private_object_init(drm, &g->base, bo->backing.bytes);
	g->base.funcs = &mt_gem_funcs;
	*out = &g->base;
	return 0;
}

static int mt_gem_create_handle(struct mt_gem_store *s, struct drm_device *drm,
		struct drm_file *file, u64 bytes, u32 *out)
{
	struct drm_gem_object *obj;
	u32 handle;
	int ret;
	if (!file || !file->minor || file->minor->dev != drm || !out)
		return -EINVAL;
	ret = mt_gem_create(s, drm, bytes, &obj);
	if (ret)
		return ret;
	ret = drm_gem_handle_create(file, obj, &handle);
	drm_gem_object_put(obj);
	if (!ret)
		*out = handle;
	return ret;
}

static int mt_gem_bind_handle(struct mt_gem_store *s, struct drm_device *drm,
		struct drm_file *file, u32 handle, struct mt_vm_vram *v,
		u64 va, u32 offset, u32 bytes, u32 flags)
{
	struct drm_gem_object *obj;
	struct mt_gem_object *g;
	int ret;
	if (!s || !file || !file->minor || file->minor->dev != drm || !v ||
	    v->store->buffers != s->buffers)
		return -EINVAL;
	/* Lookup returns a GEM reference; closing the handle concurrently cannot
	 * destroy its BO while the VM acquires its own reference. */
	obj = drm_gem_object_lookup(file, handle);
	if (!obj)
		return -ENOENT;
	if (obj->dev != drm || obj->funcs != &mt_gem_funcs) {
		ret = -EXDEV;
		goto put;
	}
	g = container_of(obj, struct mt_gem_object, base);
	if (g->store != s) {
		ret = -EXDEV;
		goto put;
	}
	mutex_lock(s->buffers->lock);
	ret = v->store->ops->bind(v, g->bo, va, offset, bytes, flags);
	mutex_unlock(s->buffers->lock);
put:
	drm_gem_object_put(obj);
	return ret;
}

static int mt_gem_prepare_work(struct mt_gem_store *s, struct drm_device *drm,
		struct drm_file *file, u32 handle, struct mt_vm_vram *v,
		const struct mt_work_command_inputs *request, void *out, u32 capacity)
{
	struct drm_gem_object *obj;
	struct mt_gem_object *g;
	int ret;
	if (!s || !file || !file->minor || file->minor->dev != drm || !v ||
	    v->store->buffers != s->buffers)
		return -EINVAL;
	obj = drm_gem_object_lookup(file, handle);
	if (!obj)
		return -ENOENT;
	if (obj->dev != drm || obj->funcs != &mt_gem_funcs) {
		ret = -EXDEV;
		goto put;
	}
	g = container_of(obj, struct mt_gem_object, base);
	if (g->store != s) {
		ret = -EXDEV;
		goto put;
	}
	mutex_lock(s->buffers->lock);
	ret = request ? mt_device_profile_work(&s->profile, request->type) : -EINVAL;
	if (!ret)
		ret = mt_work_command_prepare(out, capacity, &v->vm, g->bo, request);
	mutex_unlock(s->buffers->lock);
put:
	drm_gem_object_put(obj);
	return ret;
}

/* Shared object lookup for CPU-only CE payload or TQX geometry preparation.
 * Neither output is a complete executable command buffer. */
static int mt_gem_prepare_copy_pair(struct mt_gem_store *s, struct drm_device *drm,
		struct drm_file *file, u32 src_handle, u32 dst_handle, struct mt_vm_vram *v,
		const void *request, bool tqx, void *out, u32 capacity)
{
	struct drm_gem_object *src, *dst;
	struct mt_gem_object *a, *b;
	int ret = -EXDEV;
	if (!s || !file || !file->minor || file->minor->dev != drm || !v ||
	    v->store->buffers != s->buffers)
		return -EINVAL;
	src = drm_gem_object_lookup(file, src_handle);
	if (!src)
		return -ENOENT;
	dst = drm_gem_object_lookup(file, dst_handle);
	if (!dst) {
		ret = -ENOENT;
		goto put_src;
	}
	if (src->dev != drm || dst->dev != drm || src->funcs != &mt_gem_funcs ||
	    dst->funcs != &mt_gem_funcs)
		goto put_both;
	a = container_of(src, struct mt_gem_object, base);
	b = container_of(dst, struct mt_gem_object, base);
	if (a->store != s || b->store != s)
		goto put_both;
	mutex_lock(s->buffers->lock);
	if (!request) {
		ret = -EINVAL;
	} else if (tqx) {
		ret = mt_device_profile_transfer(&s->profile);
		if (!ret && s->profile.family != 2)
			ret = -EOPNOTSUPP; /* Source descriptors traced for local family 2. */
		if (!ret)
			ret = mt_tqx_copy_prepare(out, capacity, &v->vm, a->bo, b->bo, request);
	} else {
		const struct mt_ce_copy_input *ce = request;
		ret = mt_device_profile_ce(&s->profile, ce->version);
		if (!ret)
			ret = mt_ce_copy_prepare(out, capacity, &v->vm, a->bo, b->bo, ce);
	}
	mutex_unlock(s->buffers->lock);
put_both:
	drm_gem_object_put(dst);
put_src:
	drm_gem_object_put(src);
	return ret;
}

static int mt_gem_prepare_copy(struct mt_gem_store *s, struct drm_device *drm,
		struct drm_file *file, u32 src_handle, u32 dst_handle, struct mt_vm_vram *v,
		const struct mt_ce_copy_input *request, void *out, u32 capacity)
{
	return mt_gem_prepare_copy_pair(s, drm, file, src_handle, dst_handle, v,
			request, false, out, capacity);
}

static int mt_gem_prepare_tqx_copy(struct mt_gem_store *s, struct drm_device *drm,
		struct drm_file *file, u32 src_handle, u32 dst_handle, struct mt_vm_vram *v,
		const struct mt_tqx_copy_input *request, void *out, u32 capacity)
{
	return mt_gem_prepare_copy_pair(s, drm, file, src_handle, dst_handle, v,
			request, true, out, capacity);
}

static int mt_gem_prepare_copy_components(struct mt_gem_store *s, struct drm_device *drm,
		struct drm_file *file, u32 command_handle, u32 src_handle, u32 dst_handle,
		struct mt_vm_vram *v, const void *request,
		void *out, u32 capacity, bool paging)
{
	struct drm_gem_object *obj[3] = {NULL, NULL, NULL};
	struct mt_bo *bo[3];
	u32 handles[3] = {command_handle, src_handle, dst_handle}, i;
	int ret = -EINVAL;
	if (!s || !file || !file->minor || file->minor->dev != drm || !v ||
	    v->store->buffers != s->buffers)
		return ret;
	for (i = 0; i < 3; i++) {
		struct mt_gem_object *g;
		obj[i] = drm_gem_object_lookup(file, handles[i]);
		if (!obj[i]) { ret = -ENOENT; goto put; }
		if (obj[i]->dev != drm || obj[i]->funcs != &mt_gem_funcs) {
			ret = -EXDEV; goto put;
		}
		g = container_of(obj[i], struct mt_gem_object, base);
		if (g->store != s) { ret = -EXDEV; goto put; }
		bo[i] = g->bo;
	}
	mutex_lock(s->buffers->lock);
	ret = mt_device_profile_ce(&s->profile, 3);
	if (!ret && paging)
		ret = mt_ce3_paging_prepare(out, capacity, &v->vm, bo[0], bo[1], bo[2], request);
	else if (!ret)
		ret = mt_ce3_stream_prepare(out, capacity, &v->vm, bo[0], bo[1], bo[2], request);
	mutex_unlock(s->buffers->lock);
put:
	/* The same handle may occur three times; each lookup owns its own ref. */
	for (i = 0; i < 3; i++)
		if (obj[i])
			drm_gem_object_put(obj[i]);
	return ret;
}

static int mt_gem_prepare_copy_stream(struct mt_gem_store *s, struct drm_device *drm,
		struct drm_file *file, u32 command_handle, u32 src_handle, u32 dst_handle,
		struct mt_vm_vram *v, const struct mt_ce3_stream_input *request,
		void *out, u32 capacity)
{
	return mt_gem_prepare_copy_components(s, drm, file, command_handle, src_handle,
		dst_handle, v, request, out, capacity, false);
}

static int mt_gem_prepare_copy_paging(struct mt_gem_store *s, struct drm_device *drm,
		struct drm_file *file, u32 command_handle, u32 src_handle, u32 dst_handle,
		struct mt_vm_vram *v, const struct mt_ce3_paging_input *request,
		void *out, u32 capacity)
{
	return mt_gem_prepare_copy_components(s, drm, file, command_handle, src_handle,
		dst_handle, v, request, out, capacity, true);
}

/* CPU program image only. Heap selection, BO mapping and upload are separate
 * unfinished steps; this function never acquires a GPU reference. */
static int mt_gem_prepare_tqx_programs(struct mt_gem_store *s, void *out, u32 capacity)
{
	if (!s)
		return -EINVAL;
	return mt_tqx_program_bank_build(out, capacity, &s->profile);
}

static int mt_gem_tqx_stream(struct mt_gem_store *s, struct drm_device *drm,
		struct drm_file *file, const u32 handles[MT_TQX_BUFFER_COUNT], struct mt_vm_vram *v,
		const struct mt_tqx_heap_input *request, void *out, u32 capacity, bool upload)
{
	struct drm_gem_object *obj[MT_TQX_BUFFER_COUNT] = {0};
	const struct mt_bo *bo[MT_TQX_BUFFER_COUNT];
	struct mt_bo *writable[MT_TQX_BUFFER_COUNT];
	struct mt_tqx_upload_workspace *workspace = NULL;
	static const struct mt_tqx_upload_ops io = {mt_bo_vram_write, mt_bo_vram_read};
	u32 i;
	int ret = -EINVAL;
	if (!s || !drm || !file || !file->minor || file->minor->dev != drm || !handles ||
	    !v || !v->store || v->store->buffers != s->buffers)
		return ret;
	if (upload) {
		workspace = kzalloc(sizeof(*workspace), GFP_KERNEL);
		if (!workspace)
			return -ENOMEM;
	}
	for (i = 0; i < MT_TQX_BUFFER_COUNT; i++) {
		struct mt_gem_object *g;
		obj[i] = drm_gem_object_lookup(file, handles[i]);
		if (!obj[i]) { ret = -ENOENT; goto put; }
		if (obj[i]->dev != drm || obj[i]->funcs != &mt_gem_funcs) {
			ret = -EXDEV; goto put;
		}
		g = container_of(obj[i], struct mt_gem_object, base);
		if (g->store != s) { ret = -EXDEV; goto put; }
		bo[i] = g->bo;
		writable[i] = g->bo;
	}
	mutex_lock(s->buffers->lock);
	if (upload)
		ret = mt_tqx_upload(out, capacity, workspace, &io, &s->profile, &v->vm, writable, request);
	else
		ret = mt_tqx_heap_stream_prepare(out, capacity, &s->profile, &v->vm, bo, request);
	mutex_unlock(s->buffers->lock);
put:
	for (i = 0; i < MT_TQX_BUFFER_COUNT; i++)
		if (obj[i])
			drm_gem_object_put(obj[i]);
	kfree(workspace);
	return ret;
}

static int mt_gem_prepare_tqx_stream(struct mt_gem_store *s, struct drm_device *drm,
		struct drm_file *file, const u32 *handles, struct mt_vm_vram *v,
		const struct mt_tqx_heap_input *request, void *out, u32 capacity)
{
	return mt_gem_tqx_stream(s, drm, file, handles, v, request, out, capacity, false);
}

static int mt_gem_upload_tqx_stream(struct mt_gem_store *s, struct drm_device *drm,
		struct drm_file *file, const u32 *handles, struct mt_vm_vram *v,
		const struct mt_tqx_heap_input *request, void *out, u32 capacity)
{
	return mt_gem_tqx_stream(s, drm, file, handles, v, request, out, capacity, true);
}

/* CPU-only descriptor encoder; new DMA/state BOs are not admitted for work. */
static int mt_gem_encode_tqx_dma(struct mt_gem_store *s, const struct mt_tqx_upload_result *source,
		const struct mt_tqx_dma_input *input, void *out, u32 capacity)
{
	struct mt_tqx_dma_input resolved;
	int ret;
	if (!s || !s->buffers || !input)
		return -EINVAL;
	resolved = *input;
	mutex_lock(s->buffers->lock);
	/* Zero means use the queried count. Explicit counts remain available
	 * for CPU-only fixtures when no information page has been installed. */
	if (s->tqx_cores && resolved.cores && resolved.cores != s->tqx_cores)
		ret = -EINVAL;
	else {
		if (!resolved.cores)
			resolved.cores = s->tqx_cores;
		ret = mt_tqx_dma_encode(out, capacity, &s->profile, source, &resolved);
	}
	mutex_unlock(s->buffers->lock);
	return ret;
}

static int mt_gem_tqx_submission(struct mt_gem_store *s, struct drm_device *drm,
		struct drm_file *file, const u32 *handles, struct mt_vm_vram *v,
		const struct mt_tqx_submission_input *request, void *out, u32 capacity,
		struct mt_execution_context *context, struct mt_tqx_work *work)
{
	struct drm_gem_object *obj[MT_TQX_SUBMISSION_BUFFERS] = {0};
	struct mt_bo *bo[MT_TQX_SUBMISSION_BUFFERS];
	struct mt_tqx_submission_workspace *workspace;
	static const struct mt_tqx_upload_ops io = {mt_bo_vram_write, mt_bo_vram_read};
	u32 i;
	int ret = -EINVAL;
	if (!s || !s->buffers || !drm || !file || !file->minor || file->minor->dev != drm || !handles ||
	    !v || !v->store || v->store->buffers != s->buffers || !request ||
	    (!work && (!out || capacity < sizeof(struct mt_tqx_submission_result))))
		return ret;
	if (work && (!context || !context->process || !context->process->store ||
	    context->process->vm != &v->vm || context->process->store->buffers != s->buffers))
		return -EXDEV;
	workspace = kzalloc(sizeof(*workspace), GFP_KERNEL);
	if (!workspace)
		return -ENOMEM;
	for (i = 0; i < MT_TQX_SUBMISSION_BUFFERS; i++) {
		struct mt_gem_object *g;
		obj[i] = drm_gem_object_lookup(file, handles[i]);
		if (!obj[i]) { ret = -ENOENT; goto put; }
		if (obj[i]->dev != drm || obj[i]->funcs != &mt_gem_funcs) {
			ret = -EXDEV; goto put;
		}
		g = container_of(obj[i], struct mt_gem_object, base);
		if (g->store != s) { ret = -EXDEV; goto put; }
		bo[i] = g->bo;
	}
	mutex_lock(s->buffers->lock);
	if (!s->tqx_cores)
		ret = -ENODATA;
	else if (work)
		ret = mt_tqx_work_prepare(work, workspace, &io, &s->profile,
			s->tqx_cores, context, bo, request);
	else
		ret = mt_tqx_submission_upload(out, capacity, workspace, &io, &s->profile,
			s->tqx_cores, &v->vm, bo, request);
	mutex_unlock(s->buffers->lock);
put:
	for (i = 0; i < MT_TQX_SUBMISSION_BUFFERS; i++)
		if (obj[i])
			drm_gem_object_put(obj[i]);
	kfree(workspace);
	return ret;
}

static int mt_gem_upload_tqx_submission(struct mt_gem_store *s, struct drm_device *drm,
		struct drm_file *file, const u32 *handles, struct mt_vm_vram *v,
		const struct mt_tqx_submission_input *request, void *out, u32 capacity)
{
	return mt_gem_tqx_submission(s, drm, file, handles, v, request, out, capacity, NULL, NULL);
}

static int mt_gem_prepare_tqx_work(struct mt_gem_store *s, struct drm_device *drm,
		struct drm_file *file, const u32 *handles, struct mt_vm_vram *v,
		struct mt_execution_context *context, const struct mt_tqx_submission_input *request,
		struct mt_tqx_work *work)
{
	if (!work)
		return -EINVAL;
	return mt_gem_tqx_submission(s, drm, file, handles, v, request, NULL, 0, context, work);
}

/* User-owned GEM objects remain limited to command/source/destination/DMA/
 * engine state. Shader/texture/PDS state are allocated from the decoded Guest
 * pools, and the PDS code BO comes from the retained static shared resource. */
static int mt_gem_prepare_tqx_work_from_pools(struct mt_gem_store *s,
		struct drm_device *drm, struct drm_file *file, const u32 handles[5],
		struct mt_vm_vram *v, struct mt_execution_context *context,
		const struct mt_tqx_submission_input *request, struct mt_tqx_work *work)
{
	struct drm_gem_object *obj[5] = {0};
	struct mt_bo *ordinary[5];
	struct mt_tqx_submission_workspace *workspace;
	static const struct mt_tqx_upload_ops io = {mt_bo_vram_write, mt_bo_vram_read};
	u32 i;
	int ret = -EINVAL;
	if (!s || !s->buffers || !drm || !file || !file->minor || file->minor->dev != drm ||
	    !handles || !v || !v->store || v->store->buffers != s->buffers ||
	    !v->store->boot || !context || !context->process || !context->process->store ||
	    context->process->vm != &v->vm || !request || !work)
		return ret;
	if (context->process->store->buffers != s->buffers)
		return -EXDEV;
	workspace = kzalloc(sizeof(*workspace), GFP_KERNEL);
	if (!workspace)
		return -ENOMEM;
	for (i = 0; i < ARRAY_SIZE(obj); i++) {
		struct mt_gem_object *g;
		obj[i] = drm_gem_object_lookup(file, handles[i]);
		if (!obj[i]) { ret = -ENOENT; goto put; }
		if (obj[i]->dev != drm || obj[i]->funcs != &mt_gem_funcs) {
			ret = -EXDEV; goto put;
		}
		g = container_of(obj[i], struct mt_gem_object, base);
		if (g->store != s) { ret = -EXDEV; goto put; }
		ordinary[i] = g->bo;
	}
	mutex_lock(s->buffers->lock);
	if (!s->tqx_cores)
		ret = -ENODATA;
	else
		ret = mt_tqx_work_prepare_from_pools(work, v->store->boot, workspace, &io,
			&s->profile, s->tqx_cores, context, ordinary, request);
	mutex_unlock(s->buffers->lock);
put:
	for (i = 0; i < ARRAY_SIZE(obj); i++)
		if (obj[i])
			drm_gem_object_put(obj[i]);
	kfree(workspace);
	return ret;
}

static int mt_gem_cancel_tqx_work(struct mt_gem_store *s, struct mt_tqx_work *work)
{
	int ret;
	if (!s || !s->buffers || !work)
		return -EINVAL;
	mutex_lock(s->buffers->lock);
	if (!work->context || !work->context->process || !work->context->process->store ||
	    work->context->process->store->buffers != s->buffers)
		ret = -EXDEV;
	else
		ret = mt_tqx_work_cancel(work);
	mutex_unlock(s->buffers->lock);
	return ret;
}

static int mt_gem_release_tqx_context_pools(struct mt_gem_store *s,
		struct mt_execution_context *context)
{
	int ret;
	if (!s || !s->buffers || !context || !context->process || !context->process->store)
		return -EINVAL;
	mutex_lock(s->buffers->lock);
	if (context->process->store->buffers != s->buffers)
		ret = -EXDEV;
	else
		ret = mt_tqx_context_pool_slices_release(context);
	mutex_unlock(s->buffers->lock);
	return ret;
}

/* Mirror the Windows DestroyContext cleanup for Linux-owned pool slices.
 * This is allocator teardown only: the sealed VM mapping and root stay alive
 * until their own withdrawal protocol is available. */
static int mt_gem_destroy_execution_context(struct mt_gem_store *s,
		struct mt_execution_context *context)
{
	int ret;
	if (!s || !s->buffers || !context || !context->process || !context->process->store)
		return -EINVAL;
	mutex_lock(s->buffers->lock);
	if (context->process->store->buffers != s->buffers)
		ret = -EXDEV;
	else {
		ret = mt_tqx_context_pool_slices_release(context);
		if (!ret)
			ret = mt_execution_context_destroy(context);
	}
	mutex_unlock(s->buffers->lock);
	return ret;
}

static const struct mt_gem_ops mt_gem_operations = {
	.prepare_tqx_work = mt_gem_prepare_tqx_work,
	.prepare_tqx_work_from_pools = mt_gem_prepare_tqx_work_from_pools,
	.cancel_tqx_work = mt_gem_cancel_tqx_work,
	.release_tqx_context_pools = mt_gem_release_tqx_context_pools,
	.destroy_execution_context = mt_gem_destroy_execution_context,
	.upload_tqx_submission = mt_gem_upload_tqx_submission,
	.encode_tqx_dma = mt_gem_encode_tqx_dma,
	.upload_tqx_stream = mt_gem_upload_tqx_stream,
	.prepare_tqx_stream = mt_gem_prepare_tqx_stream,
	.prepare_tqx_programs = mt_gem_prepare_tqx_programs,
	.create = mt_gem_create, .create_handle = mt_gem_create_handle,
	.bind_handle = mt_gem_bind_handle,
	.prepare_work = mt_gem_prepare_work, .prepare_copy = mt_gem_prepare_copy,
	.prepare_copy_stream = mt_gem_prepare_copy_stream,
	.prepare_copy_paging = mt_gem_prepare_copy_paging,
	.prepare_tqx_copy = mt_gem_prepare_tqx_copy,
};

static inline void mt_gem_store_init(struct mt_gem_store *s, struct mt_bo_store *buffers,
		struct device *parent, const struct mt_device_profile *profile)
{
	*s = (struct mt_gem_store){.buffers = buffers, .parent = parent, .ops = &mt_gem_operations};
	if (profile)
		s->profile = *profile;
}
#endif
