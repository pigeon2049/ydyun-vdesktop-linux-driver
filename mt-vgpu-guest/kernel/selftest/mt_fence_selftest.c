// SPDX-License-Identifier: GPL-2.0
/* Real dma_fence + queue/event code. Registers and queues are ordinary RAM,
 * never PCI mappings. Fake events below are test inputs, not hardware acks. */
#include "../mt_marker_fence.h"

static int result = -ENODATA;
module_param(result, int, 0444);
static unsigned int checks, failure_line, callback_count;
module_param(checks, uint, 0444);
module_param(failure_line, uint, 0444);
module_param(callback_count, uint, 0444);
static struct dma_fence_cb callback;
static int gate(void *p) { return *(int *)p; }
static void completed(struct dma_fence *f, struct dma_fence_cb *cb)
{
	callback_count++;
}
static int consume(void *p, u32 dm, const struct mt_fw_event *e)
{
	return mt_marker_complete(p, dm, e);
}
#define CHECK(expr) do { \
	if (!(expr)) { result = -EINVAL; failure_line = __LINE__; goto done; } \
	checks++; \
} while (0)


/* RAM BOs with invented GPU addresses; never use the VRAM allocator here. */
struct job_ram_store { u64 next; u32 allocs, frees; };
static int job_alloc(void *p, u32 bytes, u32 alignment, struct mt_bo_backing *b)
{
 struct job_ram_store *s=p;
 void *mem=kvzalloc(bytes,GFP_KERNEL);
 if (!mem) return -ENOMEM;
 s->next=ALIGN(s->next,alignment);
 *b=(struct mt_bo_backing){mem,s->next,s->next,bytes};
 s->next+=bytes; s->allocs++;
 return 0;
}
static int job_clear(void *p, const struct mt_bo_backing *b)
{ memset(b->handle,0,b->bytes); return 0; }
static void job_free(void *p, const struct mt_bo_backing *b)
{ struct job_ram_store *s=p; kvfree(b->handle); s->frees++; }
static int job_map(void *p, const struct mt_bo_backing *b, void **out)
{ *out=b->handle; return 0; }
static void job_unmap(void *p, const struct mt_bo_backing *b) { }
static const struct mt_bo_ops job_ops={job_alloc,job_clear,job_free,job_map,job_unmap};

