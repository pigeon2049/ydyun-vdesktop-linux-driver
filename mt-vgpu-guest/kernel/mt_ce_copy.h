/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MT_GUEST_CE_COPY_H
#define MT_GUEST_CE_COPY_H
#include "mt_work_command.h"

#define MT_CE_COPY_BYTES 40U
/* Packet field controls only; their memory-domain/cache policy is not yet
 * inferred for this Guest. Never select these from untrusted userspace. */
#define MT_CE_SRC_MODE2 1U
#define MT_CE_DST_MODE2 2U
#define MT_CE_BIT59 4U
#define MT_CE_DST_TAG 8U
struct mt_ce_copy_input {
	u64 src, dst, bytes;
	u32 version, flags;
};
/* Isolated linear-copy instruction, NOT a complete executable stream.
 * CE1/2: 1400df1a4. CE3: 1400e5df4. Start/end/synchronization commands and
 * optional 24-byte prefix must be implemented before live submission. */
static inline int mt_ce_copy_encode(void *out, u32 capacity,
		const struct mt_ce_copy_input *in)
{
	u8 packet[MT_CE_COPY_BYTES] = {0};
	u64 header, length, dst;
	if (!out || !in || capacity < sizeof(packet) ||
	    in->version < 1 || in->version > 3 || (in->flags & ~15U))
		return -EINVAL;
	header = in->version == 3 ? 0x20100e0000000000ULL : 0x2010048000000000ULL;
	header |= (u64)!!(in->flags & MT_CE_SRC_MODE2) << 31;
	header |= (u64)!!(in->flags & MT_CE_DST_MODE2) << 32;
	header |= (u64)!!(in->flags & MT_CE_BIT59) << 59;
	dst = in->dst & 0xffffffffffffULL;
	if (in->version == 3 || (in->flags & MT_CE_DST_TAG))
		dst |= 0xc000000000000000ULL;
	length = in->bytes - 1;
	if (in->version != 3)
		length &= 0xffffffffULL;
	mt_fw_put64(packet, 0, header);
	mt_fw_put64(packet, 8, dst);
	mt_fw_put64(packet, 16, length);
	mt_fw_put64(packet, 24, in->src & 0xffffffffffffULL);
	mt_fw_put64(packet, 32, length);
	memcpy(out, packet, sizeof(packet));
	return 0;
}

/* Resolve against the specified BO, not simply any mapping of the VA. */
static inline int mt_ce_copy_resolve_access(const struct mt_gpu_vm *vm,
		const struct mt_bo *bo, u64 va, u32 bytes, bool write, u64 *physical)
{
	u32 i;
	if (!vm || !vm->tables || !bo || !bo->refs || !bytes)
		return -EINVAL;
	/* Linear physical-range/alias users do not yet accept scatter backing. */
	if (bo->page_pa)
		return -EOPNOTSUPP;
	if (va >= (1ULL << MT_GPU_VA_BITS) || bytes > (1ULL << MT_GPU_VA_BITS) - va)
		return -ERANGE;
	if (bo->store != vm->tables->store || bo->ops != vm->tables->ops)
		return -EXDEV;
	for (i = 0; i < vm->count; i++) {
		const struct mt_vm_binding *b = &vm->bindings[i];
		u64 offset;
		if (b->bo != bo || va < b->va)
			continue;
		offset = va - b->va;
		if (offset > b->bytes || bytes > b->bytes - offset)
			continue;
		if (mt_bo_check_range(bo, b->offset + offset, bytes))
			return -ERANGE;
		if (write && (b->flags & MT_GPU_MAP_READ_ONLY))
			return -EACCES;
		*physical = bo->backing.gpu_pa + b->offset + offset;
		return 0;
	}
	return -ENOENT;
}
static inline int mt_ce_copy_resolve(const struct mt_gpu_vm *vm,
		const struct mt_bo *bo, u64 va, u32 bytes, u64 *physical)
{
	return mt_ce_copy_resolve_access(vm, bo, va, bytes, false, physical);
}
/* CPU staging under the BO/VM lock; output unchanged on failure. Check both
 * mappings and backing overlap: distinct VA aliases may share storage.
 * Successful preparation is not permission to publish or execute. */
static inline int mt_ce_copy_prepare(void *out, u32 capacity,
		const struct mt_gpu_vm *vm, const struct mt_bo *src, const struct mt_bo *dst,
		const struct mt_ce_copy_input *in)
{
	u64 src_pa, dst_pa;
	int ret;
	if (!in || !in->bytes || in->bytes > 0xffffffffULL)
		return -EINVAL;
	ret = mt_ce_copy_resolve(vm, src, in->src, in->bytes, &src_pa);
	if (ret)
		return ret;
	ret = mt_ce_copy_resolve(vm, dst, in->dst, in->bytes, &dst_pa);
	if (ret)
		return ret;
	if (src_pa < dst_pa + in->bytes && dst_pa < src_pa + in->bytes)
		return -EINVAL; /* No memmove/overlap guarantee has been recovered. */
	return mt_ce_copy_encode(out, capacity, in);
}
#endif
