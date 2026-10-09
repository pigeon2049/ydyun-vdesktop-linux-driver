/* Actual mt_gem.h/BO/VM code with modeled DRM references, file handle tables,
 * mutexes and RAM backing. This is not a test of the real Linux DRM core. */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../kernel/mt_gpu_vm.h"
#define MT_GUEST_VM_VRAM_H
#define PAGE_SIZE 4096U
#define GFP_KERNEL 0
#define DRIVER_GEM 1
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#undef WARN_ON
#define WARN_ON(x) ((x) ? (assert(!(x)), 1) : 0)
struct mutex { bool held; };
static void mutex_lock(struct mutex *m) { assert(!m->held); m->held = true; }
static void mutex_unlock(struct mutex *m) { assert(m->held); m->held = false; }
#define lockdep_assert_held(m) assert((m)->held)
#include "mt_boot_bo_model.h"
static unsigned heap_allocs, heap_frees, fail_heap_at;
static void *kzalloc(size_t bytes, int flags)
{
	void *p;
	(void)flags;
	if (fail_heap_at && !--fail_heap_at) return NULL;
	p = calloc(1, bytes);
	assert(p);
	heap_allocs++;
	return p;
}
static void kfree(void *p) { if (p) { heap_frees++; free(p); } }
struct device { unsigned id; };
struct drm_device { unsigned refs, features; struct device *dev; };
struct drm_minor { struct drm_device *dev; };
struct dma_buf;
struct vm_area_struct;
struct drm_gem_object;
struct drm_gem_object_funcs {
	void (*free)(struct drm_gem_object *);
	struct dma_buf *(*export)(struct drm_gem_object *, int);
	int (*mmap)(struct drm_gem_object *, struct vm_area_struct *);
};
struct drm_gem_object {
	unsigned refs;
	size_t size;
	struct drm_device *dev;
	const struct drm_gem_object_funcs *funcs;
};
struct drm_file {
	struct drm_minor *minor;
	struct drm_gem_object *handles[16];
	bool fail_handle, close_after_lookup;
};
static bool drm_core_check_feature(struct drm_device *d, unsigned f) { return d->features & f; }
static void drm_dev_get(struct drm_device *d) { assert(d->refs); d->refs++; }
static void drm_dev_put(struct drm_device *d) { assert(d->refs > 1); d->refs--; }
static unsigned gem_inits, gem_releases;
static void drm_gem_private_object_init(struct drm_device *d, struct drm_gem_object *o, size_t bytes)
{ assert(!(bytes & 4095)); o->dev = d; o->size = bytes; o->refs = 1; gem_inits++; }
static void drm_gem_object_release(struct drm_gem_object *o)
{ assert(!o->refs && o->dev->refs > 1); gem_releases++; }
static void drm_gem_object_put(struct drm_gem_object *o)
{ assert(o->refs); if (!--o->refs) o->funcs->free(o); }
static int drm_gem_handle_create(struct drm_file *f, struct drm_gem_object *o, u32 *handle)
{
	u32 i;
	if (f->fail_handle) return -ENOMEM;
	for (i = 1; i < 16; i++) if (!f->handles[i]) {
		f->handles[i] = o; o->refs++; *handle = i; return 0;
	}
	return -ENOSPC;
}
static void close_handle(struct drm_file *f, u32 handle)
{
	struct drm_gem_object *o = f->handles[handle];
	assert(o);
	f->handles[handle] = NULL;
	drm_gem_object_put(o);
}
static struct drm_gem_object *drm_gem_object_lookup(struct drm_file *f, u32 handle)
{
	struct drm_gem_object *o = handle < 16 ? f->handles[handle] : NULL;
	if (o) {
		o->refs++;
		/* Deterministic close interleaving after the protected lookup. */
		if (f->close_after_lookup) close_handle(f, handle);
	}
	return o;
}
struct mt_vm_vram;
struct mt_vm_store_ops {
	int (*bind)(struct mt_vm_vram *, struct mt_bo *, u64, u32, u32, u32);
};
struct mt_vm_store {
	struct mt_bo_store *buffers;
	struct mt_boot_bo_store *boot;
	const struct mt_vm_store_ops *ops;
};
struct mt_vm_vram { struct mt_gpu_vm vm; struct mt_vm_store *store; };
static int bind_vm(struct mt_vm_vram *v, struct mt_bo *b, u64 va, u32 o, u32 n, u32 flags)
{ assert(v->store->buffers->lock->held); return mt_gpu_vm_bind(&v->vm, b, va, o, n, flags); }
static const struct mt_vm_store_ops vm_ops = {bind_vm};
static int backing_alloc(void *opaque, u32 bytes, u32 align, struct mt_bo_backing *out)
{
	struct mt_bo_store *s = opaque;
	void *p;
	assert(s->lock->held);
	if (s->fail_alloc) return -ENOSPC;
	p = malloc(bytes);
	assert(p);
	s->next_pa = (s->next_pa + align - 1) & ~(u64)(align - 1);
	*out = (struct mt_bo_backing){p, s->next_pa, s->next_pa, bytes};
	s->next_pa += bytes;
	s->allocated++;
	return 0;
}
static int backing_clear(void *opaque, const struct mt_bo_backing *b)
{
	struct mt_bo_store *s = opaque;
	assert(s->lock->held);
	if (s->fail_clear) return -EIO;
	memset(b->handle, 0, b->bytes);
	return 0;
}
static void backing_free(void *opaque, const struct mt_bo_backing *b)
{ struct mt_bo_store *s = opaque; assert(s->lock->held); s->freed++; free(b->handle); }
static int backing_map(void *s, const struct mt_bo_backing *b, void **out)
{ (void)s; *out = b->handle; return 0; }
static void backing_unmap(void *s, const struct mt_bo_backing *b) { (void)s; (void)b; }
static const struct mt_bo_ops backing_ops = {
	backing_alloc, backing_clear, backing_free, backing_map, backing_unmap,
};
/* RAM model for the production BAR I/O helpers used by the GEM bridge. */
static int mt_bo_vram_write(struct mt_bo *bo,u64 offset,const void *src,u64 bytes)
{
 void *mapping;int ret=mt_bo_check_range(bo,offset,bytes);if(ret)return ret;
 ret=mt_bo_cpu_begin(bo,&mapping);if(ret)return ret;
 memcpy((u8 *)mapping+offset,src,bytes);return mt_bo_cpu_end(bo);
}
static int mt_bo_vram_read(struct mt_bo *bo,u64 offset,void *dst,u64 bytes)
{
 void *mapping;int ret=mt_bo_check_range(bo,offset,bytes);if(ret)return ret;
 ret=mt_bo_cpu_begin(bo,&mapping);if(ret)return ret;
 memcpy(dst,(u8 *)mapping+offset,bytes);return mt_bo_cpu_end(bo);
}
#include "../../kernel/mt_gem.h"

