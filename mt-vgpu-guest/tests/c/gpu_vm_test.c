#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../kernel/mt_gpu_vm.h"
#include "../../kernel/mt_process_resources.h"
#include "../../kernel/mt_work_job.h"
#include "../../kernel/mt_ce_copy.h"

struct store { u64 next_pa; unsigned allocs, frees; };
static int alloc(void *opaque, u32 bytes, u32 alignment, struct mt_bo_backing *b)
{
	struct store *s = opaque;
	void *p = malloc(bytes);
	if (!p) return -ENOMEM;
	s->next_pa = (s->next_pa + alignment - 1) & ~(u64)(alignment - 1);
	*b = (struct mt_bo_backing){p, s->next_pa, s->next_pa, bytes};
	s->next_pa += bytes;
	s->allocs++;
	return 0;
}
static int clear(void *s, const struct mt_bo_backing *b)
{ (void)s; memset(b->handle, 0, b->bytes); return 0; }
static void release(void *opaque, const struct mt_bo_backing *b)
{ struct store *s = opaque; free(b->handle); s->frees++; }
static int map(void *s, const struct mt_bo_backing *b, void **out)
{ (void)s; *out = b->handle; return 0; }
static void unmap(void *s, const struct mt_bo_backing *b) { (void)s; (void)b; }
static const struct mt_bo_ops ops = {alloc, clear, release, map, unmap};

/* Independent three-level walk. Do not call the production index/encoder
 * functions here; verify PA translations rather than just nonzero entries. */
static u64 walk(const struct mt_gpu_vm *v, u64 va, u64 *pte)
{
	u32 pc;
	u64 pd, root = v->tables->backing.gpu_pa, offset;
	memcpy(&pc, (u8 *)v->image + ((va / 0x40000000) % 1024) * 4, 4);
	if (!(pc & 1)) return 0;
	offset = ((u64)(pc & 0xfffffff0U) << 8) - root;
	assert(offset <= v->capacity - 4096);
	memcpy(&pd, (u8 *)v->image + offset + ((va / 0x200000) % 512) * 8, 8);
	if (!(pd & 1)) return 0;
	offset = (pd & 0xfffffff000ULL) - root;
	assert(offset <= v->capacity - 4096);
	memcpy(pte, (u8 *)v->image + offset + ((va / 4096) % 512) * 8, 8);
	return *pte & 1 ? (*pte & 0xfffffff000ULL) + (va % 4096) : 0;
}

