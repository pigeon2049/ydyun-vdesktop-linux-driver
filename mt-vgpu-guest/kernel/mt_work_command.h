/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_WORK_COMMAND_H
#define MT_GUEST_WORK_COMMAND_H
#include "mt_fw_queue.h"
#include "mt_gpu_vm.h"

/* Descriptor type numbers from 1400136dc; fields from 14001475c.
 * SIMSubmitCommandVirtual (140014344) supplies root PC, a process token,
 * and command GPU VA. The process token is NOT a GPU address or Linux PID. */
struct mt_work_command_inputs {
	u64 root_pa, process_id, command_va;
	u32 type, process_pid, bytes, fence, submit_flags;
};
static inline u32 mt_work_opcode(u32 type)
{
	switch (type) {
	case 1: return 0x67; /* Transfer Op */
	case 3: case 11: return 0x66; /* RGXVertex / UniversalQueue */
	case 5: return 0x68; /* RGXCompute */
	case 6: return 0x65; /* Preempt: not enabled by prepare below */
	case 9: return 0x69; /* CopyEngine */
	default: return 0x64;
	}
}

/* Exact packet serializer, not an admission or address validation API.
 * NULL input reproduces the reference NULL-descriptor marker path. */
static inline int mt_work_command_encode(void *out, u32 capacity,
		const struct mt_work_command_inputs *input, u32 fence)
{
	u8 packet[MT_FW_COMMAND_BYTES] = {0};
	if (!out || capacity < sizeof(packet))
		return -EINVAL;
	mt_fw_put32(packet, 0x0c, input ? mt_work_opcode(input->type) : 0x64);
	mt_fw_put32(packet, 0x48, fence);
	if (input) {
		mt_fw_put32(packet, 0x08, (input->submit_flags >> 5) & 4);
		mt_fw_put64(packet, 0x18, input->root_pa);
		mt_fw_put64(packet, 0x20, input->process_id);
		mt_fw_put64(packet, 0x28, input->command_va);
		mt_fw_put32(packet, 0x30, input->bytes);
		mt_fw_put32(packet, 0x4c, input->process_pid);
	}
	memcpy(out, packet, sizeof(packet));
	return 0;
}

/* CPU staging only; hold the common BO/VM lock. A future publisher must pin
 * the VM and every referenced resource, seal/upload tables and revalidate.
 * The command stream itself is not validated by this range check. */
static inline int mt_work_command_prepare(void *out, u32 capacity,
		const struct mt_gpu_vm *vm, const struct mt_bo *command,
		const struct mt_work_command_inputs *request)
{
	struct mt_work_command_inputs in;
	u32 i;
	if (!out || capacity < MT_FW_COMMAND_BYTES || !vm || !vm->tables ||
	    !command || !command->refs || !request || !request->bytes ||
	    (request->submit_flags & ~0x80U))
		return -EINVAL;
	if (request->type != 1 && request->type != 3 && request->type != 5 &&
	    request->type != 9 && request->type != 11)
		return -EOPNOTSUPP;
	if (request->command_va >= (1ULL << MT_GPU_VA_BITS) ||
	    request->bytes > (1ULL << MT_GPU_VA_BITS) - request->command_va)
		return -ERANGE;
	if (command->store != vm->tables->store || command->ops != vm->tables->ops)
		return -EXDEV;
	for (i = 0; i < vm->count; i++) {
		const struct mt_vm_binding *b = &vm->bindings[i];
		u64 offset;
		if (b->bo != command || request->command_va < b->va)
			continue;
		offset = request->command_va - b->va;
		if (offset > b->bytes || request->bytes > b->bytes - offset)
			continue;
		if (mt_bo_check_range(command, b->offset + offset, request->bytes))
			return -ERANGE;
		in = *request;
		/* A caller may not substitute a raw root address for this VM's BO. */
		in.root_pa = vm->tables->backing.gpu_pa;
		return mt_work_command_encode(out, capacity, &in, in.fence);
	}
	return -ENOENT;
}

/* Logical node type -> firmware queue mapping from 14000c1e4. DM6 is the
 * reference sentinel for non-firmware PCIe DMA/timer nodes, not a seventh
 * entry in the six-DM firmware ring array. No type-to-node auto selection. */
struct mt_node_route {
	u32 type, index, dm, flags, capabilities, scheduling_class, engines, group;
};
static inline int mt_node_route_build(struct mt_node_route *out, u32 type, u32 index)
{
	struct mt_node_route r = {.type = type, .index = index, .dm = 6, .engines = 1};
	if (!out || type > 8)
		return -EINVAL;
	switch (type) {
	case 1: r.dm = 1; r.flags = 1; r.capabilities = 4; r.scheduling_class = 6; break;
	case 2: r.dm = 3; r.flags = 1; r.capabilities = 8; break;
	case 3: r.dm = 4; r.capabilities = 0x10; break;
	case 4: r.dm = 5; r.capabilities = 0x10; break;
	case 5: r.dm = 2; r.flags = 1; r.capabilities = 0xf; r.scheduling_class = 1; break;
	case 6: r.scheduling_class = 6; break;
	case 8: r.dm = 2; r.flags = 1; r.capabilities = 0xf; r.group = 1; break;
	}
	*out = r;
	return 0;
}
#endif
