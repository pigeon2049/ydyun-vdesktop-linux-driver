#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../kernel/mt_work_job.h"

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


int main(void)
{
 struct store s = {.next_pa = 0x600000000ULL};
 struct mt_bo tables = {0}, a = {0}, b = {0};
 struct mt_gpu_vm v = {0};
 struct mt_work_job job = {0}, other = {0}, before;
 struct mt_work_command_inputs r = {.type=5, .command_va=0x200000, .bytes=128};
 void *image=malloc(32768), *scratch=malloc(32768), *mapping;
 unsigned refs;
 assert(image && scratch);
 assert(!mt_bo_create(&tables,&ops,&s,32768,4096));
 assert(!mt_bo_create(&a,&ops,&s,8192,4096));
 assert(!mt_bo_create(&b,&ops,&s,4096,4096));
 assert(!mt_gpu_vm_init(&v,&tables,image,scratch,32768));
 assert(!mt_bo_put(&tables));
 assert(!mt_gpu_vm_bind(&v,&a,0x200000,0,8192,0));
 assert(!mt_gpu_vm_bind(&v,&a,0x400000,0,4096,0));
 assert(!mt_gpu_vm_bind(&v,&b,0x600000,0,4096,0));
 before=job;
 assert(mt_work_job_prepare(&job,&v,&a,&r)==-EINVAL);
 assert(!memcmp(&job,&before,sizeof(job)) && tables.refs==1);
 v.uploaded=true; /* Modeled upload, no hardware. */
 assert(!mt_bo_cpu_begin(&b,&mapping));
 assert(mt_work_job_prepare(&job,&v,&a,&r)==-EBUSY);
 assert(!tables.gpu_users && !a.gpu_users && !v.active_uses);
 assert(tables.refs==1 && a.refs==3 && !memcmp(&job,&before,sizeof(job)));
 assert(!mt_bo_cpu_end(&b));
 refs=b.refs; b.refs=~(u32)0;
 assert(mt_work_job_prepare(&job,&v,&a,&r)==-EOVERFLOW);
 assert(tables.refs==1 && a.refs==3 && !v.active_uses);
 b.refs=refs;
 assert(!mt_work_job_prepare(&job,&v,&a,&r));
 assert(job.count==3 && v.active_uses==1 && a.gpu_users==1 && a.refs==4);
 assert(mt_work_job_prepare(&other,&v,&a,&r)==-EBUSY);
 assert(mt_bo_cpu_begin(&a,&mapping)==-EBUSY);
 assert(mt_gpu_vm_bind(&v,&b,0x800000,0,4096,0)==-EBUSY);
 assert(mt_gpu_vm_unbind(&v,0x600000,4096)==-EBUSY);
 assert(mt_gpu_vm_fini(&v)==-EBUSY);
 assert(mt_work_job_complete(&job)==-EINVAL);
 assert(!mt_work_job_cancel(&job) && !v.active_uses && a.refs==3);
 assert(mt_work_job_cancel(&job)==-EINVAL);
 assert(!mt_work_job_prepare(&job,&v,&a,&r));
 assert(!mt_gpu_vm_seal(&v));
 mt_work_job_published(&job);
 assert(mt_work_job_cancel(&job)==-EBUSY && v.active_uses==1);
 assert(!mt_bo_put(&a) && !mt_bo_put(&b)); /* Close external owners. */
 assert(!s.frees);
 assert(!mt_work_job_complete(&job));
 assert(!v.active_uses && !a.gpu_users && a.refs==2 && !s.frees);
 assert(mt_work_job_complete(&job)==-EINVAL);
 assert(mt_gpu_vm_fini(&v)==-EBUSY); /* Completion is not root withdrawal. */
 v.sealed=false; /* Fixture teardown only: this root never reached hardware. */
 assert(!mt_gpu_vm_fini(&v));
 assert(s.allocs==3 && s.frees==3);
 free(image);free(scratch);
 puts("PASS: work resource aliases, transactional rollback, CPU exclusion, VM guards, publication and owner-close lifetime");
 return 0;
}