/* Exercise all seven real BO mappings through the handle bridge, including
 * cleanup after failures at every lookup position and file-close races. */
static void tqx_heap_bridge(struct mt_gem_store *store, struct drm_device *drm,
        struct drm_file *file, struct mt_vm_store *vm_store)
{
 struct mt_vm_vram v={.store=vm_store};
 struct mt_bo tables={0};
 struct mt_tqx_heap_input req={.copy={0x40100001,0x40200000,8191},
     .va={0x40000000,0x8400000000ULL,0x8100000000ULL,0x8100001000ULL,0xf000000000ULL}};
 struct mt_tqx_copy_stream_image out,saved;
 struct mt_tqx_upload_result uploaded;
 u32 handles[7],bad[7],i,j;
 u64 va[7];u32 bytes[7];
 void *image=calloc(1,65536),*scratch=calloc(1,65536);
 struct mutex *lock=vm_store->buffers->lock;
 assert(image && scratch && !store->objects);
 file->close_after_lookup=false;
 assert(!mt_device_profile_select(&store->profile,0x1ed5,0x222));
 mutex_lock(lock);
 assert(!mt_bo_create(&tables,&backing_ops,vm_store->buffers,65536,4096));
 assert(!mt_gpu_vm_init(&v.vm,&tables,image,scratch,65536));
 assert(!mt_bo_put(&tables));mutex_unlock(lock);
 for(i=0;i<7;i++) {
  bytes[i]=i<5?mt_tqx_buffer_bytes[i]:8192;
  va[i]=i<5?req.va[i]:(i==5?req.copy.src-1:req.copy.dst);
  assert(!store->ops->create_handle(store,drm,file,bytes[i],&handles[i]));
  assert(!store->ops->bind_handle(store,drm,file,handles[i],&v,va[i],0,bytes[i],MT_GPU_MAP_DEFAULT));
 }
 assert(!store->ops->prepare_tqx_stream(store,drm,file,handles,&v,&req,&out,sizeof(out)));
 saved=out;assert(out.count==3 && out.command_bytes==240);
 fail_heap_at=1;
 assert(store->ops->upload_tqx_stream(store,drm,file,handles,&v,&req,&uploaded,sizeof(uploaded))==-ENOMEM);
 assert(!store->ops->upload_tqx_stream(store,drm,file,handles,&v,&req,&uploaded,sizeof(uploaded)));
 assert(!memcmp(uploaded.record,out.record,296));
 for(i=0;i<7;i++) {
  memcpy(bad,handles,sizeof(bad));bad[i]=0;
  assert(store->ops->prepare_tqx_stream(store,drm,file,bad,&v,&req,&out,sizeof(out))==-ENOENT);
  assert(store->ops->upload_tqx_stream(store,drm,file,bad,&v,&req,&uploaded,sizeof(uploaded))==-ENOENT);
  assert(!memcmp(&out,&saved,sizeof(out)));
  for(j=0;j<7;j++)assert(file->handles[handles[j]]->refs==1);
 }
 /* Different VA, same backing: state and texture aliases of a command page. */
 for(i=3;i<=4;i++) {
  struct mt_bo *command=container_of(file->handles[handles[0]],struct mt_gem_object,base)->bo;
  mutex_lock(lock);assert(!mt_gpu_vm_unbind(&v.vm,va[i],bytes[i]));
  assert(!mt_gpu_vm_bind(&v.vm,command,va[i],0,bytes[i],MT_GPU_MAP_DEFAULT));mutex_unlock(lock);
  memcpy(bad,handles,sizeof(bad));bad[i]=handles[0];
  assert(store->ops->prepare_tqx_stream(store,drm,file,bad,&v,&req,&out,sizeof(out))==-EINVAL);
  assert(!memcmp(&out,&saved,sizeof(out)) && file->handles[handles[0]]->refs==1);
  mutex_lock(lock);assert(!mt_gpu_vm_unbind(&v.vm,va[i],bytes[i]));mutex_unlock(lock);
  assert(!store->ops->bind_handle(store,drm,file,handles[i],&v,va[i],0,bytes[i],MT_GPU_MAP_DEFAULT));
 }
 /* A source VA may also alias a state allocation; check all seven intervals. */
 req.copy.src=req.va[3];req.copy.bytes=256;memcpy(bad,handles,sizeof(bad));bad[5]=handles[3];
 assert(store->ops->prepare_tqx_stream(store,drm,file,bad,&v,&req,&out,sizeof(out))==-EINVAL);
 assert(!memcmp(&out,&saved,sizeof(out)));
 req.copy.src=va[5]+1;req.copy.bytes=8191;
 assert(store->ops->prepare_tqx_stream(store,drm,file,handles,&v,&req,&out,sizeof(out)-1)==-EINVAL);
 assert(store->ops->prepare_tqx_stream(store,drm,file,handles,&v,NULL,&out,sizeof(out))==-EINVAL);
 assert(!memcmp(&out,&saved,sizeof(out)));
 /* Each lookup takes an independent ref even if all handles close mid-call. */
 file->close_after_lookup=true;
 assert(!store->ops->upload_tqx_stream(store,drm,file,handles,&v,&req,&uploaded,sizeof(uploaded)));
 assert(!store->objects && drm->refs==1 && !v.vm.active_uses && !v.vm.uploaded);
 for(i=0;i<7;i++)assert(!file->handles[handles[i]] && v.vm.bindings[i].bo->refs==1);
 file->close_after_lookup=false;
 mutex_lock(lock);assert(!mt_gpu_vm_fini(&v.vm));mutex_unlock(lock);
 free(scratch);free(image);
}

