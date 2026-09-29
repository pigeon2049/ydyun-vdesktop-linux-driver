#include "../kernel/mt_ce_paging.h"
int export_one(void *out, u32 capacity, const void *record, u32 bytes)
{
	return mt_ce_record_export_one(out, capacity, record, bytes);
}
int encode(void *out, u32 capacity, const void *record, u32 bytes, u64 state_va)
{
	return mt_ce_paging_encode(out, capacity, record, bytes, state_va);
}
int prepare(void *out, u32 capacity, const struct mt_ce3_paging_input *in)
{
	struct mt_bo table = {.refs=1}, command = {.refs=1}, src = {.refs=1}, dst = {.refs=1};
	struct mt_gpu_vm vm = {.tables=&table, .count=3};
	command.backing = (struct mt_bo_backing){.bytes=8192, .gpu_pa=0x600000000ULL};
	src.backing = (struct mt_bo_backing){.bytes=4096, .gpu_pa=0x600002000ULL};
	dst.backing = (struct mt_bo_backing){.bytes=4096, .gpu_pa=0x600003000ULL};
	vm.bindings[0] = (struct mt_vm_binding){&command, in->stream.command_va & ~4095ULL, 0, 8192, 0};
	vm.bindings[1] = (struct mt_vm_binding){&src, in->stream.copy.src, 0, 4096, 0};
	vm.bindings[2] = (struct mt_vm_binding){&dst, in->stream.copy.dst, 0, 4096, 0};
	return mt_ce3_paging_prepare(out, capacity, &vm, &command, &src, &dst, in);
}
