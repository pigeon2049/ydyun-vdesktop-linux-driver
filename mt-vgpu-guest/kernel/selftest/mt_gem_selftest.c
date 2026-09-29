// SPDX-License-Identifier: GPL-2.0
/* Real Linux GEM reference callbacks over RAM. No PCI device, BAR access,
 * DRM registration, userspace node, firmware publication or GPU work. */
#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <drm/drm_gem.h>
#include <drm/drm_ioctl.h>
#include "../mt_gem.h"
#include "../mt_tqx_copy_stream.h"

static int result = -ENODATA;
module_param(result, int, 0444);
static unsigned int checks;
module_param(checks, uint, 0444);
static unsigned int failure_line;
module_param(failure_line, uint, 0444);

struct ram_store {
	struct mt_bo_store buffers;
	u64 next_pa;
	u32 allocated, freed;
};
static int ram_alloc(void *opaque, u32 bytes, u32 alignment, struct mt_bo_backing *b)
{
	struct mt_bo_store *s = opaque;
	struct ram_store *r = container_of(s, struct ram_store, buffers);
	void *p;
	lockdep_assert_held(s->lock);
	p = kvzalloc(bytes, GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	r->next_pa = ALIGN(r->next_pa, (u64)alignment);
	*b = (struct mt_bo_backing){.handle = p, .gpu_pa = r->next_pa,
		.bar_offset = r->next_pa, .bytes = bytes};
	r->next_pa += bytes;
	r->allocated++;
	s->objects++;
	s->allocated_bytes += bytes;
	return 0;
}
static int ram_clear(void *opaque, const struct mt_bo_backing *b)
{
	struct mt_bo_store *s = opaque;
	lockdep_assert_held(s->lock);
	memset(b->handle, 0, b->bytes);
	return 0;
}
static void ram_free(void *opaque, const struct mt_bo_backing *b)
{
	struct mt_bo_store *s = opaque;
	struct ram_store *r = container_of(s, struct ram_store, buffers);
	lockdep_assert_held(s->lock);
	r->freed++;
	s->objects--;
	s->allocated_bytes -= b->bytes;
	kvfree(b->handle);
}
static int ram_map(void *opaque, const struct mt_bo_backing *b, void **out)
{
	struct mt_bo_store *s = opaque;
	lockdep_assert_held(s->lock);
	*out = b->handle;
	return 0;
}
static void ram_unmap(void *opaque, const struct mt_bo_backing *b)
{
	struct mt_bo_store *s = opaque;
	lockdep_assert_held(s->lock);
}
static const struct mt_bo_ops ram_ops = {
	.alloc = ram_alloc, .clear = ram_clear, .free = ram_free,
	.map = ram_map, .unmap = ram_unmap,
};
static unsigned int upload_writes, upload_reads;
static bool upload_corrupt;
static int upload_write(struct mt_bo *bo, u64 offset, const void *src, u64 bytes)
{
 void *mapping;
 int ret=mt_bo_check_range(bo,offset,bytes);
 if(ret)return ret;
 ret=mt_bo_cpu_begin(bo,&mapping);
 if(ret)return ret;
 memcpy((u8 *)mapping+offset,src,bytes);
 upload_writes++;
 return mt_bo_cpu_end(bo);
}
static int upload_read(struct mt_bo *bo, u64 offset, void *dst, u64 bytes)
{
 void *mapping;
 int ret=mt_bo_check_range(bo,offset,bytes);
 if(ret)return ret;
 ret=mt_bo_cpu_begin(bo,&mapping);
 if(ret)return ret;
 memcpy(dst,(u8 *)mapping+offset,bytes);
 if(upload_corrupt)((u8 *)dst)[bytes-1]^=1;
 upload_reads++;
 return mt_bo_cpu_end(bo);
}
static const struct mt_tqx_upload_ops upload_ops={upload_write,upload_read};
DEFINE_DRM_GEM_FOPS(test_fops);
static const struct drm_driver test_driver = {
	.driver_features = DRIVER_GEM, .fops = &test_fops,
	.name = "mt-guest-gem-selftest", .desc = "RAM GEM lifecycle selftest",
	.major = 0, .minor = 1,
};

/* CHECK jumps to cleanup without panicking a live guest. All allocations
 * remain unpublished and can always be released after a failed assertion. */
#define CHECK(expr) do { \
	if (!(expr)) { if (!failure_line) failure_line = __LINE__; result = -EINVAL; goto done; } \
	checks++; \
} while (0)