static void shared_resources(void)
{
	const u32 sizes[] = {0x200000,0x100000,0x80000,0x80000,4096,0x400000};
	struct store s = {.next_pa=0x620000000ULL};
	struct mt_bo tables={0}, command={0}, resources[6]={0}, *bo[6];
	struct mt_gpu_vm vm={0};
	struct mt_device_profile profile;
	struct mt_work_job job={0};
	struct mt_work_command_inputs request={.command_va=0x40000000,.bytes=80,.type=1};
	u8 *image=malloc(65536), *scratch=malloc(65536), *saved=malloc(65536);
	u32 i,j;u64 pte, scatter[1024];
	assert(image && scratch && saved);
	assert(!mt_device_profile_select(&profile,0x1ed5,0x222));
	assert(!mt_bo_create(&tables,&ops,&s,65536,4096));
	assert(!mt_bo_create(&command,&ops,&s,4096,4096));
	assert(!mt_gpu_vm_init(&vm,&tables,image,scratch,65536));
	assert(!mt_gpu_vm_bind(&vm,&command,0x40000000,0,4096,0));
	for(i=0;i<6;i++){
		assert(!mt_bo_create(&resources[i],&ops,&s,sizes[i],4096));bo[i]=&resources[i];
	}
	for(i=0;i<1024;i++)scatter[i]=0x8810000000ULL+((i*37)%1024)*8192;
	resources[5].page_pa=scatter;
	memcpy(saved,image,65536);vm.uploaded=true;
	/* Overflow at every acquisition position must roll back earlier pins. */
	for(i=0;i<6;i++){
		resources[i].refs=~(u32)0;
		assert(mt_process_resources_bind(&vm,&profile,bo)==-EOVERFLOW);
		resources[i].refs=1;
		assert(vm.count==1 && vm.uploaded && !memcmp(saved,image,65536));
		for(j=0;j<6;j++)assert(resources[j].refs==1);
	}
	for(i=1;i<6;i++)if(i!=MT_SHARED_FENCE){
		bo[i]=NULL;assert(mt_process_resources_bind(&vm,&profile,bo)==-ENOENT);bo[i]=&resources[i];
	}
	resources[5].backing.bytes=8192; /* Reject the former paging-context substitution. */
	assert(mt_process_resources_bind(&vm,&profile,bo)==-ERANGE);
	resources[5].backing.bytes=MT_PAGING_COMMAND_BYTES;
	vm.capacity=3*4096;
	assert(mt_process_resources_bind(&vm,&profile,bo)==-ENOSPC);vm.capacity=65536;
	assert(vm.count==1 && vm.uploaded && !memcmp(saved,image,65536));
	/* A malformed last page cannot change existing mappings or references. */
	for(i=0;i<3;i++){
		u64 save=scatter[1023];
		scatter[1023]=i==0?save+1:i==1?(1ULL<<40):tables.backing.gpu_pa+15*4096;
		assert(mt_process_resources_bind(&vm,&profile,bo)<0);
		assert(vm.count==1 && vm.uploaded && !memcmp(saved,image,65536));
		for(j=0;j<6;j++)assert(resources[j].refs==1);
		scatter[1023]=save;
	}
	assert(!mt_process_resources_bind(&vm,&profile,bo));
	assert(vm.count==7 && !vm.uploaded);
	for(i=1;i<vm.count;i++){
		const struct mt_vm_binding *b=&vm.bindings[i];
		assert(b->bo==bo[i-1] && !b->flags && b->bo->refs==2);
		for(j=0;j<b->bytes;j+=4096){
			assert(walk(&vm,b->va+j+31,&pte)==(b->bo->page_pa?b->bo->page_pa[j/4096]:b->bo->backing.gpu_pa+j)+31);
			assert((pte&7)==1 && !(pte&(1ULL<<62)));
		}
	}
	pte=0xdeadbeef;
	assert(!mt_ce_copy_resolve(&vm,bo[5],0x81ff800000ULL,4096,&pte) && pte==scatter[0]);
	assert(mt_process_resources_bind(&vm,&profile,bo)==-EEXIST);
	vm.uploaded=true;
	assert(!mt_work_job_prepare(&job,&vm,&command,&request));
	assert(job.count==8 && vm.active_uses==1);
	for(i=0;i<6;i++)assert(resources[i].refs==3 && resources[i].gpu_users==1);
	assert(mt_process_resources_bind(&vm,&profile,bo)==-EBUSY);
	assert(!mt_work_job_cancel(&job));
	assert(!mt_gpu_vm_fini(&vm));
	for(i=0;i<6;i++)assert(resources[i].refs==1 && !mt_bo_put(&resources[i]));
	assert(!mt_bo_put(&tables) && !mt_bo_put(&command));
	assert(s.allocs==s.frees);
	free(image);free(scratch);free(saved);
	puts("PASS: shared-resource batch mapping, every-page walks, six reference rollbacks, missing/short BOs, table exhaustion and work-job pins; RAM only");
}

