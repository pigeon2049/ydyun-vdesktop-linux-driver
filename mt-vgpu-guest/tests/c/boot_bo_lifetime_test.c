/* Real VRAM BO/cache/VM code; RAM allocator, I/O and OS primitives modeled. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../kernel/mt_bo.h"
#define MT_VRAM_H
#define MT_GUEST_BOOT_RESOURCES_H
#define MT_GUEST_SYSTEM_MEMORY_H
struct mt_system_memory {void *cpu;u64 *page_pa;u32 bytes;};
#define __iomem
#define __force
#define GFP_KERNEL 0
#define THIS_MODULE 0
#define PAGE_SIZE 4096U
#define MT_POOL_NORMAL 0
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define WARN_ON(x) ((x) ? (assert(!(x)), 1) : 0)
struct mutex { bool held; };
#define lockdep_assert_held(m) assert((m)->held)
struct list_head { struct list_head *next,*prev; };
static void list_init(struct list_head *h){h->next=h->prev=h;}
static void list_add_tail(struct list_head *n,struct list_head *h)
{n->prev=h->prev;n->next=h;h->prev->next=n;h->prev=n;}
static void list_del(struct list_head *n){n->prev->next=n->next;n->next->prev=n->prev;}
#define list_for_each_entry(p,h,m) for(p=container_of((h)->next,__typeof__(*p),m); &(p)->m!=(h); p=container_of((p)->m.next,__typeof__(*p),m))
struct mt_vram_block {struct list_head link;void *mapping;u64 bar_offset,gpu_pa;u32 size;};
struct mt_vram {struct list_head blocks;bool region_owned;u64 next;};
enum {MT_BOOT_DUMMY,MT_BOOT_PDS,MT_BOOT_USC,MT_BOOT_YUV,MT_BOOT_KILL,
      MT_BOOT_FENCE,MT_BOOT_PAGING_CONTEXT,MT_BOOT_PB,MT_BOOT_ALLOCATION_COUNT};
struct mt_boot_resources {struct mt_vram_block blocks[MT_BOOT_ALLOCATION_COUNT];bool prepared;};
static unsigned heaps,heap_frees,fail_heap,modules,vram_allocs,vram_frees,clears,fail_vram;
static void *kzalloc(size_t n,int flags)
{void *p;(void)flags;if(fail_heap && !--fail_heap)return NULL;p=calloc(1,n);assert(p);heaps++;return p;}
static void kfree(void *p){if(p){heap_frees++;free(p);}}
#define kvzalloc kzalloc
#define kvfree kfree
static void __module_get(int p){(void)p;modules++;}
static void module_put(int p){(void)p;assert(modules);modules--;}
static int mt_vram_alloc(struct mt_vram *v,u32 pool,u32 n,u32 align,struct mt_vram_block *b)
{
 (void)pool;if(fail_vram && !--fail_vram)return -ENOMEM;assert(!b->size);v->next=(v->next+align-1)&~(u64)(align-1);
 b->mapping=malloc(n);assert(b->mapping);memset(b->mapping,0x5a,n);
 b->gpu_pa=b->bar_offset=v->next;b->size=n;v->next+=n;
 list_add_tail(&b->link,&v->blocks);vram_allocs++;return 0;
}
static void mt_vram_free(struct mt_vram *v,struct mt_vram_block *b)
{(void)v;assert(b->size);free(b->mapping);list_del(&b->link);memset(b,0,sizeof(*b));vram_frees++;}
static void memset_io(void *p,int x,size_t n){clears++;memset(p,x,n);}
#define memcpy_toio memcpy
#define memcpy_fromio memcpy
static void mb(void){}
#include "../../kernel/mt_vm_vram.h"

int main(void)
{
 const u32 indices[]={MT_BOOT_PB,MT_BOOT_PDS,MT_BOOT_YUV,MT_BOOT_KILL,MT_BOOT_FENCE,0};
 const u32 sizes[]={0x200000,0x100000,0x80000,0x80000,4096,0x400000};
 struct mutex lock={true};struct mt_vram vram={.region_owned=true,.next=0x600000000ULL};
 struct mt_system_memory paging_command={0};struct mt_vram_block *blocks[5];
 struct mt_reserved_pools pools={0};
 struct mt_boot_resources boot={.prepared=true};struct mt_boot_bo_store shared={0};
 struct mt_bo_store buffers;struct mt_vm_store vms;struct mt_vm_vram *a=NULL,*b=NULL;
 struct mt_device_profile profile;struct mt_bo *held;u32 i,j;unsigned before,objects;
 u8 byte=0;struct mt_vram_block foreign={0};struct mt_bo invalid={0};
 list_init(&vram.blocks);mt_bo_store_init(&buffers,&vram,&lock);mt_vm_store_init(&vms,&buffers);
 assert(!mt_device_profile_select(&profile,0x1ed5,0x222));
 for(i=1;i<=3;i++){
  fail_vram=i;assert(mt_reserved_pools_prepare(&vram,&pools,&profile)==-ENOMEM);
  assert(!fail_vram && !pools.prepared && vram_allocs==vram_frees);
 }
 vram_allocs=vram_frees=0;
 assert(!mt_reserved_pools_prepare(&vram,&pools,&profile));
 assert(mt_reserved_pools_prepare(&vram,&pools,&profile)==-EINVAL);
 for(i=0;i<5;i++){
  blocks[i]=&boot.blocks[indices[i]];
  assert(!mt_vram_alloc(&vram,0,sizes[i],4096,blocks[i]));
 }
 paging_command.cpu=malloc(MT_PAGING_COMMAND_BYTES);paging_command.page_pa=malloc(1024*sizeof(u64));
 assert(paging_command.cpu && paging_command.page_pa);memset(paging_command.cpu,0x5a,MT_PAGING_COMMAND_BYTES);
 for(i=0;i<1024;i++)paging_command.page_pa[i]=0x8800000000ULL+((i*37)%1024)*8192;
 assert(!mt_vram_alloc(&vram,0,8192,4096,&boot.blocks[MT_BOOT_PAGING_CONTEXT]));
 memset(boot.blocks[MT_BOOT_PAGING_CONTEXT].mapping,0xa5,8192);
 assert(mt_boot_bo_init(&shared,&buffers,&boot,NULL,&pools)==-EINVAL);
 paging_command.bytes=8192;
 assert(mt_boot_bo_init(&shared,&buffers,&boot,&paging_command,&pools)==-EINVAL);
 paging_command.bytes=MT_PAGING_COMMAND_BYTES;
 assert(!mt_boot_bo_init(&shared,&buffers,&boot,&paging_command,&pools));vms.boot=&shared;
 assert(!modules && !buffers.objects && !mt_boot_bo_can_release(&shared));
 assert(!vms.ops->create(&vms,16,&a) && !vms.ops->create(&vms,16,&b));
 assert(modules==2 && buffers.objects==2 && clears==2);
 before=heaps-heap_frees;
 for(i=1;i<=18;i++){
  fail_heap=i;
  assert(vms.ops->bind_boot_shared(a,&profile)==-ENOMEM);
  assert(!fail_heap && !a->vm.count && !mt_boot_bo_can_release(&shared));
  assert(modules==2 && buffers.objects==2 && heaps-heap_frees==before && vram_frees==0);
 }
 /* A block from another owner and each detached boot list node are rejected. */
 assert(mt_bo_vram_borrow(&invalid,&buffers,&foreign)==-EXDEV);
 for(i=0;i<8;i++){
  struct mt_vram_block *block=i<5?blocks[i]:&pools.blocks[i-5];
  list_del(&block->link);
  assert(vms.ops->bind_boot_shared(a,&profile)==-EXDEV);
  list_add_tail(&block->link,&vram.blocks);
  assert(!a->vm.count && !mt_boot_bo_can_release(&shared));
  assert(modules==2 && buffers.objects==2 && heaps-heap_frees==before);
 }
 paging_command.page_pa[1023]++;
 assert(vms.ops->bind_boot_shared(a,&profile)==-ERANGE && !a->vm.count && !mt_boot_bo_can_release(&shared));
 paging_command.page_pa[1023]--;
 assert(!vms.ops->bind_boot_shared(a,&profile));
 assert(modules==11 && buffers.objects==11 && a->vm.count==9 && clears==2);
 assert(mt_boot_bo_can_release(&shared)==-EBUSY);
 objects=buffers.objects;
 for(i=0;i<MT_BOOT_BO_COUNT;i++){
  shared.slots[i]->refs=~(u32)0;
  assert(vms.ops->bind_boot_shared(b,&profile)==-EOVERFLOW);
  shared.slots[i]->refs=1;
  for(j=0;j<MT_BOOT_BO_COUNT;j++)assert(shared.slots[j]->refs==1);
  assert(!b->vm.count && buffers.objects==objects && modules==11);
 }
 /* Fail reference acquisition inside the atomic binder, after cache gets. */
 for(i=0;i<MT_BOOT_BO_COUNT;i++){
  shared.slots[i]->refs=~(u32)0-1;
  assert(vms.ops->bind_boot_shared(b,&profile)==-EOVERFLOW);
  assert(shared.slots[i]->refs==~(u32)0-1);
  shared.slots[i]->refs=1;
  for(j=0;j<MT_BOOT_BO_COUNT;j++)assert(shared.slots[j]->refs==1);
  assert(!b->vm.count && buffers.objects==objects && modules==11);
 }
 assert(!vms.ops->bind_boot_shared(b,&profile));
 assert(modules==11 && buffers.objects==11 && b->vm.count==9);
 for(i=0;i<MT_BOOT_BO_COUNT;i++){
  assert(a->vm.bindings[i].bo==b->vm.bindings[i].bo && shared.slots[i]->refs==2);
  assert(shared.slots[i]->backing.gpu_pa==(i<5?blocks[i]->gpu_pa:i==5?paging_command.page_pa[0]:pools.blocks[i-6].gpu_pa));
  assert(shared.slots[i]->ops->clear(&buffers,&shared.slots[i]->backing)==-EPERM);
  assert(!mt_bo_vram_read(shared.slots[i],0,&byte,1) && byte==0x5a);
 }
 {
  struct mt_execution_store execution;struct mt_execution_process process={0};
  struct mt_execution_context context={0};struct mt_pool_slice slices[3]={0},extra={0},copy;
  struct mt_guest_pool_spec specs[3];const u32 kinds[]={4,6,3};
  mt_guest_plan_pools(specs);mt_execution_store_init(&execution,&buffers,&profile);
  assert(!mt_execution_process_create(&execution,&process,&a->vm,42));
  assert(!mt_execution_context_create(&context,&process,1,0));
  assert(mt_boot_pool_alloc(&shared,&context,5,4096,&extra)==-EOPNOTSUPP);
  shared.slots[6]->refs=~(u32)0;
  assert(mt_boot_pool_alloc(&shared,&context,4,4096,&extra)==-EOVERFLOW);
  shared.slots[6]->refs=2;
  assert(!extra.state && !context.pool_slices && !pools.slices[0].slices);
  context.pool_slices=~(u32)0;
  assert(mt_boot_pool_alloc(&shared,&context,4,4096,&extra)==-EOVERFLOW);
  context.pool_slices=0;

  for(i=0;i<3;i++){
   assert(!mt_boot_pool_alloc(&shared,&context,kinds[i],5001,&slices[i]));
   assert(slices[i].va==specs[i].va && slices[i].bytes==8192 && !slices[i].offset);
   copy=slices[i];assert(mt_pool_slice_free(&copy)==-EINVAL);
   assert(mt_execution_context_destroy(&context)==-EBUSY);
   assert(!mt_boot_pool_alloc(&shared,&context,kinds[i],specs[i].bytes-8192,&extra));
   assert(extra.offset==8192 && extra.bytes==specs[i].bytes-8192);
   assert(mt_boot_pool_alloc(&shared,&context,kinds[i],4096,&copy)==-EINVAL);
   memset(&copy,0,sizeof(copy));
   assert(mt_boot_pool_alloc(&shared,&context,kinds[i],4096,&copy)==-ENOSPC);
   assert(!mt_pool_slice_free(&extra));
   assert(!mt_bo_gpu_begin(slices[i].bo));
   assert(mt_pool_slice_free(&slices[i])==-EBUSY);
   assert(mt_boot_pool_alloc(&shared,&context,kinds[i],4096,&extra)==-EBUSY);
   assert(!mt_bo_gpu_end(slices[i].bo));
  }
  assert(context.pool_slices==3);
  for(i=0;i<3;i++){
   assert(!mt_pool_slice_free(&slices[i]));
   assert(mt_pool_slice_free(&slices[i])==-EINVAL);
   assert(!mt_boot_pool_alloc(&shared,&context,kinds[i],specs[i].bytes,&extra));
   assert(extra.va==specs[i].va && !extra.offset);
   assert(!mt_pool_slice_free(&extra));
  }
  assert(!context.pool_slices && !mt_execution_context_destroy(&context));
  assert(!mt_execution_process_destroy(&process));
 }
 /* The exact fixed-resource set is cached as already bound; repeating the
  * internal bind operation is intentionally idempotent. */
 assert(!vms.ops->bind_boot_shared(a,&profile));
 assert(!vms.ops->destroy(a));a=NULL;
 assert(modules==10 && buffers.objects==10 && vram_frees==1);
 held=shared.slots[MT_PROCESS_SHARED_COUNT+1];assert(!mt_bo_gpu_begin(held));
 assert(mt_bo_vram_read(held,0,&byte,1)==-EBUSY);
 assert(!vms.ops->destroy(b));b=NULL;
 assert(modules==1 && buffers.objects==1 && vram_frees==2 && shared.slots[MT_PROCESS_SHARED_COUNT+1]==held);
 assert(mt_boot_bo_can_release(&shared)==-EBUSY);
 assert(!mt_bo_gpu_end(held));held=NULL;
 assert(!modules && !buffers.objects && !buffers.allocated_bytes && !mt_boot_bo_can_release(&shared));
 for(i=0;i<MT_GUEST_POOL_COUNT;i++)
  for(j=0;j<pools.blocks[i].size;j++)assert(((u8 *)pools.blocks[i].mapping)[j]==0x5a);
 /* Recreate views after all previous containers have been destroyed. */
 assert(!vms.ops->create(&vms,16,&a));assert(!vms.ops->bind_boot_shared(a,&profile));
 assert(!vms.ops->destroy(a));a=NULL;
 assert(!modules && !mt_boot_bo_can_release(&shared));
 /* Sealed-but-idle teardown (r137/r138): destroy reopens the space itself,
  * so every owned reference is released. A space with live GPU uses stays
  * sealed and busy. Direct fini remains strict throughout. */
 {unsigned m0=modules,o0=buffers.objects;u64 b0=buffers.allocated_bytes;
  assert(!vms.ops->create(&vms,16,&a));assert(!vms.ops->bind_boot_shared(a,&profile));
  assert(a->vm.count);
  a->vm.uploaded=true; /* Model an unpublished upload; never a hardware ack. */
  assert(!vms.ops->seal(a));
  assert(mt_gpu_vm_fini(&a->vm)==-EBUSY);
  a->vm.active_uses=1;
  assert(vms.ops->destroy(a)==-EBUSY && a->vm.sealed);
  a->vm.active_uses=0;
  assert(!vms.ops->destroy(a));a=NULL;
  assert(modules==m0 && buffers.objects==o0 && buffers.allocated_bytes==b0);
  assert(!vms.ops->create(&vms,16,&a));
  assert(mt_gpu_vm_unseal(&a->vm)==-EALREADY);
  assert(!vms.ops->bind_boot_shared(a,&profile));
  a->vm.uploaded=true;assert(!vms.ops->seal(a));
  assert(!mt_gpu_vm_unseal(&a->vm) && !a->vm.sealed);
  assert(mt_gpu_vm_unseal(&a->vm)==-EALREADY);
  assert(!vms.ops->destroy(a));a=NULL;
  assert(modules==m0 && buffers.objects==o0 && buffers.allocated_bytes==b0);
  assert(!mt_boot_bo_can_release(&shared));
 }
 for(i=0;i<5;i++){
  struct mt_vram_block *block=blocks[i];
  for(j=0;j<block->size;j++)assert(((u8 *)block->mapping)[j]==0x5a);
  mt_vram_free(&vram,block);
 }
 for(j=0;j<MT_PAGING_COMMAND_BYTES;j++)assert(((u8 *)paging_command.cpu)[j]==0x5a);
 free(paging_command.cpu);free(paging_command.page_pa);
 for(j=0;j<8192;j++)assert(((u8 *)boot.blocks[MT_BOOT_PAGING_CONTEXT].mapping)[j]==0xa5);
 mt_vram_free(&vram,&boot.blocks[MT_BOOT_PAGING_CONTEXT]);
 mt_reserved_pools_fini(&vram,&pools);
 assert(!mt_bo_store_fini(&buffers) && !vms.objects);
 assert(vram_allocs==vram_frees && heaps==heap_frees);
 puts("PASS: actual BO/VM boot borrowing, eighteen view failures, three pool allocation rollbacks, foreign/detached blocks, shared cache reuse, GPU retention, no borrowed clear/free, balanced module and backing lifetimes; RAM models only");
 return 0;
}