/* Upload using live pool slices and the shared static-PDS BO. */
static int test_tqx_pool_upload(struct mt_gem_store *gem, struct mt_vm_vram *v,
 struct mt_execution_context *context, struct mt_bo **shared, struct mt_bo **original,
 const struct mt_tqx_heap_input *base, struct mt_tqx_submission_workspace *workspace,
 struct mt_tqx_work *work)
{
 struct mt_pool_state states[3]={0};struct mt_pool_slice slices[3]={0},prefixes[3]={0};
 struct mt_guest_pool_spec specs[3];struct mt_tqx_submission_input request={.stream=*base};
 struct mt_bo *bo[MT_TQX_SUBMISSION_BUFFERS];u32 i;int ret=-EINVAL;
 mt_guest_plan_pools(specs);memcpy(bo,original,sizeof(bo));
 for(i=0;i<3;i++)CHECK(!mt_pool_slice_alloc(&states[i],context,shared[6+i],&specs[i],4096,&prefixes[i]));
 CHECK(!mt_pool_slice_alloc(&states[1],context,shared[7],&specs[1],20480,&slices[0]));
 CHECK(!mt_pool_slice_alloc(&states[0],context,shared[6],&specs[0],4096,&slices[1]));
 CHECK(!mt_pool_slice_alloc(&states[2],context,shared[8],&specs[2],4096,&slices[2]));
 request.stream.va[1]=slices[0].va;request.stream.va[2]=0x81ffc00000ULL;
 request.stream.va[3]=slices[1].va;request.stream.va[4]=slices[2].va;
 request.dma_va=0x40010000;request.state_va=0x40020000;
 bo[1]=slices[0].bo;bo[2]=shared[MT_SHARED_PDS];bo[3]=slices[1].bo;bo[4]=slices[2].bo;
 CHECK(!mt_tqx_work_prepare(work,workspace,&upload_ops,&gem->profile,gem->tqx_cores,context,bo,&request));
 CHECK(context->pool_slices==6 && work->job.count==19);
 CHECK(!memcmp((u8 *)bo[1]->backing.handle+slices[0].offset,mt_tqx_program_bytes,MT_TQX_SHADER_BANK_BYTES));
 CHECK(!memcmp(bo[2]->backing.handle,mt_tqx_program_bytes+MT_TQX_SHADER_BANK_BYTES,176));
 for(i=0;i<3;i++)CHECK(mt_pool_slice_free(&slices[i])==-EBUSY);
 CHECK(mt_execution_context_destroy(context)==-EBUSY);
 CHECK(!mt_tqx_work_cancel(work));
 for(i=0;i<3;i++)CHECK(!mt_pool_slice_free(&slices[i]));
 for(i=0;i<3;i++)CHECK(!mt_pool_slice_free(&prefixes[i]));
 CHECK(!context->pool_slices && !v->vm.active_uses);
 ret=0;
done:
 if(work->context)mt_tqx_work_cancel(work);
 for(i=0;i<3;i++)if(slices[i].state)mt_pool_slice_free(&slices[i]);
 for(i=0;i<3;i++)if(prefixes[i].state)mt_pool_slice_free(&prefixes[i]);
 return ret;
}