static void test_work_resources(struct mt_marker_store *s)
{
 struct job_ram_store ram={.next=0x600000000ULL};
 struct mt_bo tables={0}, command={0}, data={0};
 struct mt_gpu_vm vm={0};
 struct mt_execution_store execution;
 struct mt_execution_process process={0};
 struct mt_execution_context context={0};
 struct mt_execution_request request={.type=9,.command_va=0x200000,.bytes=128};
 struct mt_work_command_inputs in={.type=5,.command_va=0x200000,.bytes=128,
   .process_id=0xf123456789abcdefULL,.fence=0xdeadbeef};
 struct mt_fw_event e={0};
 struct dma_fence *f=NULL, *out=ERR_PTR(-EINPROGRESS);
 void *image=NULL,*scratch=NULL,*mapping;
 u32 wire, refs;
 bool command_owner=false, data_owner=false;
 image=kvzalloc(32768,GFP_KERNEL); scratch=kvzalloc(32768,GFP_KERNEL);
 CHECK(image && scratch);
 CHECK(!mt_bo_create(&tables,&job_ops,&ram,32768,4096));
 CHECK(!mt_gpu_vm_init(&vm,&tables,image,scratch,32768));
 CHECK(!mt_bo_put(&tables));
 CHECK(!mt_bo_create(&command,&job_ops,&ram,8192,4096)); command_owner=true;
 CHECK(!mt_bo_create(&data,&job_ops,&ram,4096,4096)); data_owner=true;
 CHECK(!mt_gpu_vm_bind(&vm,&command,0x200000,0,8192,0));
 CHECK(!mt_gpu_vm_bind(&vm,&command,0x400000,0,4096,0));
 CHECK(!mt_gpu_vm_bind(&vm,&data,0x600000,0,4096,0));
 {
  u32 saved_refs=data.refs;
  int fini_ret;
  /* A damaged later BO must not let fini release the table/earlier BOs. */
  data.refs=0;
  fini_ret=mt_gpu_vm_fini(&vm);
  data.refs=saved_refs;
  CHECK(fini_ret==-EUCLEAN && vm.tables==&tables && tables.refs==1 &&
   command.refs==3 && !ram.frees);
 }
 vm.uploaded=true; /* RAM fixture, not a hardware upload. */
 CHECK(!mt_gpu_vm_seal(&vm));
 mt_execution_store_init(&execution,&ram,&s->profile);
 CHECK(!mt_execution_process_create(&execution,&process,&vm,12345));
 CHECK(!mt_execution_context_create(&context,&process,4,9));
 CHECK(context.route.dm==5 && process.token==0 && vm.owners==1);
 CHECK(mt_execution_process_destroy(&process)==-EBUSY);
 CHECK(s->ops->submit_work(s,5,&vm,&command,&in,&out)==-EOPNOTSUPP && !vm.active_uses);
 s->work_ready=true; /* No live module sets this. */
 {
  struct mt_device_profile saved_profile=s->profile, local_profile;
  u64 next=s->next[5];
  CHECK(!mt_device_profile_select(&local_profile,0x1ed5,0x222));
  s->profile=local_profile;
  s->buffers=&ram; /* Reach profile gate after the ownership check. */
  in.type=9;
  CHECK(s->ops->submit_work(s,5,&vm,&command,&in,&out)==-EOPNOTSUPP);
  CHECK(s->ops->submit_context(s,&context,&command,&request,&out)==-EOPNOTSUPP);
  CHECK(s->next[5]==next && !vm.active_uses && !s->total && !command.gpu_users);
  s->profile=saved_profile;
  s->buffers=NULL;
  in.type=5;
 }
 CHECK(!mt_bo_cpu_begin(&data,&mapping));
 CHECK(s->ops->submit_work(s,5,&vm,&command,&in,&out)==-EBUSY);
 CHECK(!tables.gpu_users && !command.gpu_users && !vm.active_uses && !s->total);
 CHECK(!mt_bo_cpu_end(&data));
 refs=data.refs; data.refs=~(u32)0;
 CHECK(s->ops->submit_work(s,5,&vm,&command,&in,&out)==-EOVERFLOW);
 data.refs=refs;
 CHECK(!tables.gpu_users && !command.gpu_users && !vm.active_uses);
 writel(1,s->queue->queue+5*MT_FW_DM_BYTES+MT_FW_CURSOR_OFFSET+8);
 CHECK(s->ops->submit_work(s,5,&vm,&command,&in,&out)==-EAGAIN);
 CHECK(!s->total && !vm.active_uses && out==ERR_PTR(-EINPROGRESS));
 CHECK(command.refs==3 && data.refs==2 && tables.refs==1);
 writel(0,s->queue->queue+5*MT_FW_DM_BYTES+MT_FW_CURSOR_OFFSET+8);
 CHECK(s->ops->submit_context(s,&context,&command,&request,&out)==-EXDEV && !s->total);
 s->buffers=&ram;
 CHECK(!s->ops->submit_context(s,&context,&command,&request,&f));
 CHECK(context.active_jobs==1 && mt_execution_context_destroy(&context)==-EBUSY);
 CHECK(readl(s->queue->queue+5*MT_FW_DM_BYTES+0x20)==0);
 CHECK(readl(s->queue->queue+5*MT_FW_DM_BYTES+0x4c)==12345);
 wire=f->seqno;
 CHECK(readl(s->queue->queue+5*MT_FW_DM_BYTES+0x48)==wire && wire!=in.fence);
 CHECK(readl(s->queue->queue+5*MT_FW_DM_BYTES+0x0c)==0x69);
 CHECK(vm.active_uses==1 && command.gpu_users==1 && command.refs==4 && data.refs==3);
 CHECK(mt_bo_cpu_begin(&command,&mapping)==-EBUSY);
 CHECK(dma_fence_wait_timeout(f,false,0)==0 && vm.active_uses==1);
 CHECK(s->ops->submit_work(s,5,&vm,&command,&in,&out)==-EBUSY && s->total==1);
 CHECK(mt_work_job_cancel(&container_of(f,struct mt_marker_fence,fence)->job)==-EBUSY);
 CHECK(!mt_bo_put(&command)); command_owner=false;
 CHECK(!mt_bo_put(&data)); data_owner=false;
 CHECK(!ram.frees && mt_gpu_vm_fini(&vm)==-EBUSY);
 dma_fence_put(f); f=NULL; /* Pending owner alone must hold every resource. */
 e.words[2]=wire+1;
 CHECK(mt_marker_complete(s,5,&e)==-ESTALE && vm.active_uses==1);
 e.words[2]=wire; e.words[1]=0x101;
 CHECK(mt_marker_complete(s,5,&e)==-EOPNOTSUPP && vm.active_uses==1 && !ram.frees);
 e.words[1]=0;
 CHECK(!mt_marker_complete(s,5,&e));
 CHECK(!context.active_jobs);
 CHECK(!s->total && !vm.active_uses && !command.gpu_users && command.refs==2 && !ram.frees);
 CHECK(mt_marker_complete(s,5,&e)==-ENOENT);
 CHECK(mt_gpu_vm_fini(&vm)==-EBUSY); /* Job completion does not withdraw root. */
 CHECK(!mt_execution_context_destroy(&context));
 CHECK(!mt_execution_process_destroy(&process) && !vm.owners);
 CHECK(!execution.contexts && !execution.processes);
 vm.sealed=false; /* Only RAM fixture teardown may do this. */
 CHECK(!mt_gpu_vm_fini(&vm));
 CHECK(ram.allocs==3 && ram.frees==3);
 done:
 /* All pending work here is simulated. Complete it before fixture teardown. */
 while (!list_empty(&s->pending[5])) {
  struct mt_marker_fence *m=list_first_entry(&s->pending[5],struct mt_marker_fence,link);
  e.words[1]=0; e.words[2]=m->wire_id;
  mt_marker_complete(s,5,&e);
 }
 dma_fence_put(f);
 if (data.cpu_users) mt_bo_cpu_end(&data);
 if (context.process) mt_execution_context_destroy(&context);
 if (process.store) mt_execution_process_destroy(&process);
 if (vm.tables) { vm.sealed=false; mt_gpu_vm_fini(&vm); }
 else if (tables.refs) mt_bo_put(&tables);
 if (command_owner) mt_bo_put(&command);
 if (data_owner) mt_bo_put(&data);
 kvfree(image); kvfree(scratch);
 s->work_ready=false;
 s->buffers=NULL;
}

