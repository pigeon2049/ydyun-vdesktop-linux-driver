/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_EXECUTION_CONTEXT_H
#define MT_GUEST_EXECUTION_CONTEXT_H
#include "mt_work_command.h"
#include "mt_device_profile.h"

/* Linux software objects, not copies of the Windows 0x88-byte allocation.
 * All calls share the BO/VM/session lock. Objects are caller-owned; a failed
 * destroy must retain their storage. No firmware registration is implied. */
struct mt_execution_store {
	void *buffers;
	struct mt_device_profile profile;
	u64 next_token;
	u32 processes, contexts;
	bool exhausted;
};
struct mt_execution_process {
	struct mt_execution_store *store;
	struct mt_gpu_vm *vm;
	u64 token;
	u32 pid, contexts;
};
struct mt_pool_slice;
struct mt_execution_context {
	struct mt_execution_process *process;
	struct mt_node_route route;
	u32 active_jobs, pool_slices;
	/* TQX upload storage is allocated once per context and reused after each
	 * matching fence. The page-table mapping and pool records stay stable. */
	struct mt_pool_slice *tqx_pool_slices[3];
};
struct mt_execution_request {
	u64 command_va;
	u32 type, bytes, submit_flags;
};
static inline void mt_execution_store_init(struct mt_execution_store *s, void *buffers,
		const struct mt_device_profile *profile)
{
	*s = (struct mt_execution_store){.buffers = buffers};
	if (profile)
		s->profile = *profile;
}
static inline int mt_execution_process_create(struct mt_execution_store *s,
		struct mt_execution_process *p, struct mt_gpu_vm *vm, u32 pid)
{
	if (!s || !p || p->store || !vm || !vm->tables)
		return -EINVAL;
	if (vm->tables->store != s->buffers)
		return -EXDEV;
	if (s->exhausted || s->processes == ~(u32)0 || vm->owners == ~(u32)0)
		return -EOVERFLOW;
	*p = (struct mt_execution_process){.store = s, .vm = vm,
		.token = s->next_token, .pid = pid};
	/* Reference 140015048 assigns a monotonically increasing adapter token.
	 * Linux refuses wrap/reuse within this store's lifetime. */
	if (s->next_token == ~(u64)0)
		s->exhausted = true;
	else
		s->next_token++;
	s->processes++;
	vm->owners++;
	return 0;
}
static inline int mt_execution_process_destroy(struct mt_execution_process *p)
{
	if (!p || !p->store)
		return -EINVAL;
	/* SIMDestroyProcess first releases the process's GPU VA reservations and
	 * MMU context. Keep this Linux owner alive while any VM work is pending;
	 * the caller must drain matching fences before dropping the process. */
	if (p->contexts || p->vm->active_uses)
		return -EBUSY;
	p->vm->owners--;
	p->store->processes--;
	memset(p, 0, sizeof(*p));
	return 0;
}
static inline int mt_execution_context_create(struct mt_execution_context *c,
		struct mt_execution_process *p, u32 node_type, u32 node_index)
{
	struct mt_node_route route;
	int ret;
	if (!c || c->process || !p || !p->store)
		return -EINVAL;
	ret = mt_node_route_build(&route, node_type, node_index);
	if (ret)
		return ret;
	/* PCIe DMA/timer contexts require a different execution path. */
	if (!route.dm || route.dm >= MT_FW_DM_COUNT)
		return -EOPNOTSUPP;
	ret = mt_device_profile_node(&p->store->profile, node_type);
	if (ret)
		return ret;
	if (p->contexts == ~(u32)0 || p->store->contexts == ~(u32)0)
		return -EOVERFLOW;
	*c = (struct mt_execution_context){.process = p, .route = route};
	p->contexts++;
	p->store->contexts++;
	return 0;
}
static inline int mt_execution_context_destroy(struct mt_execution_context *c)
{
	if (!c || !c->process)
		return -EINVAL;
	if (c->active_jobs || c->pool_slices)
		return -EBUSY;
	c->process->contexts--;
	c->process->store->contexts--;
	memset(c, 0, sizeof(*c));
	return 0;
}
static inline int mt_execution_context_inputs(struct mt_work_command_inputs *out,
		const struct mt_execution_context *c, const struct mt_execution_request *r)
{
	int ret;
	if (!out || !c || !c->process || !r)
		return -EINVAL;
	ret = mt_device_profile_work(&c->process->store->profile, r->type);
	if (ret)
		return ret;
	*out = (struct mt_work_command_inputs){
		.root_pa = c->process->vm->tables->backing.gpu_pa,
		.process_id = c->process->token, .process_pid = c->process->pid,
		.command_va = r->command_va, .type = r->type,
		.bytes = r->bytes, .submit_flags = r->submit_flags};
	return 0;
}
#endif