static int test_tqx_heaps(struct mt_gem_store *gem, struct drm_device *drm,
        struct mt_vm_store *vms, struct mutex *lock)
{
 struct drm_gem_object *obj[MT_TQX_SUBMISSION_BUFFERS]={0};
 struct drm_gem_object *shared_obj[MT_BOOT_BO_COUNT]={0};
 struct mt_bo *shared[MT_BOOT_BO_COUNT];
 const u32 shared_bytes[]={0x200000,0x100000,0x80000,0x80000,4096,0x400000,0x200000,0x100000,0x200000};
 struct mt_tqx_work *work=NULL;
 struct mt_execution_store execution;
 struct mt_execution_process process={0};
 struct mt_execution_context context={0};
 const struct mt_bo *bo[MT_TQX_SUBMISSION_BUFFERS];
 struct mt_bo *writable[MT_TQX_SUBMISSION_BUFFERS];
 struct mt_tqx_submission_workspace *submission=NULL;
 struct mt_tqx_submission_result submitted;
 struct mt_tqx_submission_input submit_req;
 struct mt_tqx_upload_workspace *workspace=NULL;
 struct mt_tqx_upload_result uploaded;
 struct mt_tqx_dma_image *dma=NULL;
 struct mt_tqx_dma_input dma_input={0x40010000,0x40020000,1};
 struct mt_vm_vram *v=NULL;
 struct mt_tqx_copy_stream_image *out=NULL,*saved=NULL;
 struct mt_tqx_heap_input req={.copy={0x40100001,0x40200000,8191},
  .va={0x40000000,0x8400000000ULL,0x8100000000ULL,0x8100001000ULL,0xf000000000ULL}};
 u32 i,bytes;u64 va;
 bool locked=false;
 int ret=-EINVAL;
 out=kzalloc(sizeof(*out),GFP_KERNEL);saved=kzalloc(sizeof(*saved),GFP_KERNEL);
 workspace=kzalloc(sizeof(*workspace),GFP_KERNEL);
 dma=kzalloc(sizeof(*dma),GFP_KERNEL);
 submission=kzalloc(sizeof(*submission),GFP_KERNEL);
 work=kzalloc(sizeof(*work),GFP_KERNEL);
 CHECK(out && saved && workspace && dma && submission && work);
 mt_fw_put32(workspace->write_page,0,0xaa557491);
 mt_fw_put32(workspace->write_page,4,2);
 mt_fw_put32(workspace->write_page,0xc9c,1);
 CHECK(!mt_gem_configure_tqx_info(gem,workspace->write_page,4096));
 CHECK(gem->tqx_cores==1);
 CHECK(!mt_gem_configure_tqx_info(gem,workspace->write_page,4096));
 mt_fw_put32(workspace->write_page,0xc9c,2);
 CHECK(mt_gem_configure_tqx_info(gem,workspace->write_page,4096)==-EBUSY);
 CHECK(gem->tqx_cores==1);
 for(i=0;i<MT_TQX_SUBMISSION_BUFFERS;i++) {
  CHECK(!gem->ops->create(gem,drm,i<5?mt_tqx_buffer_bytes[i]:(i==8?4096:8192),&obj[i]));
  bo[i]=container_of(obj[i],struct mt_gem_object,base)->bo;
  writable[i]=(struct mt_bo *)bo[i];
 }
 for(i=0;i<MT_BOOT_BO_COUNT;i++) {
  CHECK(!gem->ops->create(gem,drm,shared_bytes[i],&shared_obj[i]));
  shared[i]=container_of(shared_obj[i],struct mt_gem_object,base)->bo;
 }
 mutex_lock(lock);locked=true;
 CHECK(!vms->ops->create(vms,32,&v));
 for(i=0;i<MT_TQX_SUBMISSION_BUFFERS;i++) {
  va=i<5?req.va[i]:(i==MT_TQX_SOURCE?req.copy.src-1:
   i==MT_TQX_DESTINATION?req.copy.dst:i==MT_TQX_DMA?0x40010000:0x40020000);
  bytes=i<5?mt_tqx_buffer_bytes[i]:(i==8?4096:8192);
  CHECK(!vms->ops->bind(v,(struct mt_bo *)bo[i],va,0,bytes,MT_GPU_MAP_DEFAULT));
 }
 /* Eighteen mappings require 21 table pages, including eight PD pages. */
 v->vm.capacity=20*4096;
 CHECK(mt_process_resources_bind_pools(&v->vm,&gem->profile,shared,shared+MT_PROCESS_SHARED_COUNT)==-ENOSPC && v->vm.count==9);
 v->vm.capacity=32*4096;
 CHECK(!mt_process_resources_bind_pools(&v->vm,&gem->profile,shared,shared+MT_PROCESS_SHARED_COUNT));
 CHECK(v->vm.used_pages==21);
 CHECK(v->vm.count==18);
 CHECK(mt_process_resources_bind_pools(&v->vm,&gem->profile,shared,shared+MT_PROCESS_SHARED_COUNT)==-ENOSPC);
 for(i=0;i<MT_BOOT_BO_COUNT;i++)CHECK(shared[i]->refs==2);
 CHECK(!mt_tqx_heap_stream_prepare(out,sizeof(*out),&gem->profile,&v->vm,bo,&req));
 CHECK(out->count==3 && out->command_bytes==240 && out->root_export[0]==1);
 *saved=*out;
 CHECK(!mt_tqx_upload(&uploaded,sizeof(uploaded),workspace,&upload_ops,&gem->profile,&v->vm,writable,&req));
 CHECK(upload_writes==9 && upload_reads==9);
 CHECK(!memcmp(uploaded.record,out->record,296) && !memcmp(uploaded.root_export,out->root_export,16));
 CHECK(!memcmp(bo[1]->backing.handle,mt_tqx_program_bytes,MT_TQX_SHADER_BANK_BYTES));
 CHECK(!memcmp(bo[2]->backing.handle,mt_tqx_program_bytes+MT_TQX_SHADER_BANK_BYTES,176));
 mutex_unlock(lock);locked=false;
 dma_input.cores=0;
 CHECK(!gem->ops->encode_tqx_dma(gem,&uploaded,&dma_input,dma,sizeof(*dma)));
 CHECK(dma->bytes==0x1300 && dma->descriptor[0x1c]==0x67);
 memcpy(&va,dma->descriptor+0x168,8);
 CHECK(va==dma_input.dma_va+0x200);
 dma_input.cores=2;
 CHECK(gem->ops->encode_tqx_dma(gem,&uploaded,&dma_input,dma,sizeof(*dma))==-EINVAL);
 CHECK(dma->bytes==0x1300);
 dma_input.cores=0;
 dma_input.state_va=dma_input.dma_va;
 CHECK(gem->ops->encode_tqx_dma(gem,&uploaded,&dma_input,dma,sizeof(*dma))==-EINVAL);
 CHECK(dma->bytes==0x1300);
 mutex_lock(lock);locked=true;
 upload_corrupt=true;
 CHECK(mt_tqx_upload(&uploaded,sizeof(uploaded),workspace,&upload_ops,&gem->profile,&v->vm,writable,&req)==-EIO);
 upload_corrupt=false;
 CHECK(!memcmp(uploaded.record,out->record,296) && !v->vm.uploaded && !v->vm.active_uses);
 for(i=0;i<MT_TQX_SUBMISSION_BUFFERS;i++)CHECK(bo[i]->refs==2 && !bo[i]->cpu_users && !bo[i]->gpu_users);
 submit_req=(struct mt_tqx_submission_input){req,0x40010000,0x40020000};
 memset(bo[MT_TQX_ENGINE_STATE]->backing.handle,0x5a,4096);
 upload_writes=upload_reads=0;
 CHECK(!mt_tqx_submission_upload(&submitted,sizeof(submitted),submission,&upload_ops,
  &gem->profile,gem->tqx_cores,&v->vm,writable,&submit_req));
 CHECK(upload_writes==11 && upload_reads==11);
 memcpy(&va,submitted.view,8);CHECK(va==submit_req.dma_va);
 memcpy(&va,submitted.view+8,8);CHECK(va==4864);
 CHECK(!memcmp(bo[MT_TQX_DMA]->backing.handle,submission->dma.descriptor,8192));
 CHECK(!memchr_inv(bo[MT_TQX_ENGINE_STATE]->backing.handle,0x5a,4096));
 mt_execution_store_init(&execution,gem->buffers,&gem->profile);
 CHECK(!mt_execution_process_create(&execution,&process,&v->vm,1234));
 CHECK(!mt_execution_context_create(&context,&process,1,0));
 CHECK(!mt_tqx_work_prepare(work,submission,&upload_ops,&gem->profile,gem->tqx_cores,
  &context,writable,&submit_req));
 CHECK(work->job.count==19 && v->vm.active_uses==1 && context.active_jobs==1);
 for(i=0;i<MT_BOOT_BO_COUNT;i++)CHECK(shared[i]->refs==3 && shared[i]->gpu_users==1);
 CHECK(!mt_tqx_work_cancel(work));
 CHECK(!test_tqx_pool_upload(gem,v,&context,shared,writable,&req,submission,work));
 CHECK(!mt_execution_context_destroy(&context));
 CHECK(!mt_execution_process_destroy(&process));