static struct mt_bo *tracked_root;
static u32 root_writes, root_reads;
static int tqx_write(struct mt_bo *bo,u64 offset,const void *src,u64 bytes)
{
 void *mapping;int ret=mt_bo_check_range(bo,offset,bytes);
 if(ret)return ret;
 ret=mt_bo_cpu_begin(bo,&mapping);if(ret)return ret;
 memcpy((u8 *)mapping+offset,src,bytes);ret=mt_bo_cpu_end(bo);
 if(!ret&&bo==tracked_root)root_writes++;
 return ret;
}
static int tqx_read(struct mt_bo *bo,u64 offset,void *dst,u64 bytes)
{
 void *mapping;int ret=mt_bo_check_range(bo,offset,bytes);
 if(ret)return ret;
 ret=mt_bo_cpu_begin(bo,&mapping);if(ret)return ret;
 memcpy(dst,(u8 *)mapping+offset,bytes);ret=mt_bo_cpu_end(bo);
 if(!ret&&bo==tracked_root)root_reads++;
 return ret;
}
static const struct mt_tqx_upload_ops tqx_io={tqx_write,tqx_read};
struct tqx_fixture {
 struct mt_gpu_vm vm;
 struct mt_bo tables,objects[9];
 struct mt_tqx_work work;
 struct mt_execution_process process;
 struct mt_execution_context context;
};
static void test_tqx_fence(struct mt_marker_store *s)
{
 struct tqx_fixture *t=kzalloc(sizeof(*t),GFP_KERNEL);
 struct mt_tqx_submission_workspace *w=kzalloc(sizeof(*w),GFP_KERNEL);
 struct job_ram_store ram={.next=0x600000000ULL};
 struct mt_execution_store execution;
 struct mt_device_profile saved_profile=s->profile,profile;
 struct mt_bo *bo[9];
 struct mt_tqx_submission_input req={
  .stream={.copy={0x40100001,0x40200000,8191},
   .va={0x40000000,0x8400000000ULL,0x8100000000ULL,0x8100001000ULL,0xf000000000ULL}},
  .dma_va=0x40010000,.state_va=0x40020000};
 void *image=kvzalloc(65536,GFP_KERNEL),*scratch=kvzalloc(65536,GFP_KERNEL),*mapping;
 void *saved_root=NULL;
 struct dma_fence *f=NULL,*out=ERR_PTR(-EINPROGRESS);
 struct mt_fw_event e={0};
 u8 held_packet[MT_FW_COMMAND_BYTES];
 u32 i,bytes,owned=0,wire,callbacks=callback_count;
 u64 va,next;
 CHECK(t && w && image && scratch && !s->total);
 memset_io(s->queue->queue+MT_FW_DM_BYTES,0,MT_FW_DM_BYTES);
 CHECK(!mt_device_profile_select(&profile,0x1ed5,0x222));
 s->profile=profile;s->buffers=&ram;s->work_ready=false;
 CHECK(!mt_bo_create(&t->tables,&job_ops,&ram,65536,4096));
 tracked_root=&t->tables;root_writes=root_reads=0;
 CHECK(!mt_gpu_vm_init(&t->vm,&t->tables,image,scratch,65536));
 CHECK(!mt_bo_put(&t->tables));
 for(i=0;i<9;i++) {
  bo[i]=&t->objects[i];bytes=i<5?mt_tqx_buffer_bytes[i]:(i==8?4096:8192);
  va=i<5?req.stream.va[i]:(i==5?req.stream.copy.src-1:
   i==6?req.stream.copy.dst:i==7?req.dma_va:req.state_va);
  CHECK(!mt_bo_create(bo[i],&job_ops,&ram,bytes,4096));owned++;
  CHECK(!mt_gpu_vm_bind(&t->vm,bo[i],va,0,bytes,MT_GPU_MAP_DEFAULT));
 }
 mt_execution_store_init(&execution,&ram,&profile);
 CHECK(!mt_execution_process_create(&execution,&t->process,&t->vm,4321));
 CHECK(!mt_execution_context_create(&t->context,&t->process,1,0));
 CHECK(!mt_tqx_work_prepare(&t->work,w,&tqx_io,&profile,1,&t->context,bo,&req));
 memcpy(held_packet,t->work.job.packet,sizeof(held_packet));
 CHECK(t->work.job.count==10 && t->vm.active_uses==1 && t->context.active_jobs==1);
 CHECK(t->vm.uploaded && !t->vm.sealed && root_writes==1 && root_reads==1);
 for(i=0;i<9;i++)CHECK(bo[i]->gpu_users==1 && bo[i]->refs==3);
 CHECK(s->ops->submit_tqx_work(s,&t->work,&out)==-EOPNOTSUPP);
 s->work_ready=true;
 CHECK(s->ops->submit_tqx_work(s,&t->work,&out)==-EOPNOTSUPP); /* Unsealed. */
 CHECK(!mt_gpu_vm_seal(&t->vm));
 saved_root=kmemdup(t->tables.backing.handle,t->vm.capacity,GFP_KERNEL);
 CHECK(saved_root && t->vm.sealed && t->vm.uploaded);
 s->ready=false;
 CHECK(s->ops->submit_tqx_work(s,&t->work,&out)==-EHOSTDOWN);s->ready=true;
 s->buffers=NULL;
 CHECK(s->ops->submit_tqx_work(s,&t->work,&out)==-EXDEV);s->buffers=&ram;
 next=s->next[1];
 *(int *)s->opaque=-EAGAIN;
 CHECK(s->ops->submit_tqx_work(s,&t->work,&out)==-EAGAIN && s->next[1]==next);
 *(int *)s->opaque=0;
 writel(64,s->queue->queue+MT_FW_DM_BYTES+MT_FW_CURSOR_OFFSET);
 CHECK(s->ops->submit_tqx_work(s,&t->work,&out)==-EIO && !s->total);
 CHECK(!memcmp(held_packet,t->work.job.packet,sizeof(held_packet)) && t->vm.active_uses==1);
 writel(0,s->queue->queue+MT_FW_DM_BYTES+MT_FW_CURSOR_OFFSET);
 next=s->next[1];
 writel(1,s->queue->queue+MT_FW_DM_BYTES+MT_FW_CURSOR_OFFSET+8);
 CHECK(s->ops->submit_tqx_work(s,&t->work,&out)==-EAGAIN);
 CHECK(s->next[1]==next+1 && !s->total && out==ERR_PTR(-EINPROGRESS));
 CHECK(t->work.context==&t->context && t->work.job.state==MT_JOB_HELD && t->work.job.packet[0x48]==0);
 CHECK(!memcmp(held_packet,t->work.job.packet,sizeof(held_packet)));
 CHECK(t->context.active_jobs==1 && t->vm.active_uses==1 && t->work.job.count==10);
 writel(0,s->queue->queue+MT_FW_DM_BYTES+MT_FW_CURSOR_OFFSET+8);
 CHECK(!s->ops->submit_tqx_work(s,&t->work,&f));wire=f->seqno;
 CHECK(!t->work.context && t->work.job.state==MT_JOB_EMPTY && s->total==1);
 CHECK(t->context.active_jobs==1 && t->vm.active_uses==1);
 CHECK(readl(s->queue->queue+MT_FW_DM_BYTES+0x0c)==0x67);
 CHECK(readq(s->queue->queue+MT_FW_DM_BYTES+0x18)==t->tables.backing.gpu_pa);
 CHECK(readq(s->queue->queue+MT_FW_DM_BYTES+0x28)==req.dma_va);
 CHECK(readl(s->queue->queue+MT_FW_DM_BYTES+0x30)==4864);
 CHECK(readl(s->queue->queue+MT_FW_DM_BYTES+0x48)==wire);
 CHECK(readl(s->queue->queue+MT_FW_DM_BYTES+0x4c)==4321);
 CHECK(mt_tqx_work_cancel(&t->work)==-EINVAL);
 CHECK(!dma_fence_add_callback(f,&callback,completed));
 CHECK(dma_fence_wait_timeout(f,false,0)==0 && callback_count==callbacks);
 CHECK(mt_execution_context_destroy(&t->context)==-EBUSY);
 for(i=0;i<9;i++)CHECK(mt_bo_cpu_begin(bo[i],&mapping)==-EBUSY);
 while(owned) { owned--;CHECK(!mt_bo_put(bo[owned])); }
 dma_fence_put(f);f=NULL;
 CHECK(!ram.frees && mt_gpu_vm_fini(&t->vm)==-EBUSY);
 e.words[2]=wire+1;
 CHECK(mt_marker_complete(s,1,&e)==-ESTALE);
 e.words[2]=wire;e.words[1]=0x101;
 CHECK(mt_marker_complete(s,1,&e)==-EOPNOTSUPP);
 CHECK(t->vm.active_uses==1 && t->context.active_jobs==1 && !ram.frees);
 e.words[1]=0;
 CHECK(!mt_marker_complete(s,1,&e));
 CHECK(!t->vm.active_uses && !t->context.active_jobs && callback_count==callbacks+1);
 for(i=0;i<9;i++)CHECK(!bo[i]->gpu_users && bo[i]->refs==1);
 CHECK(mt_marker_complete(s,1,&e)==-ENOENT && !s->total);
 CHECK(t->work.job.state==MT_JOB_EMPTY && !t->work.context);
 CHECK(!mt_tqx_work_prepare(&t->work,w,&tqx_io,&profile,1,&t->context,bo,&req));
 CHECK(t->vm.sealed && t->vm.uploaded && t->work.job.count==10);
 CHECK(root_writes==1 && root_reads==1 &&
       !memcmp(saved_root,t->tables.backing.handle,t->vm.capacity));
 CHECK(readq(t->work.job.packet+0x18)==t->tables.backing.gpu_pa);
 CHECK(!mt_tqx_work_cancel(&t->work));
 CHECK(!mt_execution_context_destroy(&t->context));
 CHECK(!mt_execution_process_destroy(&t->process));
 t->vm.sealed=false; /* RAM-only teardown; not a hardware root withdrawal. */
 CHECK(!mt_gpu_vm_fini(&t->vm));
 CHECK(ram.allocs==10 && ram.frees==10);
done:
 while(!list_empty(&s->pending[1])) {
  struct mt_marker_fence *m=list_first_entry(&s->pending[1],struct mt_marker_fence,link);
  e.words[1]=0;e.words[2]=m->wire_id;mt_marker_complete(s,1,&e);
 }
 dma_fence_put(f);
 if(t) {
  if(t->work.context)mt_tqx_work_cancel(&t->work);
  if(t->context.process)mt_execution_context_destroy(&t->context);
  if(t->process.store)mt_execution_process_destroy(&t->process);
  if(t->vm.tables) {t->vm.sealed=false;mt_gpu_vm_fini(&t->vm);}
  else if(t->tables.refs)mt_bo_put(&t->tables);
  while(owned)mt_bo_put(&t->objects[--owned]);
 }
 tracked_root=NULL;root_writes=root_reads=0;kvfree(saved_root);
 kvfree(scratch);kvfree(image);kfree(w);kfree(t);
 s->profile=saved_profile;s->buffers=NULL;s->work_ready=false;
 *(int *)s->opaque=0;
}