int main(void)
{
	struct store s = {.next_pa = 0x600000000ULL}, other = {.next_pa = 0x700000000ULL};
	struct mt_bo tables = {0}, a = {0}, b = {0}, foreign = {0};
	struct mt_gpu_vm v = {0};
	u8 *image = malloc(32768), *scratch = malloc(32768), *saved = malloc(32768);
	u64 pte, expected;
	u32 before_refs, i, pages;
	assert(image && scratch && saved);
	assert(!mt_bo_create(&tables, &ops, &s, 32768, 4096));
	assert(!mt_bo_create(&a, &ops, &s, 0x6000, 4096));
	assert(!mt_bo_create(&b, &ops, &s, 0x4000, 4096));
	assert(!mt_bo_create(&foreign, &ops, &other, 4096, 4096));
	assert(mt_gpu_vm_init(&v, &tables, image, image + 4096, 32768) == -EINVAL);
	tables.page_pa=&expected;
	assert(mt_gpu_vm_init(&v, &tables, image, scratch, 32768)==-EINVAL);
	tables.page_pa=NULL;
	assert(!mt_gpu_vm_init(&v, &tables, image, scratch, 32768));
	assert(!mt_bo_put(&tables) && tables.refs == 1);
	assert(mt_gpu_vm_seal(&v) == -EINVAL);
	assert(mt_gpu_vm_bind(&v, &foreign, 0, 0, 4096, 0) == -EXDEV);
	assert(mt_gpu_vm_bind(&v, &tables, 0, 0, 4096, 0) == -EINVAL);
	assert(!mt_gpu_vm_bind(&v, &a, 0x3ffff000, 4096, 0x3000, 0x1c));
	assert(v.used_pages == 5 && a.refs == 2);
	for (i = 0; i < 0x3000; i += 4096) {
		assert(walk(&v, 0x3ffff000 + i + 17, &pte) == a.backing.gpu_pa + 4096 + i + 17);
		assert(pte & (1ULL << 62));
	}
	memcpy(saved, image, 32768);
	before_refs = a.refs;
	v.uploaded = true; /* Model an unpublished upload; never a hardware ack. */
	assert(mt_gpu_vm_bind(&v, &a, 0x40000000, 0, 4096, 0) == -EEXIST);
	assert(mt_gpu_vm_bind(&v, &a, 1ULL << 40, 0, 4096, 0) == -EINVAL);
	assert(mt_gpu_vm_bind(&v, &a, 0x200000, 0x5000, 8192, 0) == -ERANGE);
	assert(mt_gpu_vm_bind(&v, &a, 0x200000, 0, 4096, 0x20) == -EINVAL);
	assert(!memcmp(saved, image, 32768) && a.refs == before_refs && v.uploaded);
	a.refs = ~(u32)0;
	assert(mt_gpu_vm_bind(&v, &a, 0x200000, 0, 4096, 0) == -EOVERFLOW);
	assert(!memcmp(saved, image, 32768) && v.count == 1 && v.uploaded);
	a.refs = before_refs;
	assert(!mt_gpu_vm_bind(&v, &b, 0x8400000000ULL, 0, 8192, 3));
	assert(!v.uploaded && v.used_pages == 7 && b.refs == 2);
	assert(walk(&v, 0x840000107bULL, &pte) == b.backing.gpu_pa + 0x107b);
	assert((pte & 7) == 7 && !(pte & (1ULL << 62)));
	assert(mt_gpu_vm_unbind(&v, 0x3ffff000, 4096) == -ENOENT);
	assert(!mt_bo_put(&a)); /* VM keeps its object after the original owner closes. */
	assert(!mt_gpu_vm_unbind(&v, 0x3ffff000, 0x3000));
	assert(!a.refs && v.used_pages == 3 && v.count == 1);
	assert(!walk(&v, 0x3ffff000, &pte));
	for (i = v.used_pages * 4096; i < v.capacity; i++) assert(!image[i]);
	assert(walk(&v, 0x8400001000ULL, &pte) == b.backing.gpu_pa + 4096);
	assert(mt_gpu_vm_seal(&v) == -EINVAL); /* Must upload the revised image. */
	v.uploaded = true;
	{
		/* A snapshot tests sealing without publishing or adding ownership.
		 * The original remains the sole owner used for fixture cleanup. The
		 * binding arrays are shared by a copy, so the snapshot is only used for
		 * sealed/EBUSY probes that never rebind or unbind. */
		struct mt_gpu_vm frozen = v;
		assert(!mt_gpu_vm_seal(&frozen));
		assert(mt_gpu_vm_seal(&frozen) == -EALREADY);
		assert(mt_gpu_vm_bind(&frozen, &b, 0x200000, 0, 4096, 0) == -EBUSY);
		assert(mt_gpu_vm_fini(&frozen) == -EBUSY && b.refs == 2);
	}
	assert(!mt_gpu_vm_fini(&v) && !tables.refs && b.refs == 1);
	assert(!mt_bo_put(&b) && !mt_bo_put(&foreign));
	/* Insufficient table capacity: entire prior image/reference plan survives. */
	assert(!mt_bo_create(&tables, &ops, &s, 3 * 4096, 4096));
	assert(!mt_bo_create(&a, &ops, &s, 0x6000, 4096));
	assert(!mt_gpu_vm_init(&v, &tables, image, scratch, 3 * 4096));
	assert(!mt_gpu_vm_bind(&v, &a, 0x200000, 0, 4096, 0));
	pages = v.used_pages;
	memcpy(saved, image, v.capacity);
	assert(mt_gpu_vm_bind(&v, &a, 0x40000000, 4096, 4096, 0) == -ENOSPC);
	assert(!memcmp(saved, image, v.capacity) && v.used_pages == pages && a.refs == 2);
	/* Multiple VAs may alias one BO; each mapping owns exactly one reference. */
	assert(!mt_gpu_vm_bind(&v, &a, 0x201000, 0, 4096, 0));
	expected = a.backing.gpu_pa;
	assert(walk(&v, 0x201055, &pte) == expected + 0x55);
	assert(a.refs == 3);
	assert(!mt_gpu_vm_unbind(&v, 0x200000, 4096) && a.refs == 2);
	assert(!mt_gpu_vm_unbind(&v, 0x201000, 4096) && a.refs == 1 && v.used_pages == 1);
	for (i = 0; i < v.capacity; i++) assert(!image[i]);
	assert(!mt_gpu_vm_fini(&v) && !mt_bo_put(&tables) && !mt_bo_put(&a));
	assert(s.allocs == s.frees && other.allocs == other.frees);
	free(image); free(scratch); free(saved);
	puts("PASS: BO-backed three-level walks, transactional bind/unbind, table exhaustion, aliases, stale-entry clearing and seal lifetime");
	shared_resources();
	return 0;
}