 CHECK(!mt_gpu_vm_unbind(&v->vm,req.va[3],4096));
 CHECK(!vms->ops->bind(v,(struct mt_bo *)bo[0],req.va[3],0,4096,3));
 bo[3]=bo[0];
 CHECK(mt_tqx_heap_stream_prepare(out,sizeof(*out),&gem->profile,&v->vm,bo,&req)==-EINVAL);
 CHECK(!memcmp(out,saved,sizeof(*saved)) && !v->vm.uploaded && !v->vm.active_uses);
 mutex_unlock(lock);locked=false;
 for(i=0;i<MT_TQX_SUBMISSION_BUFFERS;i++) { drm_gem_object_put(obj[i]);obj[i]=NULL; }
 for(i=0;i<MT_BOOT_BO_COUNT;i++) { drm_gem_object_put(shared_obj[i]);shared_obj[i]=NULL; }
 CHECK(!gem->objects);
 ret=0;
done:
 if(locked)mutex_unlock(lock);
 for(i=0;i<MT_TQX_SUBMISSION_BUFFERS;i++)if(obj[i])drm_gem_object_put(obj[i]);
 for(i=0;i<MT_BOOT_BO_COUNT;i++)if(shared_obj[i])drm_gem_object_put(shared_obj[i]);
 mutex_lock(lock);
 if(work && work->context)mt_tqx_work_cancel(work);
 if(context.process)mt_execution_context_destroy(&context);
 if(process.store)mt_execution_process_destroy(&process);
 if(v)vms->ops->destroy(v);
 mutex_unlock(lock);
 kfree(work);kfree(submission);kfree(dma);kfree(workspace);kfree(saved);kfree(out);
 return ret;
}

/* Independent walk of a test VM, including table-allocation bounds. */
static u64 test_vm_page(const struct mt_gpu_vm *vm, u64 va)
{
 u64 root=vm->tables->backing.gpu_pa,offset,pd,pte;u32 pc;
 memcpy(&pc,(u8 *)vm->image+((va>>30)&1023)*4,4);
 offset=((u64)(pc&0xfffffff0U)<<8)-root;
 if(!(pc&1)||offset>vm->capacity-4096)return 0;
 memcpy(&pd,(u8 *)vm->image+offset+((va>>21)&511)*8,8);
 offset=(pd&0xfffffff000ULL)-root;
 if(!(pd&1)||offset>vm->capacity-4096)return 0;
 memcpy(&pte,(u8 *)vm->image+offset+((va>>12)&511)*8,8);
 return (pte&7)==1 && !(pte&(1ULL<<62)) ? pte&0xfffffff000ULL : 0;
}

struct boot_fixture {
 struct mt_vram vram;
 struct mt_boot_resources boot;
 struct mt_vram_block table_blocks[2];
 struct mt_system_memory paging_command;
 struct mt_reserved_pools pools;
 struct mt_vram_block *blocks[5];
 struct mt_bo tables[2];
 struct mt_bo_store buffers;
 struct mt_boot_bo_store shared;
 struct mt_gpu_vm vm[2];
 void *image[2], *scratch[2];
};
static u32 boot_allocated, boot_freed;

/* Exercise actual VRAM borrowed handles using ordinary RAM list members.
 * No pci_iomap, gen_pool allocation, boot teardown or device access occurs. */