static void tqx_submission_bridge(struct mt_gem_store *store, struct drm_device *drm,
        struct drm_file *file, struct mt_vm_store *vm_store)
{
 struct mt_vm_vram v={.store=vm_store};struct mt_bo tables={0};
 struct mt_tqx_submission_input req={
  .stream={.copy={0x40100001,0x40200000,8191},
   .va={0x40000000,0x8400000000ULL,0x8100000000ULL,0x8100001000ULL,0xf000000000ULL}},
  .dma_va=0x40010000,.state_va=0x40020000};
 struct mt_tqx_submission_result out,saved;
 struct mt_tqx_work work={0};
 struct mt_execution_store execution;
 struct mt_execution_process process={0};
 struct mt_execution_context context={0};
 u32 handles[9],bad[9],i,j,bytes;u64 va,field;
 void *image=calloc(1,65536),*scratch=calloc(1,65536);
 struct mutex *lock=vm_store->buffers->lock;
 assert(image && scratch && !store->objects && !store->tqx_cores);
 mt_fw_put32(scratch,0,0xaa557491);mt_fw_put32(scratch,4,2);mt_fw_put32(scratch,0xc9c,1);
 assert(!mt_gem_configure_tqx_info(store,scratch,4096));
 mutex_lock(lock);
 assert(!mt_bo_create(&tables,&backing_ops,vm_store->buffers,65536,4096));
 assert(!mt_gpu_vm_init(&v.vm,&tables,image,scratch,65536));
 assert(!mt_bo_put(&tables));mutex_unlock(lock);
 for(i=0;i<9;i++) {
  bytes=i<5?mt_tqx_buffer_bytes[i]:(i==8?4096:8192);
  va=i<5?req.stream.va[i]:(i==5?req.stream.copy.src-1:
   i==6?req.stream.copy.dst:i==7?req.dma_va:req.state_va);
  assert(!store->ops->create_handle(store,drm,file,bytes,&handles[i]));
  assert(!store->ops->bind_handle(store,drm,file,handles[i],&v,va,0,bytes,MT_GPU_MAP_DEFAULT));
 }
 memset(&out,0xa5,sizeof(out));saved=out;
 fail_heap_at=1;
 assert(store->ops->upload_tqx_submission(store,drm,file,handles,&v,&req,&out,sizeof(out))==-ENOMEM);
 assert(!memcmp(&out,&saved,sizeof(out)));
 store->tqx_cores=0; /* Missing-query fixture, restored before any upload. */
 assert(store->ops->upload_tqx_submission(store,drm,file,handles,&v,&req,&out,sizeof(out))==-ENODATA);
 assert(!memcmp(&out,&saved,sizeof(out)));store->tqx_cores=1;
 assert(!store->ops->upload_tqx_submission(store,drm,file,handles,&v,&req,&out,sizeof(out)));
 saved=out;memcpy(&field,out.view,8);assert(field==req.dma_va);
 memcpy(&field,out.view+8,8);assert(field==4864);
 for(i=0;i<9;i++) {
  memcpy(bad,handles,sizeof(bad));bad[i]=0;
  assert(store->ops->upload_tqx_submission(store,drm,file,bad,&v,&req,&out,sizeof(out))==-ENOENT);
  assert(!memcmp(&out,&saved,sizeof(out)));
  for(j=0;j<9;j++)assert(file->handles[handles[j]]->refs==1);
 }
 mutex_lock(lock);
 mt_execution_store_init(&execution,vm_store->buffers,&store->profile);
 assert(!mt_execution_process_create(&execution,&process,&v.vm,1234));
 assert(!mt_execution_context_create(&context,&process,1,0));
 mutex_unlock(lock);
 for(i=0;i<9;i++) {
  memcpy(bad,handles,sizeof(bad));bad[i]=0;
  assert(store->ops->prepare_tqx_work(store,drm,file,bad,&v,&context,&req,&work)==-ENOENT);
  assert(!work.context && !context.active_jobs && !v.vm.active_uses);
 }
 file->close_after_lookup=true;
 assert(!store->ops->prepare_tqx_work(store,drm,file,handles,&v,&context,&req,&work));
 assert(!store->objects && drm->refs==1 && v.vm.active_uses==1 && v.vm.uploaded);
 for(i=0;i<9;i++)assert(!file->handles[handles[i]] && v.vm.bindings[i].bo->refs==2);
 assert(context.active_jobs==1 && work.job.count==10);
 assert(!store->ops->cancel_tqx_work(store,&work));
 mutex_lock(lock);
 assert(!mt_execution_context_destroy(&context));
 assert(!mt_execution_process_destroy(&process));
 mutex_unlock(lock);
 file->close_after_lookup=false;
 mutex_lock(lock);assert(!mt_gpu_vm_fini(&v.vm));mutex_unlock(lock);
 free(scratch);free(image);
}

