#include "tqx_ram_io.h"
int main(void)
{
 struct store s={.next=0x600000000ULL};
 struct mt_bo tables={0},objects[7]={0},*bo[7];struct mt_gpu_vm vm={0};
 struct mt_device_profile p;struct mt_tqx_upload_workspace *w=malloc(sizeof(*w));
 struct mt_tqx_upload_result result,saved;
 struct mt_tqx_heap_input req={.copy={0x40100001,0x40200000,8191},
  .va={0x40000000,0x8400000000ULL,0x8100000000ULL,0x8100001000ULL,0xf000000000ULL}};
 void *image=malloc(65536),*scratch=malloc(65536);u32 i,j,k,refs[7],n;u64 va;
 assert(image && scratch && w);assert(!mt_device_profile_select(&p,0x1ed5,0x222));
 assert(!mt_bo_create(&tables,&bo_ops,&s,65536,4096));
 assert(!mt_gpu_vm_init(&vm,&tables,image,scratch,65536));assert(!mt_bo_put(&tables));
 for(i=0;i<7;i++) {
  bo[i]=&objects[i];n=i<5?mt_tqx_buffer_bytes[i]:8192;
  va=i<5?req.va[i]:(i==5?req.copy.src-1:req.copy.dst);
  assert(!mt_bo_create(bo[i],&bo_ops,&s,n+8192,4096));
  memset(bo[i]->backing.handle,0xa5,n+8192);
  assert(!mt_gpu_vm_bind(&vm,bo[i],va,4096,n,3));refs[i]=bo[i]->refs;
 }
 memset(&result,0xa5,sizeof(result));
 assert(!mt_tqx_upload(&result,sizeof(result),w,&io,&p,&vm,bo,&req));saved=result;
 assert(s.writes==9 && s.reads==9 && !vm.uploaded && !vm.active_uses && !vm.sealed);
 assert(w->stream.count==3 && !memcmp(result.record,w->stream.record,296));
 assert(!memcmp(result.root_export,w->stream.root_export,16));
 for(i=0;i<7;i++) {
  u8 *base=bo[i]->backing.handle;n=i<5?mt_tqx_buffer_bytes[i]:8192;
  for(j=0;j<4096;j++)assert(base[j]==0xa5 && base[4096+n+j]==0xa5);
  if(i>=5)for(j=0;j<n;j++)assert(base[4096+j]==0xa5);
 }
 assert(!memcmp((u8 *)bo[1]->backing.handle+4096,mt_tqx_program_bytes,MT_TQX_SHADER_BANK_BYTES));
 assert(!memcmp((u8 *)bo[2]->backing.handle+4096,mt_tqx_program_bytes+MT_TQX_SHADER_BANK_BYTES,176));
 /* Every page write/read/readback and every CPU-map failure unwinds refs.
  * Partial BO bytes are deliberately not claimed to be rolled back. */
 for(k=0;k<4;k++)for(j=1;j<=(k==3?18:9);j++) {
  s.writes=s.reads=s.maps=0;s.write_fail=s.read_fail=s.corrupt=s.map_fail=0;
  if(k==0)s.write_fail=j;
  if(k==1)s.read_fail=j;
  if(k==2)s.corrupt=j;
  if(k==3)s.map_fail=j;
  assert(mt_tqx_upload(&result,sizeof(result),w,&io,&p,&vm,bo,&req)==(k==3?-ENXIO:-EIO));
  assert(!memcmp(&result,&saved,sizeof(result)));
  for(i=0;i<7;i++)assert(bo[i]->refs==refs[i] && !bo[i]->cpu_users && !bo[i]->gpu_users);
 }
 s.write_fail=s.read_fail=s.corrupt=s.map_fail=0;
 for(i=0;i<7;i++) {
  void *mapping;
  s.writes=s.reads=0;assert(!mt_bo_cpu_begin(bo[i],&mapping));
  assert(mt_tqx_upload(&result,sizeof(result),w,&io,&p,&vm,bo,&req)==-EBUSY);
  assert(!s.writes && !s.reads);assert(!mt_bo_cpu_end(bo[i]));
  assert(!mt_bo_gpu_begin(bo[i]));
  assert(mt_tqx_upload(&result,sizeof(result),w,&io,&p,&vm,bo,&req)==-EBUSY);
  assert(!s.writes && !s.reads);assert(!mt_bo_gpu_end(bo[i]));
 }
 vm.sealed=true;
 /* Once mappings are fixed, a sealed root still permits content updates in
  * its existing BOs while idle; only the page table itself stays immutable. */
 s.writes=s.reads=0;
 assert(!mt_tqx_upload(&result,sizeof(result),w,&io,&p,&vm,bo,&req));
 assert(s.writes==9 && s.reads==9 && !memcmp(&result,&saved,sizeof(result)));
 vm.sealed=false;
	s.writes=s.reads=0;
 vm.active_uses=1;
 assert(mt_tqx_upload(&result,sizeof(result),w,&io,&p,&vm,bo,&req)==-EBUSY);vm.active_uses=0;
 assert(mt_tqx_upload(&result,sizeof(result)-1,w,&io,&p,&vm,bo,&req)==-EINVAL);
 assert(mt_tqx_upload(&result,sizeof(result),NULL,&io,&p,&vm,bo,&req)==-EINVAL);
 req.va[3]=req.va[2];
 assert(mt_tqx_upload(&result,sizeof(result),w,&io,&p,&vm,bo,&req)==-EINVAL);req.va[3]+=4096;
 assert(!s.writes && !s.reads && !memcmp(&result,&saved,sizeof(result)));
 /* A complete retry repairs partial buffers and clears stale trailing jobs. */
 req.copy.bytes=1;s.writes=s.reads=0;
 assert(!mt_tqx_upload(&result,sizeof(result),w,&io,&p,&vm,bo,&req));
 assert(s.writes==9 && s.reads==9 && w->stream.count==1);
 for(i=80;i<4096;i++)assert(!((u8 *)bo[0]->backing.handle)[4096+i]);
 for(i=96;i<4096;i++)assert(!((u8 *)bo[3]->backing.handle)[4096+i]);
 for(i=64;i<4096;i++)assert(!((u8 *)bo[4]->backing.handle)[4096+i]);
 for(i=0;i<7;i++)assert(!mt_bo_put(bo[i]));
 assert(!mt_gpu_vm_fini(&vm) && s.allocs==s.frees);
 free(scratch);free(image);free(w);
 puts("PASS: TQX nine-page upload/readback, nonzero BO offsets, padding/guards, 45 I/O/map faults, 14 busy resource gates, retry and balanced references; RAM only");
 return 0;
}