static int test_boot_views(struct mutex *lock, const struct mt_device_profile *profile)
{
 const u32 indices[]={MT_BOOT_PB,MT_BOOT_PDS,MT_BOOT_YUV,MT_BOOT_KILL,MT_BOOT_FENCE,0};
 const u32 sizes[]={0x200000,0x100000,0x80000,0x80000,4096,0x400000};
 struct boot_fixture *f=kzalloc(sizeof(*f),GFP_KERNEL);
 struct mt_bo *held=NULL;
 u32 i;u8 bytes[32];bool locked=false;int ret=-EINVAL;
 CHECK(f);
 INIT_LIST_HEAD(&f->vram.blocks);f->vram.region_owned=true;f->boot.prepared=true;f->pools.prepared=true;
 mt_bo_store_init(&f->buffers,&f->vram,lock);
 mutex_lock(lock);locked=true;
 for(i=0;i<5;i++)f->blocks[i]=&f->boot.blocks[indices[i]];
 for(i=0;i<11;i++) {
  struct mt_vram_block *block=i<5?f->blocks[i]:i<7?&f->table_blocks[i-5]:i==7?&f->boot.blocks[MT_BOOT_PAGING_CONTEXT]:&f->pools.blocks[i-8];
  u32 size=i<5?sizes[i]:i<7?65536:i==7?8192:i==9?0x100000:0x200000;
  void *ram=kvzalloc(size,GFP_KERNEL);
  CHECK(ram);
  boot_allocated++;
  block->mapping=(void __iomem *)ram;block->size=size;
  block->gpu_pa=0x650000000ULL+i*0x400000ULL;block->bar_offset=i*0x400000ULL;
  list_add_tail(&block->link,&f->vram.blocks);memset(ram,0x5a,block->size);
 }
 {
  const struct mt_system_address address={.bar2=0x800000000ULL,.bar2_bytes=0x400000000ULL,.bias=0x8800000000ULL};
  CHECK(!mt_system_memory_prepare(&f->paging_command,MT_PAGING_COMMAND_BYTES,&address));
  boot_allocated++;
  CHECK(!memchr_inv(f->paging_command.cpu,0,MT_PAGING_COMMAND_BYTES));
  memset(f->paging_command.cpu,0x5a,MT_PAGING_COMMAND_BYTES);
  f->paging_command.bytes=8192;
  CHECK(mt_boot_bo_init(&f->shared,&f->buffers,&f->boot,&f->paging_command,&f->pools)==-EINVAL);
  f->paging_command.bytes=MT_PAGING_COMMAND_BYTES;
 }
 CHECK(!mt_boot_bo_init(&f->shared,&f->buffers,&f->boot,&f->paging_command,&f->pools));
 CHECK(!f->buffers.objects && !mt_boot_bo_can_release(&f->shared));
 for(i=0;i<2;i++) {
  f->image[i]=kvzalloc(65536,GFP_KERNEL);f->scratch[i]=kvzalloc(65536,GFP_KERNEL);
  CHECK(f->image[i] && f->scratch[i]);
  CHECK(!mt_bo_vram_borrow(&f->tables[i],&f->buffers,&f->table_blocks[i]));
  CHECK(!mt_gpu_vm_init(&f->vm[i],&f->tables[i],f->image[i],f->scratch[i],65536));
  CHECK(!mt_bo_put(&f->tables[i]));
 }
 CHECK(!mt_boot_bo_bind(&f->shared,&f->vm[0],profile));
 CHECK(!mt_boot_bo_bind(&f->shared,&f->vm[1],profile));
 CHECK(f->buffers.objects==11 && mt_boot_bo_can_release(&f->shared)==-EBUSY);
 for(i=0;i<MT_BOOT_BO_COUNT;i++) {
  struct mt_bo *bo=f->shared.slots[i];
  CHECK(bo && bo->refs==2 && f->vm[0].bindings[i].bo==f->vm[1].bindings[i].bo);
  CHECK(!mt_bo_vram_read(bo,0,bytes,sizeof(bytes)) && !memchr_inv(bytes,0x5a,sizeof(bytes)));
  CHECK(bo->ops->clear(&f->buffers,&bo->backing)==-EPERM);
 }
 {
  struct mt_bo *ram=f->shared.slots[MT_SHARED_PAGING_COMMAND];
  bool correct=true;
  CHECK(ram->page_pa==f->paging_command.page_pa);
  for(i=0;i<1024;i++)
   if(test_vm_page(&f->vm[0],0x81ff800000ULL+i*4096)!=f->paging_command.page_pa[i])correct=false;
  CHECK(correct);
  memset(bytes,0xa7,sizeof(bytes));
  CHECK(!mt_bo_vram_write(ram,4090,bytes,sizeof(bytes)));
  memset(bytes,0,sizeof(bytes));
  CHECK(!mt_bo_vram_read(ram,4090,bytes,sizeof(bytes)));
  CHECK(!memchr_inv(bytes,0xa7,sizeof(bytes)));
  memset(bytes,0x5a,sizeof(bytes));
  CHECK(!mt_bo_vram_write(ram,4090,bytes,sizeof(bytes)));
 }
 {
  struct mt_guest_pool_spec specs[MT_GUEST_POOL_COUNT];u32 j;bool correct=true;
  mt_guest_plan_pools(specs);
  for(i=0;i<MT_GUEST_POOL_COUNT;i++)for(j=0;j<specs[i].bytes;j+=4096)
   if(test_vm_page(&f->vm[0],specs[i].va+j)!=f->pools.blocks[i].gpu_pa+j)correct=false;
  CHECK(correct);
 }
	CHECK(!mt_boot_bo_bind(&f->shared,&f->vm[1],profile)); /* Existing full mapping is idempotent. */
 CHECK(!mt_gpu_vm_fini(&f->vm[0]));
 held=f->shared.slots[MT_PROCESS_SHARED_COUNT+1];
 CHECK(!mt_bo_gpu_begin(held));
 CHECK(mt_bo_vram_read(held,0,bytes,sizeof(bytes))==-EBUSY);
 CHECK(mt_bo_vram_write(held,0,bytes,sizeof(bytes))==-EBUSY);
 CHECK(!mt_gpu_vm_fini(&f->vm[1]));
 CHECK(f->buffers.objects==1 && mt_boot_bo_can_release(&f->shared)==-EBUSY);
 CHECK(!mt_bo_gpu_end(held));held=NULL;
 CHECK(!f->buffers.objects && !f->buffers.allocated_bytes && !mt_boot_bo_can_release(&f->shared));
 for(i=0;i<5;i++)CHECK(!memchr_inv((void *)f->blocks[i]->mapping,0x5a,sizes[i]));
 CHECK(!memchr_inv(f->paging_command.cpu,0x5a,MT_PAGING_COMMAND_BYTES));
 CHECK(!memchr_inv((void *)f->boot.blocks[MT_BOOT_PAGING_CONTEXT].mapping,0x5a,8192));
 ret=0;
done:
 if(f) {
  if(!locked){mutex_lock(lock);locked=true;}
  if(held && held->gpu_users)mt_bo_gpu_end(held);
  for(i=0;i<2;i++) {
   if(f->vm[i].tables)mt_gpu_vm_fini(&f->vm[i]);
   if(f->tables[i].refs)mt_bo_put(&f->tables[i]);
   kvfree(f->image[i]);kvfree(f->scratch[i]);
   if(f->table_blocks[i].mapping){kvfree((void *)f->table_blocks[i].mapping);boot_freed++;}
  }
  for(i=0;i<5;i++)if(f->blocks[i] && f->blocks[i]->mapping){kvfree((void *)f->blocks[i]->mapping);boot_freed++;}
  if(f->boot.blocks[MT_BOOT_PAGING_CONTEXT].mapping){kvfree((void *)f->boot.blocks[MT_BOOT_PAGING_CONTEXT].mapping);boot_freed++;}
  for(i=0;i<MT_GUEST_POOL_COUNT;i++)if(f->pools.blocks[i].mapping){kvfree((void *)f->pools.blocks[i].mapping);boot_freed++;}
  if(f->paging_command.cpu){mt_system_memory_fini(&f->paging_command);boot_freed++;}
  kfree(f);
 }
 if(locked)mutex_unlock(lock);
 return ret;
}