int main(void)
{
	struct mutex lock = {0};
	struct device parent = {1}, other_parent = {2};
	struct drm_device drm = {1, DRIVER_GEM, &parent};
	struct drm_minor minor = {&drm};
	struct drm_file file = {.minor = &minor}, other_file = {.minor = &minor};
	struct mt_bo_store buffers = {
		.lock = &lock, .ops = &backing_ops, .next_pa = 0x600000000ULL,
	};
	struct mt_gem_store store, other_store;
	struct mt_device_profile profile, local_profile;
	struct mt_vm_store vm_store = {.buffers = &buffers, .ops = &vm_ops};
	struct mt_vm_vram v = {.store = &vm_store};
	struct mt_bo tables = {0}, *held;
	struct drm_gem_object *obj = (void *)1;
	u8 image[16384], scratch[16384];
	u32 handle, sentinel = 0xfefefefe;
	unsigned i, before;
	void *mapping;
	assert(!mt_device_profile_select(&profile, 0x1ed5, 0x600)); /* Explicit CE3 RAM fixture. */
	assert(!mt_device_profile_select(&local_profile, 0x1ed5, 0x222));
	mt_gem_store_init(&store, &buffers, &parent, &profile);
	mt_gem_store_init(&other_store, &buffers, &parent, &profile);
	assert(mt_gem_create(&store, &drm, 0, &obj) == -EINVAL && obj == (void *)1);
	drm.dev = &other_parent;
	assert(mt_gem_create(&store, &drm, 4096, &obj) == -EINVAL);
	drm.dev = &parent;
	drm.features = 0;
	assert(mt_gem_create(&store, &drm, 4096, &obj) == -EINVAL);
	drm.features = DRIVER_GEM;
	for (i = 1; i <= 2; i++) {
		fail_heap_at = i;
		assert(mt_gem_create(&store, &drm, 4096, &obj) == -ENOMEM);
		assert(obj == (void *)1 && heap_allocs == heap_frees);
	}
	buffers.fail_alloc = true;
	assert(mt_gem_create(&store, &drm, 4096, &obj) == -ENOSPC);
	buffers.fail_alloc = false;
	buffers.fail_clear = true;
	assert(mt_gem_create(&store, &drm, 4096, &obj) == -EIO);
	buffers.fail_clear = false;
	assert(obj == (void *)1 && !store.objects && drm.refs == 1);
	file.fail_handle = true;
	assert(mt_gem_create_handle(&store, &drm, &file, 4097, &sentinel) == -ENOMEM);
	assert(sentinel == 0xfefefefe && !store.objects && buffers.allocated == buffers.freed);
	file.fail_handle = false;
	/* Topology cannot be installed over pre-existing BOs, and cannot change
	 * after installation. Test both the probe's locked path and public path. */
	{
		struct drm_gem_object *topology_obj;
		other_store.profile=local_profile;
		memset(scratch,0,sizeof(scratch));
		mt_fw_put32(scratch,0,0xaa557491);mt_fw_put32(scratch,4,2);
		mt_fw_put32(scratch,0xc9c,1);
		assert(!mt_gem_create(&other_store,&drm,4096,&topology_obj));
		assert(mt_gem_configure_tqx_info(&other_store,scratch,sizeof(scratch))==-EBUSY);
		assert(!other_store.tqx_cores);
		drm_gem_object_put(topology_obj);
		mutex_lock(&lock);
		assert(!mt_gem_configure_tqx_info_locked(&other_store,scratch,sizeof(scratch)));
		mutex_unlock(&lock);
		assert(other_store.tqx_cores==1);
		assert(!mt_gem_configure_tqx_info(&other_store,scratch,sizeof(scratch)));
		mt_fw_put32(scratch,0xc9c,2);
		assert(mt_gem_configure_tqx_info(&other_store,scratch,sizeof(scratch))==-EBUSY);
		mt_fw_put32(scratch,0,0);
		assert(mt_gem_configure_tqx_info(&other_store,scratch,sizeof(scratch))==-EPROTO);
		assert(other_store.tqx_cores==1 && !lock.held);
		other_store.profile=profile;
	}
	mutex_lock(&lock);
	assert(!mt_bo_create(&tables, &backing_ops, &buffers, sizeof(image), 4096));
	assert(!mt_gpu_vm_init(&v.vm, &tables, image, scratch, sizeof(image)));
	assert(!mt_bo_put(&tables));
	mutex_unlock(&lock);
	assert(!mt_gem_create_handle(&store, &drm, &file, 4097, &handle));
	assert(store.objects == 1 && drm.refs == 2 && file.handles[handle]->size == 8192);
	obj = file.handles[handle];
	assert(obj->funcs->mmap(obj, NULL) == -EOPNOTSUPP);
	assert(obj->funcs->export(obj, 0) == ERR_PTR(-EOPNOTSUPP));
	assert(mt_gem_bind_handle(&store, &drm, &other_file, handle, &v, 0, 0, 4096, 0) == -ENOENT);
	assert(mt_gem_bind_handle(&other_store, &drm, &file, handle, &v, 0, 0, 4096, 0) == -EXDEV);
	assert(!mt_gem_bind_handle(&store, &drm, &file, handle, &v, 0x200000, 0, 8192, 3));
	assert(mt_gem_bind_handle(&store, &drm, &file, handle, &v, 0x200000, 0, 8192, 3) == -EEXIST);
	{
		struct mt_work_command_inputs request = {.root_pa = 0xbad000,
			.process_id = 0xf123456789abcdefULL, .command_va = 0x200008,
			.type = 5, .process_pid = 77, .bytes = 512, .fence = 99, .submit_flags = 0x80};
		u8 packet[96], saved[96];
		u64 field;
		u32 other_handle;
		memset(packet, 0xa5, sizeof(packet));
		assert(!store.ops->prepare_work(&store, &drm, &file, handle, &v, &request, packet, sizeof(packet)));
		memcpy(&field, packet + 0x18, 8);
		assert(field == tables.backing.gpu_pa && field != request.root_pa);
		memcpy(&field, packet + 0x20, 8);
		assert(field == request.process_id); /* process tokens are not masked to 40 bits */
		memcpy(&field, packet + 0x28, 8);
		assert(field == request.command_va && packet[0x0c] == 0x68 && packet[8] == 4);
		for (i = 80; i < sizeof(packet); i++) assert(packet[i] == 0xa5);
		memcpy(saved, packet, sizeof(packet));
		store.profile=local_profile;
		request.type=9;
		assert(store.ops->prepare_work(&store,&drm,&file,handle,&v,&request,packet,sizeof(packet))==-EOPNOTSUPP);
		request.type=5;
		store.profile=profile;
		assert(!mt_gem_create_handle(&store, &drm, &file, 8192, &other_handle));
		assert(store.ops->prepare_work(&store, &drm, &file, other_handle, &v, &request, packet, 80) == -ENOENT);
		close_handle(&file, other_handle);
		assert(store.ops->prepare_work(&store, &drm, &other_file, handle, &v, &request, packet, 80) == -ENOENT);
		assert(store.ops->prepare_work(&other_store, &drm, &file, handle, &v, &request, packet, 80) == -EXDEV);
		assert(store.ops->prepare_work(&store, &drm, &file, handle, &v, &request, packet, 79) == -EINVAL);
		request.type = 2;
		assert(store.ops->prepare_work(&store, &drm, &file, handle, &v, &request, packet, 80) == -EOPNOTSUPP);
		request.type = 5;
		request.submit_flags = 1;
		assert(store.ops->prepare_work(&store, &drm, &file, handle, &v, &request, packet, 80) == -EINVAL);
		request.submit_flags = 0;
		request.bytes = 8192;
		assert(store.ops->prepare_work(&store, &drm, &file, handle, &v, &request, packet, 80) == -ENOENT);
		request.command_va = (1ULL << 40) - 4096;
		assert(store.ops->prepare_work(&store, &drm, &file, handle, &v, &request, packet, 80) == -ERANGE);
		request.command_va = ~(u64)0;
		assert(store.ops->prepare_work(&store, &drm, &file, handle, &v, &request, packet, 80) == -ERANGE);
		assert(!memcmp(saved, packet, sizeof(packet)));
		assert(!v.vm.uploaded && !v.vm.sealed && v.vm.count == 1 && store.objects == 1);
		assert(v.vm.bindings[0].bo->refs == 2 && !v.vm.bindings[0].bo->gpu_users);
	}

	{
		struct mt_ce_copy_input copy = {.src=0x200000,.dst=0x201000,.bytes=256,.version=3};
		u8 packet[56], saved[56];
		u64 field;
		u32 missing;
		memset(packet,0xa5,sizeof(packet));
		assert(!store.ops->prepare_copy(&store,&drm,&file,handle,handle,&v,&copy,packet,sizeof(packet)));
		memcpy(&field,packet+8,8);assert(field==(0xc000000000000000ULL|copy.dst));
		memcpy(&field,packet+24,8);assert(field==copy.src);
		for (i=40;i<sizeof(packet);i++) assert(packet[i]==0xa5);
		memcpy(saved,packet,sizeof(packet));
		store.profile=local_profile; /* Actual 0222 must reject a CE request. */
		assert(store.ops->prepare_copy(&store,&drm,&file,handle,handle,&v,&copy,packet,sizeof(packet))==-EOPNOTSUPP);
		store.profile=profile;
		assert(!mt_gem_create_handle(&store,&drm,&file,4096,&missing));
		assert(store.ops->prepare_copy(&store,&drm,&file,handle,missing,&v,&copy,packet,56)==-ENOENT);
		close_handle(&file,missing);
		assert(store.ops->prepare_copy(&store,&drm,&file,handle,missing,&v,&copy,packet,56)==-ENOENT);
		assert(store.ops->prepare_copy(&store,&drm,&other_file,handle,handle,&v,&copy,packet,56)==-ENOENT);
		assert(store.ops->prepare_copy(&other_store,&drm,&file,handle,handle,&v,&copy,packet,56)==-EXDEV);
		assert(store.ops->prepare_copy(&store,&drm,&file,handle,handle,&v,&copy,packet,39)==-EINVAL);
		copy.bytes=0;
		assert(store.ops->prepare_copy(&store,&drm,&file,handle,handle,&v,&copy,packet,56)==-EINVAL);
		copy.bytes=4097;
		assert(store.ops->prepare_copy(&store,&drm,&file,handle,handle,&v,&copy,packet,56)==-ENOENT);
		copy.bytes=256;copy.src=(1ULL<<40)-1;
		assert(store.ops->prepare_copy(&store,&drm,&file,handle,handle,&v,&copy,packet,56)==-ERANGE);
		copy.src=0x200000;
		assert(!mt_gem_bind_handle(&store,&drm,&file,handle,&v,0x600000,0,4096,0));
		copy.dst=0x600008; /* Different VA, overlapping physical storage. */
		assert(store.ops->prepare_copy(&store,&drm,&file,handle,handle,&v,&copy,packet,56)==-EINVAL);
		assert(!memcmp(saved,packet,sizeof(packet)));
		mutex_lock(&lock);
		assert(!mt_gpu_vm_unbind(&v.vm,0x600000,4096));
		mutex_unlock(&lock);
		assert(v.vm.bindings[0].bo->refs==2 && !v.vm.bindings[0].bo->gpu_users);
	}

	{
		struct mt_tqx_copy_input req={.src=0x200001,.dst=0x201000,.bytes=4095};
		struct mt_tqx_copy_plan plan, saved;
		unsigned refs=obj->refs;
		u32 missing;
		store.profile=local_profile;
		assert(!store.ops->prepare_tqx_copy(&store,&drm,&file,handle,handle,&v,&req,&plan,sizeof(plan)));
		assert(plan.count==1 && plan.chunks[0].element_bytes==1 && plan.chunks[0].width==4095);
		saved=plan;
		store.profile=profile;
		assert(store.ops->prepare_tqx_copy(&store,&drm,&file,handle,handle,&v,&req,&plan,sizeof(plan))==-EOPNOTSUPP);
		assert(!mt_device_profile_select(&store.profile,0x1ed5,0x400));
		assert(store.ops->prepare_tqx_copy(&store,&drm,&file,handle,handle,&v,&req,&plan,sizeof(plan))==-EOPNOTSUPP);
		store.profile=local_profile;
		assert(!mt_gem_create_handle(&store,&drm,&file,4096,&missing));
		assert(store.ops->prepare_tqx_copy(&store,&drm,&file,handle,missing,&v,&req,&plan,sizeof(plan))==-ENOENT);
		close_handle(&file,missing);
		assert(store.ops->prepare_tqx_copy(&store,&drm,&file,missing,handle,&v,&req,&plan,sizeof(plan))==-ENOENT);
		assert(store.ops->prepare_tqx_copy(&store,&drm,&other_file,handle,handle,&v,&req,&plan,sizeof(plan))==-ENOENT);
		assert(store.ops->prepare_tqx_copy(&other_store,&drm,&file,handle,handle,&v,&req,&plan,sizeof(plan))==-EXDEV);
		assert(store.ops->prepare_tqx_copy(&store,&drm,&file,handle,handle,&v,&req,&plan,sizeof(plan)-1)==-EINVAL);
		assert(store.ops->prepare_tqx_copy(&store,&drm,&file,handle,handle,&v,NULL,&plan,sizeof(plan))==-EINVAL);
		req.bytes=4097;
		assert(store.ops->prepare_tqx_copy(&store,&drm,&file,handle,handle,&v,&req,&plan,sizeof(plan))==-ENOENT);
		req.bytes=0;
		assert(store.ops->prepare_tqx_copy(&store,&drm,&file,handle,handle,&v,&req,&plan,sizeof(plan))==-EINVAL);
		req.bytes=256;req.src=(1ULL<<40)-1;
		assert(store.ops->prepare_tqx_copy(&store,&drm,&file,handle,handle,&v,&req,&plan,sizeof(plan))==-ERANGE);
		req.src=0x200000;
		assert(!mt_gem_bind_handle(&store,&drm,&file,handle,&v,0x600000,0,4096,0));
		req.dst=0x600008;
		assert(store.ops->prepare_tqx_copy(&store,&drm,&file,handle,handle,&v,&req,&plan,sizeof(plan))==-EINVAL);
		assert(!memcmp(&plan,&saved,sizeof(plan)) && obj->refs==refs);
		mutex_lock(&lock);
		assert(!mt_gpu_vm_unbind(&v.vm,0x600000,4096));
		mutex_unlock(&lock);
		assert(v.vm.bindings[0].bo->refs==2 && !v.vm.active_uses && !v.vm.uploaded);
		store.profile=profile;
	}

	{
		struct mt_ce3_stream_input stream={.copy={.src=0x200400,.dst=0x201000,.bytes=256,.version=3},.command_va=0x200000};
		struct mt_ce3_stream_image image, saved;
		u64 field;
		unsigned refs=obj->refs;
		assert(!store.ops->prepare_copy_stream(&store,&drm,&file,handle,handle,handle,&v,&stream,&image,sizeof(image)));
		memcpy(&field,image.commands,8);assert(field==(0x4000000000000000ULL|0x200038));
		memcpy(&field,image.commands+8,8);assert(field==80);
		memcpy(&field,image.record+8,8);assert(field==0x2000a0);
		assert(obj->refs==refs && !v.vm.active_uses && !v.vm.uploaded);
		saved=image;
		store.profile=local_profile;
		assert(store.ops->prepare_copy_stream(&store,&drm,&file,handle,handle,handle,&v,&stream,&image,sizeof(image))==-EOPNOTSUPP);
		store.profile=profile;
		assert(store.ops->prepare_copy_stream(&store,&drm,&file,handle,handle,0,&v,&stream,&image,sizeof(image))==-ENOENT);
		assert(store.ops->prepare_copy_stream(&store,&drm,&file,handle,0,handle,&v,&stream,&image,sizeof(image))==-ENOENT);
		assert(store.ops->prepare_copy_stream(&store,&drm,&other_file,handle,handle,handle,&v,&stream,&image,sizeof(image))==-ENOENT);
		assert(store.ops->prepare_copy_stream(&other_store,&drm,&file,handle,handle,handle,&v,&stream,&image,sizeof(image))==-EXDEV);
		stream.copy.src=0x200040; /* Would overwrite/read command bytes. */
		assert(store.ops->prepare_copy_stream(&store,&drm,&file,handle,handle,handle,&v,&stream,&image,sizeof(image))==-EINVAL);
		stream.copy.src=0x200400;stream.copy.dst=0x200080;
		assert(store.ops->prepare_copy_stream(&store,&drm,&file,handle,handle,handle,&v,&stream,&image,sizeof(image))==-EINVAL);
		stream.copy.dst=0x201000;stream.command_va=0x200001;
		assert(store.ops->prepare_copy_stream(&store,&drm,&file,handle,handle,handle,&v,&stream,&image,sizeof(image))==-EINVAL);
		stream.command_va=0x200fe0;
		assert(store.ops->prepare_copy_stream(&store,&drm,&file,handle,handle,handle,&v,&stream,&image,sizeof(image))==-EINVAL);
		stream.command_va=0x200000;stream.copy.version=2;
		assert(store.ops->prepare_copy_stream(&store,&drm,&file,handle,handle,handle,&v,&stream,&image,sizeof(image))==-EINVAL);
		stream.copy.version=3;
		assert(store.ops->prepare_copy_stream(&store,&drm,&file,handle,handle,handle,&v,&stream,&image,sizeof(image)-1)==-EINVAL);
		assert(!memcmp(&image,&saved,sizeof(image)) && obj->refs==refs);
	}
	{
		struct mt_ce3_paging_input req = {
			.stream = {.copy={.src=0x200400,.dst=0x201000,.bytes=256,.version=3},.command_va=0x200000},
			.descriptor_va=0x200100, .state_va=0x200300};
		struct { struct mt_ce3_paging_image image; u8 guard[16]; } out, saved;
		u64 field;
		u32 value;
		unsigned refs = obj->refs;
		const u64 bad_descriptor[] = {0x200000, 0x200300, 0x200400, 0x201000};
		const u64 bad_state[] = {0x200000, 0x200100, 0x200400, 0x201000};
		memset(&out, 0xa5, sizeof(out));
		assert(!store.ops->prepare_copy_paging(&store,&drm,&file,handle,handle,handle,&v,&req,&out,sizeof(out)));
		memcpy(&field,out.image.paging.descriptor+0x138,8); assert(field==req.stream.command_va);
		memcpy(&field,out.image.paging.descriptor+0x130,8); assert(field==req.state_va);
		memcpy(&field,out.image.paging.state,8); assert(field==24);
		memcpy(&value,out.image.paging.descriptor+0x1c,4); assert(value==0x69);
		for (i=0;i<16;i++) assert(out.guard[i]==0xa5);
		saved=out;
		store.profile=local_profile;
		assert(store.ops->prepare_copy_paging(&store,&drm,&file,handle,handle,handle,&v,&req,&out,sizeof(out))==-EOPNOTSUPP);
		store.profile=profile;
		for (i=0;i<4;i++) {
			req.descriptor_va=bad_descriptor[i];
			assert(store.ops->prepare_copy_paging(&store,&drm,&file,handle,handle,handle,&v,&req,&out,sizeof(out))==-EINVAL);
		}
		req.descriptor_va=0x200100;
		for (i=0;i<4;i++) {
			req.state_va=bad_state[i];
			assert(store.ops->prepare_copy_paging(&store,&drm,&file,handle,handle,handle,&v,&req,&out,sizeof(out))==-EINVAL);
		}
		req.state_va=0x201fe0;
		assert(store.ops->prepare_copy_paging(&store,&drm,&file,handle,handle,handle,&v,&req,&out,sizeof(out))==-ENOENT);
		req.state_va=0x200300; req.descriptor_va=0x201f00;
		assert(store.ops->prepare_copy_paging(&store,&drm,&file,handle,handle,handle,&v,&req,&out,sizeof(out))==-ENOENT);
		req.descriptor_va=0x200101;
		assert(store.ops->prepare_copy_paging(&store,&drm,&file,handle,handle,handle,&v,&req,&out,sizeof(out))==-EINVAL);
		req.descriptor_va=0x200100; req.state_va=0x200301;
		assert(store.ops->prepare_copy_paging(&store,&drm,&file,handle,handle,handle,&v,&req,&out,sizeof(out))==-EINVAL);
		req.state_va=1ULL<<40;
		assert(store.ops->prepare_copy_paging(&store,&drm,&file,handle,handle,handle,&v,&req,&out,sizeof(out))==-ERANGE);
		req.state_va=0x200300;
		assert(!mt_gem_bind_handle(&store,&drm,&file,handle,&v,0x600000,0,4096,0));
		req.descriptor_va=0x600000; /* VA differs, but aliases the command stream. */
		assert(store.ops->prepare_copy_paging(&store,&drm,&file,handle,handle,handle,&v,&req,&out,sizeof(out))==-EINVAL);
		req.descriptor_va=0x200100; req.state_va=0x600100; /* Aliases descriptor. */
		assert(store.ops->prepare_copy_paging(&store,&drm,&file,handle,handle,handle,&v,&req,&out,sizeof(out))==-EINVAL);
		req.state_va=0x200300;
		mutex_lock(&lock);
		assert(!mt_gpu_vm_unbind(&v.vm,0x600000,4096));
		mutex_unlock(&lock);
		assert(store.ops->prepare_copy_paging(&store,&drm,&file,handle,handle,handle,&v,&req,&out,sizeof(out.image)-1)==-EINVAL);
		assert(store.ops->prepare_copy_paging(&store,&drm,&file,handle,handle,handle,&v,NULL,&out,sizeof(out))==-EINVAL);
		assert(store.ops->prepare_copy_paging(&store,&drm,&file,handle,handle,0,&v,&req,&out,sizeof(out))==-ENOENT);
		assert(store.ops->prepare_copy_paging(&store,&drm,&file,handle,0,handle,&v,&req,&out,sizeof(out))==-ENOENT);
		assert(store.ops->prepare_copy_paging(&store,&drm,&other_file,handle,handle,handle,&v,&req,&out,sizeof(out))==-ENOENT);
		assert(store.ops->prepare_copy_paging(&other_store,&drm,&file,handle,handle,handle,&v,&req,&out,sizeof(out))==-EXDEV);
		assert(!memcmp(&out,&saved,sizeof(out)) && obj->refs==refs);
		assert(!v.vm.active_uses && !v.vm.uploaded && !v.vm.bindings[0].bo->gpu_users);
	}
	held = v.vm.bindings[0].bo;
	before = heap_frees;
	close_handle(&file, handle);
	assert(!store.objects && drm.refs == 1 && held->refs == 1);
	assert(heap_frees == before + 1); /* wrapper gone, BO container remains */
	mutex_lock(&lock);
	assert(!mt_bo_cpu_begin(held, &mapping));
	for (i = 0; i < 8192; i++) assert(!((u8 *)mapping)[i]);
	assert(!mt_gpu_vm_unbind(&v.vm, 0x200000, 8192));
	assert(held->refs == 1 && heap_frees == before + 1);
	assert(!mt_bo_cpu_end(held)); /* final CPU reference destroys BO metadata */
	assert(heap_frees == before + 2);
	mutex_unlock(&lock);
	assert(!mt_gem_create_handle(&store, &drm, &file, 4096, &handle));
	file.close_after_lookup = true;
	assert(!mt_gem_bind_handle(&store, &drm, &file, handle, &v, 0x400000, 0, 4096, 0));
	assert(!file.handles[handle] && !store.objects && v.vm.count == 1);
	held = v.vm.bindings[0].bo;
	mutex_lock(&lock);
	assert(!mt_bo_gpu_begin(held));
	assert(!mt_gpu_vm_fini(&v.vm)); /* GPU owns BO after wrapper and VM die */
	assert(held->refs == 1 && held->gpu_users == 1);
	assert(!mt_bo_gpu_end(held));
	mutex_unlock(&lock);
	assert(!store.objects && drm.refs == 1 && !lock.held);
	assert(heap_allocs == heap_frees && buffers.allocated == buffers.freed);
	tqx_heap_bridge(&store,&drm,&file,&vm_store);
	tqx_submission_bridge(&store,&drm,&file,&vm_store);
	assert(heap_allocs == heap_frees && buffers.allocated == buffers.freed);
	assert(gem_inits == gem_releases);
	puts("PASS: GEM bridge lifetime, file isolation, VM-backed work/copy/stream/paging staging, alias overlap rejection, unchanged failed outputs and balanced DRM references (modeled core)");
	return 0;
}
