#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../kernel/mt_execution_context.h"

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
 struct store s={.next_pa=0x600000000ULL}, foreign={0};
 struct mt_execution_store store, wrong;
 struct mt_device_profile profile;
 struct mt_execution_process p={0}, p2={0}, before;
 struct mt_execution_context c={0}, c2={0}, saved;
 struct mt_gpu_vm vm={0};
 struct mt_bo table={0}, command={0};
 struct mt_execution_request request={.command_va=0x200000,.type=5,.bytes=128};
 struct mt_work_command_inputs in;
 u8 packet[80];
 void *image=malloc(32768), *scratch=malloc(32768);
 assert(image && scratch);
 assert(!mt_bo_create(&table,&ops,&s,32768,4096));
 assert(!mt_bo_create(&command,&ops,&s,4096,4096));
 assert(!mt_gpu_vm_init(&vm,&table,image,scratch,32768));
 assert(!mt_bo_put(&table));
 assert(!mt_gpu_vm_bind(&vm,&command,0x200000,0,4096,0));
 assert(!mt_device_profile_select(&profile,0x1ed5,0x222));
 mt_execution_store_init(&store,&s,&profile);mt_execution_store_init(&wrong,&foreign,&profile);
 before=p;
 assert(mt_execution_process_create(&wrong,&p,&vm,42)==-EXDEV);
 assert(!memcmp(&p,&before,sizeof(p)) && !vm.owners && !wrong.next_token);
 assert(!mt_execution_process_create(&store,&p,&vm,42));
 assert(p.token==0 && p.pid==42 && vm.owners==1);
 assert(mt_gpu_vm_fini(&vm)==-EBUSY); /* Unsealed VM still belongs to process. */
 saved=c;
 assert(mt_execution_context_create(&c,&p,6,0)==-EOPNOTSUPP);
 assert(mt_execution_context_create(&c,&p,7,0)==-EOPNOTSUPP);
 assert(mt_execution_context_create(&c,&p,9,0)==-EINVAL);
 assert(mt_execution_context_create(&c,&p,3,0)==-EOPNOTSUPP);
 assert(mt_execution_context_create(&c,&p,4,0)==-EOPNOTSUPP);
 assert(!memcmp(&c,&saved,sizeof(c)) && !p.contexts);
 assert(!mt_execution_context_create(&c,&p,2,2));
 assert(!mt_execution_context_create(&c2,&p,5,1));
 assert(c.route.dm==3 && c2.route.dm==2 && p.contexts==2 && store.contexts==2);
 assert(mt_execution_process_destroy(&p)==-EBUSY);
 assert(!mt_execution_context_inputs(&in,&c,&request));
 assert(in.root_pa==table.backing.gpu_pa && in.process_id==0 && in.process_pid==42 && !in.fence);
 {
  struct mt_work_command_inputs saved_in=in;
  request.type=9;
  assert(mt_execution_context_inputs(&in,&c,&request)==-EOPNOTSUPP);
  assert(!memcmp(&in,&saved_in,sizeof(in)));
  request.type=5;
 }
 assert(!mt_work_command_prepare(packet,80,&vm,&command,&in));
 assert(packet[0xc]==0x68 && packet[0x4c]==42);
 c.active_jobs=1;
 assert(mt_execution_context_destroy(&c)==-EBUSY);
 c.active_jobs=0;
 assert(!mt_execution_context_destroy(&c));
 assert(!mt_execution_context_destroy(&c2));
 assert(!mt_execution_process_destroy(&p));
 assert(!vm.owners && !store.processes && !store.contexts);
 store.next_token=~(u64)0;
 assert(!mt_execution_process_create(&store,&p,&vm,1));
 assert(p.token==~(u64)0 && store.exhausted);
 assert(mt_execution_process_create(&store,&p2,&vm,2)==-EOVERFLOW && !p2.store);
 assert(!mt_execution_process_destroy(&p));
 assert(mt_execution_process_create(&store,&p2,&vm,2)==-EOVERFLOW); /* Never recycle token. */
 assert(!mt_gpu_vm_fini(&vm));
 assert(!mt_bo_put(&command) && s.allocs==s.frees);
 free(image);free(scratch);
 puts("PASS: process/VM ownership, context routes, kernel-selected identity, busy teardown, token exhaustion");
 return 0;
}
