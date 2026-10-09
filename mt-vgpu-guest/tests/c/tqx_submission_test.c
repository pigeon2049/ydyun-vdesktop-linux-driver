#include "tqx_ram_io.h"
#include "../../kernel/mt_tqx_submission.h"
#include "../../kernel/mt_tqx_work.h"
int main(void)
{
 struct store s={.next=0x600000000ULL};
 struct mt_bo tables={0},objects[9]={0},*bo[9];struct mt_gpu_vm vm={0};
 struct mt_device_profile p;
 struct mt_tqx_submission_workspace *w=malloc(sizeof(*w));
 struct mt_tqx_submission_result result,saved;
 struct mt_tqx_work work={0};
 struct mt_execution_store execution;
 struct mt_execution_process process={0};
 struct mt_execution_context context={0};
 struct mt_tqx_submission_input req={
  .stream={.copy={0x40100001,0x40200000,8191},
   .va={0x40000000,0x8400000000ULL,0x8100000000ULL,0x8100001000ULL,0xf000000000ULL}},
  .dma_va=0x40010000,.state_va=0x40020000};
 void *image=malloc(65536),*scratch=malloc(65536);
 u32 i,j,k,refs[9],sizes[9];u64 va[9],field;
 assert(image && scratch && w && !mt_device_profile_select(&p,0x1ed5,0x222));
 assert(!mt_bo_create(&tables,&bo_ops,&s,65536,4096));
 assert(!mt_gpu_vm_init(&vm,&tables,image,scratch,65536));assert(!mt_bo_put(&tables));
 for(i=0;i<9;i++) {
  bo[i]=&objects[i];sizes[i]=i<5?mt_tqx_buffer_bytes[i]:(i==8?4096:8192);
  va[i]=i<5?req.stream.va[i]:(i==5?req.stream.copy.src-1:
      i==6?req.stream.copy.dst:i==7?req.dma_va:req.state_va);
  assert(!mt_bo_create(bo[i],&bo_ops,&s,sizes[i]+8192,4096));
  memset(bo[i]->backing.handle,0xa5,sizes[i]+8192);
  assert(!mt_gpu_vm_bind(&vm,bo[i],va[i],4096,sizes[i],MT_GPU_MAP_DEFAULT));refs[i]=bo[i]->refs;
 }
 memset(&result,0xa5,sizeof(result));
 assert(!mt_tqx_submission_upload(&result,sizeof(result),w,&io,&p,1,&vm,bo,&req));saved=result;
 assert(s.writes==11 && s.reads==11 && !vm.uploaded && !vm.active_uses && !vm.sealed);
 memcpy(&field,result.view,8);assert(field==req.dma_va);
 memcpy(&field,result.view+8,8);assert(field==4864);
 /* Regression for the real r23 fault: flags 1/3 make outputs read-only.
  * Reject all three writable roles before any upload/read/map, preserving
  * the previous result. A read-only source remains a valid copy input. */
 for(i=MT_TQX_DESTINATION;i<=MT_TQX_ENGINE_STATE;i++)for(k=0;k<2;k++) {
  assert(!mt_gpu_vm_unbind(&vm,va[i],sizes[i]));
  assert(!mt_gpu_vm_bind(&vm,bo[i],va[i],4096,sizes[i],
      MT_GPU_MAP_READ_ONLY | (k?MT_GPU_MAP_CACHE_COHERENT:0)));
  s.writes=s.reads=s.maps=0;
  assert(mt_tqx_submission_upload(&result,sizeof(result),w,&io,&p,1,&vm,bo,&req)==-EACCES);
  assert(!s.writes && !s.reads && !s.maps && !memcmp(&result,&saved,sizeof(result)));
  assert(!mt_gpu_vm_unbind(&vm,va[i],sizes[i]));
  assert(!mt_gpu_vm_bind(&vm,bo[i],va[i],4096,sizes[i],MT_GPU_MAP_DEFAULT));
 }
 assert(!mt_gpu_vm_unbind(&vm,va[MT_TQX_SOURCE],sizes[MT_TQX_SOURCE]));
 assert(!mt_gpu_vm_bind(&vm,bo[MT_TQX_SOURCE],va[MT_TQX_SOURCE],4096,
      sizes[MT_TQX_SOURCE],MT_GPU_MAP_READ_ONLY));
 assert(!mt_tqx_submission_upload(&result,sizeof(result),w,&io,&p,1,&vm,bo,&req));
 assert(s.writes==11 && s.reads==11 && !memcmp(&result,&saved,sizeof(result)));
 assert(!mt_gpu_vm_unbind(&vm,va[MT_TQX_SOURCE],sizes[MT_TQX_SOURCE]));
 assert(!mt_gpu_vm_bind(&vm,bo[MT_TQX_SOURCE],va[MT_TQX_SOURCE],4096,
      sizes[MT_TQX_SOURCE],MT_GPU_MAP_DEFAULT));
 assert(!memcmp((u8 *)bo[7]->backing.handle+4096,w->dma.descriptor,8192));
 for(i=0;i<9;i++) {
  u8 *base=bo[i]->backing.handle;
  for(j=0;j<4096;j++)assert(base[j]==0xa5 && base[4096+sizes[i]+j]==0xa5);
  if(i==5 || i==6 || i==8)for(j=0;j<sizes[i];j++)assert(base[4096+j]==0xa5);
 }
 /* Every write/read/corruption and map failure, including both DMA pages. */
 for(k=0;k<4;k++)for(j=1;j<=(k==3?22:11);j++) {
  s.writes=s.reads=s.maps=0;s.write_fail=s.read_fail=s.corrupt=s.map_fail=0;
  if(k==0)s.write_fail=j;
  if(k==1)s.read_fail=j;
  if(k==2)s.corrupt=j;
  if(k==3)s.map_fail=j;
  assert(mt_tqx_submission_upload(&result,sizeof(result),w,&io,&p,1,&vm,bo,&req)==(k==3?-ENXIO:-EIO));
  assert(!memcmp(&result,&saved,sizeof(result)));
  for(i=0;i<9;i++)assert(bo[i]->refs==refs[i] && !bo[i]->cpu_users && !bo[i]->gpu_users);
 }
 s.write_fail=s.read_fail=s.corrupt=s.map_fail=0;
 for(i=0;i<9;i++) {
  void *mapping;
  s.writes=s.reads=0;assert(!mt_bo_cpu_begin(bo[i],&mapping));
  assert(mt_tqx_submission_upload(&result,sizeof(result),w,&io,&p,1,&vm,bo,&req)==-EBUSY);
  assert(!s.writes && !s.reads);assert(!mt_bo_cpu_end(bo[i]));
  assert(!mt_bo_gpu_begin(bo[i]));
  assert(mt_tqx_submission_upload(&result,sizeof(result),w,&io,&p,1,&vm,bo,&req)==-EBUSY);
  assert(!s.writes && !s.reads);assert(!mt_bo_gpu_end(bo[i]));
 }
 /* All 15 new physical-alias pairs must fail before writing old resources. */
 for(i=7;i<9;i++)for(j=0;j<i;j++) {
  assert(!mt_gpu_vm_unbind(&vm,va[i],sizes[i]));
  assert(!mt_gpu_vm_bind(&vm,bo[j],va[i],4096,sizes[i],MT_GPU_MAP_DEFAULT));bo[i]=bo[j];
  assert(mt_tqx_submission_upload(&result,sizeof(result),w,&io,&p,1,&vm,bo,&req)==-EINVAL);
  assert(!s.writes && !s.reads && !memcmp(&result,&saved,sizeof(result)));
  assert(!mt_gpu_vm_unbind(&vm,va[i],sizes[i]));bo[i]=&objects[i];
  assert(!mt_gpu_vm_bind(&vm,bo[i],va[i],4096,sizes[i],MT_GPU_MAP_DEFAULT));
 }
 for(i=7;i<9;i++) {
  assert(!mt_gpu_vm_unbind(&vm,va[i],sizes[i]));
  assert(mt_tqx_submission_upload(&result,sizeof(result),w,&io,&p,1,&vm,bo,&req)==-ENOENT);
  assert(!s.writes && !s.reads);
  assert(!mt_gpu_vm_bind(&vm,bo[i],va[i],4096,sizes[i],MT_GPU_MAP_DEFAULT));
 }
 assert(!mt_gpu_vm_unbind(&vm,va[7],sizes[7]));
 assert(!mt_gpu_vm_bind(&vm,bo[7],va[7],4096,4096,MT_GPU_MAP_DEFAULT));
 assert(mt_tqx_submission_upload(&result,sizeof(result),w,&io,&p,1,&vm,bo,&req)==-ENOENT);
 assert(!s.writes && !s.reads);
 assert(!mt_gpu_vm_unbind(&vm,va[7],4096));
 assert(!mt_gpu_vm_bind(&vm,bo[7],va[7],4096,sizes[7],MT_GPU_MAP_DEFAULT));
 vm.sealed=true;
 /* Content may be refreshed in already-mapped BOs with an idle sealed root. */
 s.writes=s.reads=0;
 assert(!mt_tqx_submission_upload(&result,sizeof(result),w,&io,&p,1,&vm,bo,&req));
 assert(s.writes==11 && s.reads==11 && !memcmp(&result,&saved,sizeof(result)));
 vm.sealed=false;
	s.writes=s.reads=0;
 vm.active_uses=1;
 assert(mt_tqx_submission_upload(&result,sizeof(result),w,&io,&p,1,&vm,bo,&req)==-EBUSY);vm.active_uses=0;
 assert(mt_tqx_submission_upload(&result,sizeof(result)-1,w,&io,&p,1,&vm,bo,&req)==-EINVAL);
 assert(mt_tqx_submission_upload(&result,sizeof(result),w,&io,&p,0,&vm,bo,&req)==-EINVAL);
 assert(!s.writes && !s.reads && !memcmp(&result,&saved,sizeof(result)));
 req.stream.copy.bytes=1;
 assert(!mt_tqx_submission_upload(&result,sizeof(result),w,&io,&p,8,&vm,bo,&req));
 assert(s.writes==11 && s.reads==11);
 memcpy(&field,result.view+8,8);assert(field==5760);
 /* Existing state survives a complete retry and a different core layout. */
 for(j=0;j<4096;j++)assert(((u8 *)bo[8]->backing.handle)[4096+j]==0xa5);
 mt_execution_store_init(&execution,&s,&p);
 assert(!mt_execution_process_create(&execution,&process,&vm,1234));
 assert(!mt_execution_context_create(&context,&process,1,0));
 /* The last pair is the actual page-table upload/readback, not a flag-only
  * model. Every failure leaves the work empty and releases transient users. */
 for(k=0;k<4;k++)for(j=1;j<=(k==3?24:12);j++) {
  s.writes=s.reads=s.maps=0;s.write_fail=s.read_fail=s.corrupt=s.map_fail=0;
  if(k==0)s.write_fail=j;
  if(k==1)s.read_fail=j;
  if(k==2)s.corrupt=j;
  if(k==3)s.map_fail=j;
  assert(mt_tqx_work_prepare(&work,w,&io,&p,1,&context,bo,&req)==(k==3?-ENXIO:-EIO));
  assert(!work.context && work.job.state==MT_JOB_EMPTY && !context.active_jobs && !vm.active_uses);
  for(i=0;i<9;i++)assert(bo[i]->refs==refs[i] && !bo[i]->cpu_users && !bo[i]->gpu_users);
 }
 s.write_fail=s.read_fail=s.corrupt=s.map_fail=0;s.writes=s.reads=0;
 assert(!mt_tqx_work_prepare(&work,w,&io,&p,1,&context,bo,&req));
 assert(s.writes==12 && s.reads==12 && vm.uploaded && vm.active_uses==1 && context.active_jobs==1);
 assert(work.context==&context && work.job.count==10 && work.job.state==MT_JOB_HELD);
 assert(!memcmp(tables.backing.handle,vm.image,vm.capacity));
 assert(mt_execution_context_destroy(&context)==-EBUSY && mt_gpu_vm_fini(&vm)==-EBUSY);
 assert(mt_tqx_work_prepare(&work,w,&io,&p,1,&context,bo,&req)==-EINVAL);
 assert(mt_work_job_move(&work.job,&work.job)==-EINVAL);
 for(i=0;i<9;i++) {
  void *mapping;
  assert(bo[i]->gpu_users==1 && bo[i]->refs==refs[i]+1);
  assert(mt_bo_cpu_begin(bo[i],&mapping)==-EBUSY);
 }
 assert(!mt_tqx_work_cancel(&work) && !context.active_jobs && !vm.active_uses);
 assert(mt_tqx_work_cancel(&work)==-EINVAL);
 assert(!mt_execution_context_destroy(&context));
 assert(!mt_execution_process_destroy(&process));
 for(i=0;i<9;i++)assert(!mt_bo_put(bo[i]));
 assert(!mt_gpu_vm_fini(&vm) && s.allocs==s.frees);
 free(scratch);free(image);free(w);
 puts("PASS: TQX eleven-page upload, preserved engine state, 55 I/O/map faults, 18 busy gates, 15 new alias pairs, missing/short mappings and retry; RAM only");
 puts("PASS: TQX atomic upload/table-readback/pinning, 60 I/O faults, ten retained BOs, CPU exclusion, context ownership and cancellation; RAM only");
 return 0;
}