static int __init mt_gem_selftest_init(void)
{
	struct device *parent = NULL;
	struct drm_device *drm = NULL;
	struct drm_gem_object *obj = NULL;
	struct mt_gem_object *g;
	struct mt_vm_vram *v = NULL;
	struct mt_bo *held = NULL;
	struct mt_work_command_inputs request = {.root_pa = 0xbad000,
		.process_id = 0xf123456789abcdefULL, .command_va = 0x200000,
		.type = 5, .bytes = 8192, .fence = 1, .submit_flags = 0x80};
	struct mt_ce3_stream_image stream_image;
	struct mt_ce3_paging_image *paging_image = NULL;
	struct mt_tqx_copy_plan *tqx_plan = NULL;
	struct mt_tqx_copy_input tqx = {.src=0x200001, .dst=0x400000, .bytes=4095};
	void *tqx_programs = NULL;
	struct mt_tqx_job_image *tqx_job = NULL;
	struct mt_tqx_stream_image *tqx_stream = NULL;
	struct mt_tqx_copy_stream_image *copy_stream = NULL;
	struct mt_tqx_copy_stream_input copy_stream_input = {
		.copy={0x100001,0x200000000ULL,0xffffffffULL},.command_va=0x600000,
		.shader_heap_base=0x10000,.pds_code_heap_base=0x7000,.pds_initial_state=0x9000};
	u32 chunk_index;
	struct mt_tqx_stream_input stream_input;
	struct mt_tqx_job_input job_input = {.source_va=0x200001,.destination_va=0x400003,
		.constants_va=0x200800,.element_bytes=1,.width=4095,.height=1,
		.shader_heap_base=0x10000,.source_descriptor_index=0x1234,
		.pds_code_heap_base=0x7000,.pds_execution_state=0x9000,.pds_constant_state=0xc000};
	struct mt_tqx_program_state program_state;
	struct mt_tqx_program_input program_input = {.constants_va=0x200800,
		.operation=8, .shader_heap_base=0x10000, .source_descriptor_index=0x1234};
	struct mt_ce3_paging_input paging = {
		.stream = {.copy={.src=0x200400,.dst=0x400000,.bytes=256,.version=3},.command_va=0x200000},
		.descriptor_va=0x200100, .state_va=0x200300};
	struct mt_ce3_stream_input stream = {.copy={.src=0x200400, .dst=0x400000, .bytes=256, .version=3}, .command_va=0x200000};
	struct mt_ce_copy_input copy = {.src=0x200000, .dst=0x400000, .bytes=256, .version=3};
	u8 packet[80], saved[80];
	u64 field;
	struct mt_gem_store gem;
	struct mt_device_profile profile;
	struct mt_vm_store vms;
	struct ram_store ram = {.next_pa = 0x600000000ULL};
	struct mutex lock;
	bool locked = false, gpu = false;
	int ret;
	mutex_init(&lock);
	ram.buffers.lock = &lock;
	ram.buffers.ops = &ram_ops;
	mt_vm_store_init(&vms, &ram.buffers);
	parent = root_device_register("mt-guest-gem-selftest");
	if (IS_ERR(parent)) {
		result = PTR_ERR(parent);
		parent = NULL;
		goto done;
	}
	CHECK(!mt_device_profile_select(&profile, 0x1ed5, 0x222));
	mt_gem_store_init(&gem, &ram.buffers, parent, &profile);
	CHECK(mt_device_profile_ce(&gem.profile, 3) == -EOPNOTSUPP);
	CHECK(mt_device_profile_work(&gem.profile, 9) == -EOPNOTSUPP);
	CHECK(mt_device_profile_node(&gem.profile, 3) == -EOPNOTSUPP);
	tqx_programs = kvzalloc(MT_TQX_PROGRAM_BANK_BYTES, GFP_KERNEL);
	CHECK(tqx_programs);
	CHECK(!gem.ops->prepare_tqx_programs(&gem, tqx_programs, MT_TQX_PROGRAM_BANK_BYTES));
	CHECK(!memcmp(tqx_programs, mt_tqx_program_bytes, MT_TQX_PROGRAM_BANK_BYTES));
	CHECK(!mt_tqx_program_state_build(&program_state, sizeof(program_state), &program_input));
	memcpy(&field, program_state.pds_constants, 8);
	CHECK(field == program_input.constants_va && program_state.shader_offset == 0x1c0);
	program_input.shader_heap_base = 0xffffff80;
	CHECK(mt_tqx_program_state_build(&program_state, sizeof(program_state), &program_input) == -ERANGE);
	/* Standalone encoder in kernel RAM, no BO/heap ownership claim. */
	tqx_job = kzalloc(sizeof(*tqx_job), GFP_KERNEL);
	CHECK(tqx_job);
	CHECK(!mt_tqx_job_build(tqx_job, sizeof(*tqx_job), &job_input));
	CHECK(!memcmp(tqx_job->program.constants, program_state.constants, 20));
	memcpy(&field, tqx_job->destination + 52, 8);
	CHECK(field == 0x0000900000007050ULL && tqx_job->destination[72] == 0x25);
	job_input.pds_execution_state = 0xfffffff0;
	CHECK(mt_tqx_job_build(tqx_job, sizeof(*tqx_job), &job_input) == -ERANGE);
	memcpy(&field, tqx_job->destination + 52, 8);
	CHECK(field == 0x0000900000007050ULL);
	tqx_stream = kzalloc(sizeof(*tqx_stream), GFP_KERNEL);
	CHECK(tqx_stream);
	job_input.pds_execution_state = 0xa000;job_input.pds_constant_state = 0xd000;
	stream_input = (struct mt_tqx_stream_input){.job=job_input,
		.command_va=0x600000,.pds_initial_state=0x9000};
	CHECK(!mt_tqx_stream_build(tqx_stream, sizeof(*tqx_stream), &stream_input));
	CHECK(tqx_stream->job.destination[72] == 0x2d && tqx_stream->record[0x30] == 96 && tqx_stream->record[0xa0] == 1);
	memcpy(&field, tqx_stream->record + 0x34, 8);
	CHECK(field == 0x0000900000007020ULL && tqx_stream->record[0x124] == 80);
	memcpy(&field, tqx_stream->page_record, 8);
	CHECK(field == stream_input.command_va && tqx_stream->page_record[8] == 80);
	stream_input.command_va++;
	CHECK(mt_tqx_stream_build(tqx_stream, sizeof(*tqx_stream), &stream_input) == -EINVAL);
	copy_stream = kzalloc(sizeof(*copy_stream), GFP_KERNEL);
	CHECK(copy_stream);
	for (chunk_index=0;chunk_index<4;chunk_index++)
		copy_stream_input.addresses[chunk_index]=(struct mt_tqx_chunk_addresses){
			.constants_va=0x300000+chunk_index*4096,.source_descriptor_index=0x1234+chunk_index*256,
			.pds_execution_state=0xa000+chunk_index*16384,.pds_constant_state=0xd000+chunk_index*16384};
	CHECK(!mt_tqx_copy_stream_build(copy_stream, sizeof(*copy_stream), &copy_stream_input));
	CHECK(copy_stream->count==4 && copy_stream->command_bytes==320);
	CHECK(copy_stream->commands[72]==0x25 && copy_stream->commands[312]==0x2d);
	memcpy(&field, copy_stream->root_export+8, 8);
	CHECK(copy_stream->root_export[0]==1 && field==copy_stream_input.command_va);
	copy_stream_input.addresses[3].constants_va=1;
	CHECK(mt_tqx_copy_stream_build(copy_stream, sizeof(*copy_stream), &copy_stream_input)==-EINVAL);
	CHECK(copy_stream->count==4 && copy_stream->commands[312]==0x2d);
	drm = drm_dev_alloc(&test_driver, parent);
	if (IS_ERR(drm)) {
		result = PTR_ERR(drm);
		drm = NULL;
		goto done;
	}
	/* drm_dev_register is deliberately never called. */
	ret = gem.ops->create(&gem, drm, 4097, &obj);
	CHECK(!ret && obj && obj->size == 8192 && gem.objects == 1);
	g = container_of(obj, struct mt_gem_object, base);
	held = g->bo;
	CHECK(obj->funcs->mmap(obj, NULL) == -EOPNOTSUPP);
	CHECK(PTR_ERR(obj->funcs->export(obj, 0)) == -EOPNOTSUPP);
	drm_gem_object_get(obj);
	drm_gem_object_put(obj);
	CHECK(gem.objects == 1 && held->refs == 1);
	mutex_lock(&lock);
	locked = true;
	ret = vms.ops->create(&vms, 4, &v);
	CHECK(!ret && v && vms.objects == 1);
	CHECK(!vms.ops->bind(v, held, 0x200000, 0, 8192, 3));
	CHECK(!vms.ops->bind(v, held, 0x400000, 4096, 4096, 3));
	CHECK(held->refs == 3);
	CHECK(!mt_device_profile_transfer(&gem.profile));
	tqx_plan = kzalloc(sizeof(*tqx_plan), GFP_KERNEL);
	CHECK(tqx_plan);
	CHECK(!mt_tqx_copy_prepare(tqx_plan, sizeof(*tqx_plan), &v->vm, held, held, &tqx));
	CHECK(tqx_plan->count == 1 && tqx_plan->chunks[0].width == 4095 && tqx_plan->src == tqx.src);
	memcpy(&field, tqx_plan->source_descriptors[0]+8, 8);
	CHECK(field == (0x4000000000000000ULL | ((u64)4094 << 46) | tqx.src));
	memcpy(&field, tqx_plan->source_descriptors[0]+32, 8);
	CHECK(field == 0x8012400032000fffULL);
	memcpy(&field, tqx_plan->source_descriptors[0]+56, 8);
	CHECK(!field); /* Zero padding is Linux policy, not reference-written data. */
	tqx.src = 0x201000;
	CHECK(mt_tqx_copy_prepare(tqx_plan, sizeof(*tqx_plan), &v->vm, held, held, &tqx) == -EINVAL);
	CHECK(tqx_plan->src == 0x200001 && held->refs == 3 && !held->gpu_users);
	CHECK(!mt_work_command_prepare(packet, sizeof(packet), &v->vm, held, &request));
	memcpy(&field, packet + 0x18, sizeof(field));
	CHECK(field == v->tables.backing.gpu_pa && field != request.root_pa);
	memcpy(&field, packet + 0x20, sizeof(field));
	CHECK(field == request.process_id);
	memcpy(saved, packet, sizeof(packet));
	request.bytes++;
	CHECK(mt_work_command_prepare(packet, sizeof(packet), &v->vm, held, &request) == -ENOENT);
	CHECK(!memcmp(saved, packet, sizeof(packet)) && held->refs == 3 && !held->gpu_users);
	CHECK(!mt_ce_copy_prepare(packet, sizeof(packet), &v->vm, held, held, &copy));
	memcpy(&field, packet + 8, 8);
	CHECK(field == (0xc000000000000000ULL | copy.dst));
	memcpy(&field, packet + 24, 8);
	CHECK(field == copy.src);
	memcpy(saved, packet, sizeof(packet));
	copy.src = 0x201000; /* Same storage as 0x400000, despite different VAs. */
	CHECK(mt_ce_copy_prepare(packet, sizeof(packet), &v->vm, held, held, &copy) == -EINVAL);
	copy.src = 0x200000; copy.bytes = 4097;
	CHECK(mt_ce_copy_prepare(packet, sizeof(packet), &v->vm, held, held, &copy) == -ENOENT);
	CHECK(!memcmp(saved, packet, sizeof(packet)) && held->refs == 3 && !held->gpu_users);
	CHECK(!mt_ce3_stream_prepare(&stream_image, sizeof(stream_image), &v->vm, held, held, held, &stream));
	memcpy(&field, stream_image.commands + 8, 8);
	CHECK(field == 80);
	memcpy(&field, stream_image.record + 8, 8);
	CHECK(field == 0x2000a0);
	stream.copy.src = 0x200040;
	CHECK(mt_ce3_stream_prepare(&stream_image, sizeof(stream_image), &v->vm, held, held, held, &stream) == -EINVAL);
	CHECK(!held->gpu_users && !v->vm.active_uses && held->refs == 3);
	paging_image = kzalloc(sizeof(*paging_image), GFP_KERNEL);
	CHECK(paging_image);
	CHECK(!mt_ce3_paging_prepare(paging_image, sizeof(*paging_image), &v->vm, held, held, held, &paging));
	memcpy(&field, paging_image->paging.descriptor + 0x138, 8);
	CHECK(field == paging.stream.command_va);
	memcpy(&field, paging_image->paging.descriptor + 0x130, 8);
	CHECK(field == paging.state_va);
	memcpy(&field, paging_image->paging.state, 8);
	CHECK(field == 24);
	paging.state_va = 0x400000; /* Destination at different VA, same backing. */
	CHECK(mt_ce3_paging_prepare(paging_image, sizeof(*paging_image), &v->vm, held, held, held, &paging) == -EINVAL);
	memcpy(&field, paging_image->paging.descriptor + 0x130, 8);
	CHECK(field == 0x200300); /* Failure preserves the previous result. */
	paging.state_va = 0x201fe0;
	CHECK(mt_ce3_paging_prepare(paging_image, sizeof(*paging_image), &v->vm, held, held, held, &paging) == -ENOENT);
	CHECK(!held->gpu_users && !v->vm.active_uses && held->refs == 3);
	mutex_unlock(&lock);
	locked = false;
	drm_gem_object_put(obj);
	obj = NULL;
	CHECK(!gem.objects && held->refs == 2 && ram.buffers.objects == 2);
	mutex_lock(&lock);
	locked = true;
	ret = mt_bo_gpu_begin(held);
	if (!ret)
		gpu = true;
	CHECK(!ret && held->refs == 3);
	ret = vms.ops->destroy(v);
	if (!ret)
		v = NULL;
	CHECK(!ret && !vms.objects && held->refs == 1 && held->gpu_users == 1);
	ret = mt_bo_gpu_end(held);
	if (!ret) {
		gpu = false;
		held = NULL;
	}
	CHECK(!ret && !ram.buffers.objects && !ram.buffers.allocated_bytes);
	mutex_unlock(&lock);
	locked = false;
	CHECK(!test_tqx_heaps(&gem,drm,&vms,&lock));
	CHECK(!test_boot_views(&lock,&profile));
	CHECK(boot_allocated==boot_freed);
	CHECK(ram.allocated == ram.freed);
	result = 0;
done:
	kvfree(tqx_programs);
	kfree(tqx_job);
	kfree(tqx_stream);
	kfree(copy_stream);
	kfree(tqx_plan);
	kfree(paging_image);
	if (locked)
		mutex_unlock(&lock);
	if (obj)
		drm_gem_object_put(obj);
	mutex_lock(&lock);
	if (v)
		vms.ops->destroy(v);
	if (gpu)
		mt_bo_gpu_end(held);
	mutex_unlock(&lock);
	if (drm)
		drm_dev_put(drm);
	if (parent)
		root_device_unregister(parent);
	pr_info("mt_gem_selftest: result=%d checks=%u failure_line=%u RAM allocations=%u frees=%u boot_backing_allocations=%u boot_backing_frees=%u; no GPU access\n",
		result, checks, failure_line, ram.allocated, ram.freed, boot_allocated, boot_freed);
	/* Keep read-only results visible even on failure, without resources. */
	return 0;
}
static void __exit mt_gem_selftest_exit(void) { }
module_init(mt_gem_selftest_init);
module_exit(mt_gem_selftest_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Guest GEM integration selftest using RAM and unregistered DRM device");