static int __init mt_fence_selftest_init(void)
{
	struct mt_marker_store s;
	struct mt_fw_queue_io q = {0};
	struct mutex lock;
	struct dma_fence *f[4] = {0}, *out;
	struct mt_fw_event e = {0};
	void *queue = NULL, *regs = NULL;
	u32 ids[4], i, dm, n;
	int ret, gate_result = 0;
	bool initialized = false, locked = false;
	struct mt_device_profile profile;
	mutex_init(&lock);
	queue = kvzalloc(MT_FW_QUEUE_BYTES, GFP_KERNEL);
	regs = kzalloc(4096, GFP_KERNEL);
	if (!queue || !regs) {
		result = -ENOMEM;
		goto done;
	}
	q.queue = (void __iomem *)queue;
	q.registers = (void __iomem *)regs;
	for (dm = 0; dm < MT_FW_DM_COUNT; dm++)
		spin_lock_init(&q.producer[dm]);
	CHECK(!mt_device_profile_select(&profile, 0x1ed5, 0x600)); /* CE3 RAM fixture. */
	mt_marker_store_init(&s, &lock, &q, &profile);
	s.can_submit = gate;
	s.opaque = &gate_result;
	initialized = true;
	mutex_lock(&lock);
	locked = true;
	out = ERR_PTR(-EINPROGRESS);
	CHECK(s.ops->submit(&s, 1, &out) == -EHOSTDOWN && out == ERR_PTR(-EINPROGRESS));
	CHECK(s.ops->submit(&s, 0, &out) == -EINVAL);
	CHECK(s.ops->submit(&s, 6, &out) == -EINVAL);
	s.ready = true; /* RAM fixture only, never enabled on the live GPU. */
	gate_result = -EAGAIN;
	CHECK(s.ops->submit(&s, 1, &out) == -EAGAIN && s.next[1] == 1 && !s.total);
	gate_result = -EHOSTDOWN;
	CHECK(s.ops->submit(&s, 1, &out) == -EHOSTDOWN && s.next[1] == 1 && !s.total);
	gate_result = 0;
	writel(64, q.queue + MT_FW_DM_BYTES + MT_FW_CURSOR_OFFSET);
	CHECK(s.ops->submit(&s, 1, &out) == -EIO && !s.total && out == ERR_PTR(-EINPROGRESS));
	writel(0, q.queue + MT_FW_DM_BYTES + MT_FW_CURSOR_OFFSET);
	writel(1, q.queue + MT_FW_DM_BYTES + MT_FW_CURSOR_OFFSET + 8);
	CHECK(s.ops->submit(&s, 1, &out) == -EAGAIN && !s.total && out == ERR_PTR(-EINPROGRESS));
	CHECK(!readl(q.registers + 0xb00));
	writel(0, q.queue + MT_FW_DM_BYTES + MT_FW_CURSOR_OFFSET + 8);
	for (i = 0; i < 3; i++) {
		dm = i == 2 ? 2 : 1;
		CHECK(!s.ops->submit(&s, dm, &f[i]));
		ids[i] = f[i]->seqno;
		CHECK(dma_fence_get_status(f[i]) == 0);
	}
	CHECK(s.total == 3 && s.count[1] == 2 && s.count[2] == 1);
	CHECK(readl(q.queue + MT_FW_DM_BYTES + 0x0c) == 100);
	CHECK(readl(q.queue + MT_FW_DM_BYTES + 0x48) == ids[0]);
	CHECK(readl(q.registers + 0xb00) == 2);
	s.next[3] = 0xffffffffULL;
	CHECK(!s.ops->submit(&s, 3, &f[3]) && f[3]->seqno == 0xffffffffULL);
	ids[3] = f[3]->seqno;
	CHECK(s.ops->submit(&s, 3, &out) == -EOVERFLOW && s.total == 4);
	CHECK(!dma_fence_add_callback(f[0], &callback, completed));
	CHECK(dma_fence_wait_timeout(f[0], false, 0) == 0);
	dma_fence_put(f[1]);
	f[1] = NULL; /* Dropping the caller does not remove pending ownership. */
	e.words[2] = ids[1];
	CHECK(mt_marker_complete(&s, 1, &e) == -ESTALE && s.total == 4 && !callback_count);
	e.words[2] = ids[0];
	CHECK(mt_marker_complete(&s, 4, &e) == -ENOENT);
	for (i = 0; i < 4; i++) {
		e.words[1] = (u32[]){5, 0x50, 0x101, 7}[i];
		CHECK(mt_marker_complete(&s, 1, &e) == -EOPNOTSUPP && s.total == 4);
	}
	e.words[1] = 0;
	memcpy_toio(q.queue + MT_FW_DM_BYTES + MT_FW_EVENT_OFFSET, &e, sizeof(e));
	/* Duplicate first ID in the second slot must not complete the next job. */
	memcpy_toio(q.queue + MT_FW_DM_BYTES + MT_FW_EVENT_OFFSET + 24, &e, sizeof(e));
	writel(2, q.queue + MT_FW_DM_BYTES + MT_FW_CURSOR_OFFSET + 32);
	ret = mt_fw_event_drain(&mt_fw_event_io_ops, &q, MT_FW_QUEUE_BYTES, 378, consume, &s, &n);
	CHECK(ret == -ESTALE && n == 1 && s.total == 3 && callback_count == 1);
	CHECK(readl(q.queue + MT_FW_DM_BYTES + MT_FW_CURSOR_OFFSET + 40) == 1);
	CHECK(dma_fence_get_status(f[0]) == 1);
	CHECK(dma_fence_wait_timeout(f[0], false, 0) == 1);
	e.words[2] = ids[1];
	memcpy_toio(q.queue + MT_FW_DM_BYTES + MT_FW_EVENT_OFFSET + 24, &e, sizeof(e));
	CHECK(!mt_fw_event_drain(&mt_fw_event_io_ops, &q, MT_FW_QUEUE_BYTES, 1, consume, &s, &n));
	CHECK(n == 1 && s.total == 2);
	for (dm = 2; dm < 4; dm++) {
		e.words[2] = ids[dm];
		memcpy_toio(q.queue + dm * MT_FW_DM_BYTES + MT_FW_EVENT_OFFSET, &e, sizeof(e));
		writel(1, q.queue + dm * MT_FW_DM_BYTES + MT_FW_CURSOR_OFFSET + 32);
	}
	CHECK(!mt_fw_event_drain(&mt_fw_event_io_ops, &q, MT_FW_QUEUE_BYTES, 378, consume, &s, &n));
	CHECK(n == 2 && !s.total && s.completed == 4);
	CHECK(dma_fence_get_status(f[2]) == 1 && dma_fence_get_status(f[3]) == 1);
	/* Software and hardware ring capacity both remain bounded. */
	for (i = 0; i < 63; i++) {
		out = NULL;
		CHECK(!s.ops->submit(&s, 4, &out));
		dma_fence_put(out);
	}
	out = ERR_PTR(-EINPROGRESS);
	CHECK(s.ops->submit(&s, 4, &out) == -EAGAIN && s.total == 63 && out == ERR_PTR(-EINPROGRESS));
	for (i = 0; i < 63; i++) {
		struct mt_marker_fence *m = list_first_entry(&s.pending[4], struct mt_marker_fence, link);
		e.words[2] = m->wire_id;
		CHECK(!mt_marker_complete(&s, 4, &e));
	}
	CHECK(!s.total && s.completed == 67 && callback_count == 1);
	result = 0;
	test_work_resources(&s);
	if (!result)
		test_tqx_fence(&s);
done:
	if (locked)
		mutex_unlock(&lock);
	if (initialized) {
		mutex_lock(&lock);
		/* Test-only teardown: these queues/packets never reached hardware. */
		for (dm = 0; dm < MT_FW_DM_COUNT; dm++)
			while (!list_empty(&s.pending[dm])) {
				struct mt_marker_fence *m = list_first_entry(&s.pending[dm], struct mt_marker_fence, link);
				e.words[1] = 0;
				e.words[2] = m->wire_id;
				mt_marker_complete(&s, dm, &e);
			}
		mutex_unlock(&lock);
	}
	for (i = 0; i < ARRAY_SIZE(f); i++)
		dma_fence_put(f[i]);
	kvfree(queue);
	kfree(regs);
	pr_info("mt_fence_selftest: result=%d checks=%u failure_line=%u callback_count=%u; RAM queues only\n",
		result, checks, failure_line, callback_count);
	return 0;
}
static void __exit mt_fence_selftest_exit(void) { }
module_init(mt_fence_selftest_init);
module_exit(mt_fence_selftest_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Guest event and dma_fence selftest using RAM queues only");
